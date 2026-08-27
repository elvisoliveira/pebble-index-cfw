/*
 * Featureless Pebble Index CFW — this file is the whole app.
 * Each button press bumps a counter in the advertisement; the phone reads
 * "counter changed" as one click. Dev company id 0xFFFF (the phone side matches by
 * that + the fixed address). Advertising is continuous at a slow interval (~1-2 s, set
 * in user_config.h) — enough battery on a coin cell for years, with the click always
 * reliable. (Deeper idle modes were tried; see the note by POR_HOLD_TICKS.)
 */
#include <da1458x_config_basic.h>
#include <da1458x_config_advanced.h>
#include <user_config.h>
#include <rwip_config.h>
#include <board_config.h>   /* BTN_PIN/PORT + FLASH_* pins — ring vs kit (TARGET_KIT) */
#include <user_app.h>
#include <app_easy_gap.h>
#include <app_easy_timer.h>
#include <wkupct_quadec.h>
#include <gpio.h>
#include <spi.h>
#include <spi_flash.h>
#include <arch.h>
#include <string.h>

/*
 * Button (BTN_PIN/BTN_PORT come from board_config.h: ring P0_1, kit P0_7). The wkupct is
 * active-low, 1 event, 20 ms debounce. Button and the flash CS are separate pins (the
 * ring is FCGQFN24, CS=P0_9), so the release-triggered gesture is a conservative choice,
 * not a necessity.
 */

/* wkupct is one-shot; re-arm after every edge, alternating which edge we want.
 *
 * Re-arm for the edge OPPOSITE the pin's CURRENT level, read live — never from a
 * remembered flag. A static "wait for release" flag desyncs from the pin and freezes
 * the counter: after counting a press, adv_update() takes a few ms; a fast release
 * during that window passes before we re-arm, so arming for the release edge leaves us
 * waiting for a HIGH that already happened (the pin is already HIGH) — the next press
 * (falling edge) is then ignored and clicks stop registering until reboot. Reading the
 * level at arm time closes that race: if the button is already up we arm for the next
 * press instead. wait_release just records which edge we armed, for the count logic. */
static bool wait_release;

static void button_rearm(void)
{
    bool pressed = (GPIO_GetPinStatus(BTN_PORT, BTN_PIN) == 0);  /* active-low */
    wkupct_enable_irq(WKUPCT_PIN_SELECT(BTN_PORT, BTN_PIN),
                      WKUPCT_PIN_POLARITY(BTN_PORT, BTN_PIN,
                          pressed ? WKUPCT_PIN_POLARITY_HIGH : WKUPCT_PIN_POLARITY_LOW),
                      1, 20);
    wait_release = pressed;
}

/*
 * Advertisement = manufacturer-specific counter + the device name, rebuilt in full
 * on every click.
 *
 * app_easy_gap_update_adv_data() REPLACES the advertising data and the scan
 * response wholesale (SDK app.c:543-562), and the SDK only inserts the device name
 * when advertising STARTS (app.c:387-395). Pushing just the 5-byte counter blob
 * therefore dropped the name on the very first click — the ring stayed findable by
 * address but vanished from any name-based scan. So we emit both AD structures
 * together every time.
 */
#define ADV_MFR_LEN  5   /* len + type + company(2) + counter */
#define ADV_NAME_LEN (2 + USER_DEVICE_NAME_LEN)

_Static_assert(ADV_MFR_LEN + ADV_NAME_LEN <= ADV_DATA_LEN,
               "USER_DEVICE_NAME too long: advertisement would overflow");

static void adv_update(uint8_t counter)
{
    uint8_t adv[ADV_MFR_LEN + ADV_NAME_LEN];

    adv[0] = 0x04;              /* length of this AD structure */
    adv[1] = 0xFF;              /* manufacturer specific data */
    adv[2] = 0xFF;              /* company id 0xFFFF (dev/unassigned), LSB */
    adv[3] = 0xFF;              /*                                    MSB */
    adv[4] = counter;           /* the click counter the phone watches */
    adv[5] = USER_DEVICE_NAME_LEN + 1;
    adv[6] = GAP_AD_TYPE_COMPLETE_NAME;
    memcpy(&adv[7], USER_DEVICE_NAME, USER_DEVICE_NAME_LEN);

    app_easy_gap_update_adv_data(adv, sizeof(adv), NULL, 0);
}

/*
 * Recovery gesture: 5 clicks in quick succession (each gap under CLICK_WINDOW) drop
 * into the failsafe bootloader — the boot then sees an invalid image and enters the
 * failsafe (see ../../failsafe-recovery-test).
 *
 * This replaces a press-and-hold. A hold plays out entirely inside extended sleep,
 * where the timer meant to detect it fired late (after release) and never triggered
 * — proven dead on hardware across two flashes. A click, by contrast, is a wkupct
 * edge that reliably wakes the system (the click counter climbs cleanly), so counting
 * five of them runs on solid ground: enter_failsafe() fires SYNCHRONOUSLY on the 5th
 * click, with no timer in the trigger path.
 *
 * The gap timer that RESETS a partial run (click_reset, an app_easy_timer callback) was
 * silently dead, so click_run never cleared and every click — however far apart —
 * accumulated toward 5. Confirmed on the ring: clicks 3 s apart (2x CLICK_WINDOW) still
 * tripped the failsafe. Root cause was NOT extended sleep: user_modules_config.h had
 * EXCLUDE_DLG_TIMER=1, which drops app_timer_api_process_handler from app_process_handlers
 * (app_entry_point.c), so the app_easy_timer expiry message reached TASK_APP with no
 * handler and the callback never ran. Setting EXCLUDE_DLG_TIMER=0 restores it. (The
 * press-and-hold that this gesture replaced was likely killed by the same missing handler,
 * misread as "the timer fires late in sleep".) A GATT Control Point 0x00 write is a
 * second, button-independent path to the same enter_failsafe().
 */
#define CLICKS_TO_FAILSAFE 5
#define CLICK_WINDOW 150   /* 1.5 s in 10 ms units: a longer gap restarts the count */

static timer_hnd click_timer = EASY_TIMER_INVALID_TIMER;
static uint8_t click_run;   /* consecutive fast clicks so far */

/* Battery: the ring just advertises continuously at a slow interval (~1-2 s, set in
 * user_config.h). That already gives years on a coin cell (self-discharge dominates),
 * with the click ALWAYS reliable. Deeper idle modes (advertise-on-click bursts,
 * hibernation, an extended-sleep toggle) were all tried and all hit the SDK/ROM
 * sleep/advertising state machine being non-deterministic on this part — the click
 * (the whole feature) went flaky. Continuous slow advertising is the stable choice.
 *
 * Bonus recovery net: hold the button ~2.5 s -> hardware POR reset (reboots if the app
 * ever hangs). A tap stays a click; 5 fast taps stay the failsafe. */
#define POR_HOLD_TICKS 20   /* por_time x 4096 x RC32K period ~= por_time x 128 ms (~2.5 s) */
static uint8_t click_n;     /* the advertised click counter */

/*
 * Drop into the failsafe by invalidating the primary image, then resetting.
 *
 * The offset is NOT a guess and there is NO product header on this ring: the
 * ring's own firmware translates the Telesto record 0x40060000 to physical 0x5000
 * (FUN_07fc3b48 in the stock app, FUN_07fc2df0 in the failsafe), and the dump in
 * failsafe-recovery-test/materials/ confirms it byte-for-byte. The boot validates
 * a single image header there — sig 0x7051 + validflag 0xAA at +2 — so clearing
 * that one byte is enough. An earlier version of this hook searched for an
 * AN-B-001 product header (sig 0x7052) at the SDK's 0x38000; that structure does
 * not exist here, so it would have no-opped forever.
 *
 * FAIL-SAFE: validates the header BEFORE writing. If it does not match we write
 * nothing and do NOT reset — a benign no-op, never a corrupt-the-wrong-bytes brick.
 */
#define IMAGE_ADDR    0x5000  /* physical; == Telesto record 0x40060000 */
#define VALIDFLAG_OFF 2
/* IMG header at IMAGE_ADDR: sig 0x7051 + validflag 0xAA at +2. */
#define IMG_SIG0      0x70
#define IMG_SIG1      0x51
#define IMG_VALID     0xAA

/*
 * SPI-flash pins (FLASH_PORT + FLASH_EN/CLK/DO/DI_PIN, plus FLASH_PWR1/2_PIN on the ring)
 * live in board_config.h — read out of the ring's own factory firmware (2026-08-10), and
 * remapped to the kit's own AT25XE021A under TARGET_KIT. On the ring P0_4/P0_3 are the
 * flash power enables (FLASH_HAS_PWR_PINS); on the kit those pins are the SPI CLK/MISO, so
 * there is nothing to power-enable there.
 */
#define FLASH_CHIP_SIZE (256 * 1024)

/* Block-protect bits in the flash status register (SR1). If ANY are set the
 * failsafe region and the image slot are read-only, and every recovery write
 * (enter_failsafe, cfw_ctrl_write, bootlog_clear) fails silently. */
#define FLASH_SR_BP_MASK  (SPI_FLASH_SR_BP0 | SPI_FLASH_SR_BP1 | SPI_FLASH_SR_BP2 | \
                           SPI_FLASH_SR_BP3 | SPI_FLASH_SR_BP4)

/* Failsafe boot-attempt log — physical == Telesto record 0x40060002. The ring's
 * failsafe marks one slot here on every boot and, after 4 uncleared attempts,
 * refuses the primary image and stays in recovery (FUN_07fc3914). 4 KB-aligned,
 * so a single sector erase clears it. */
#define BOOTLOG_ADDR  0x1F000

static const spi_cfg_t flash_spi_cfg = {
    .spi_ms      = SPI_MS_MODE_MASTER,
    .spi_cp      = SPI_CP_MODE_0,
    .spi_speed   = SPI_SPEED_MODE_4MHz,
    .spi_wsz     = SPI_MODE_8BIT,
    .spi_cs      = SPI_CS_0,
    .cs_pad.port = FLASH_PORT,
    .cs_pad.pin  = FLASH_EN_PIN,
    .spi_capture = SPI_MASTER_EDGE_CAPTURE,
};
static const spi_flash_cfg_t flash_dev_cfg = { .chip_size = FLASH_CHIP_SIZE };

/* Bring the SPI flash up before a read/program. The featureless app never touches
 * flash except during recovery, so we init on demand instead of at boot (no idle
 * power cost). Must be paired with flash_off() on any path that does NOT reset —
 * otherwise P0_1 stays an SPI chip select and the button stops working. */
static void flash_on(void)
{
    /* Power the flash first (ring only — FLASH_HAS_PWR_PINS), then let it settle before
     * any SPI. On the kit those pins (P0_4/P0_3) ARE the SPI CLK/MISO, so driving them as
     * GPIO power-enables would break the bus — board_config.h omits FLASH_HAS_PWR_PINS. */
#ifdef FLASH_HAS_PWR_PINS
    GPIO_ConfigurePin(FLASH_PORT, FLASH_PWR1_PIN, OUTPUT, PID_GPIO, true);
    GPIO_ConfigurePin(FLASH_PORT, FLASH_PWR2_PIN, OUTPUT, PID_GPIO, true);
    arch_asm_delay_us(200);
#endif
    GPIO_ConfigurePin(FLASH_PORT, FLASH_EN_PIN,  OUTPUT, PID_SPI_EN,  true);
    GPIO_ConfigurePin(FLASH_PORT, FLASH_CLK_PIN, OUTPUT, PID_SPI_CLK, false);
    GPIO_ConfigurePin(FLASH_PORT, FLASH_DO_PIN,  OUTPUT, PID_SPI_DO,  false);
    GPIO_ConfigurePin(FLASH_PORT, FLASH_DI_PIN,  INPUT,  PID_SPI_DI,  false);
    spi_flash_configure_env(&flash_dev_cfg);
    spi_initialize(&flash_spi_cfg);
    /* Wake the flash from deep power-down. The factory app parks it there after
     * every access (0xB9) and wakes it first (0xAB + ~30 us) — confirmed by tracing
     * the stock app on the DA14535 kit (FUN_07fc3800 sends 0xAB, FUN_07fc3870 sends
     * 0xB9). A Telesto PROGRAM+reset is a *software* reset that does NOT power-cycle
     * the chip, so the CFW can inherit a powered-down flash and read only garbage.
     * The SDK's release skips the tRES delay unless UDPD is set, so add it here. */
    spi_flash_release_from_power_down();
    arch_asm_delay_us(30);
    /* Conditional block-protection clear. On the kit's AT25XE021A the status
     * register reads 0 and this is a no-op; if the ring's part powers up with
     * block-protect bits set (a protected part read 0x1C this session) the
     * failsafe region and image slot are read-only and every recovery write
     * fails silently. Clearing costs one reversible WRSR(0) — no OTP lock bit,
     * no address so no opcode overflow — and only when actually protected, so an
     * already-open flash is never written. WRSR needs its own WREN (unlike the
     * erase/program helpers, which send it internally). */
    if (spi_flash_read_status_reg() & FLASH_SR_BP_MASK) {
        spi_flash_set_write_enable();
        spi_flash_write_status_reg(0x00);
    }
}

/* Release the flash pins. With the real pinout the button (P0_1) and the flash CS
 * (P0_9) are separate, so there is nothing to hand back — this only deasserts CS.
 *
 * ponytail: the ring's own firmware also parks the flash in deep power-down here
 * (cmd 0xB9) and wakes it with 0xAB + 30 us in flash_on(). We do not, because the
 * CFW touches flash only during recovery and the extra idle current is irrelevant
 * next to always-on BLE. Add it if a power budget ever says otherwise. */
static void flash_off(void)
{
    GPIO_ConfigurePin(FLASH_PORT, FLASH_EN_PIN, OUTPUT, PID_GPIO, true);
}

/*
 * Clear the failsafe boot-attempt log — the CFW's half of the anti-brick contract.
 *
 * The failsafe stamps a slot at BOOTLOG_ADDR on every boot and, after 4 uncleared
 * attempts, refuses the primary image and stays in recovery (FUN_07fc3914). The
 * stock app clears the log once it is up and stable (FUN_07fc510c); a featureless
 * CFW that does not do the same self-bricks into the failsafe after 4 boots.
 *
 * Reaching app_on_init means ROM boot, failsafe image validation, SDK init and the
 * BLE GAP reset all succeeded — a strong "booted OK" signal — so we stamp the
 * just-cleared contract here: erase the sector (=> all 0xFF), then write the
 * 0xDEADDEAD magic. Confirmed clean state on the ring is AD DE AD DE + 32x0xFF
 * (BLE dump, 2026-08-10); the 32 trailing 0xFF come free from the erase.
 *
 * ponytail: erases BOOTLOG_ADDR on every healthy boot — the failsafe re-marks a
 * slot each boot so the log is never already clean, and this is the same region
 * the stock rewrites on its stable callback (same wear). No skip-if-clean check.
 *
 * Runs on both boards: the ring's flash on TARGET_KIT undefined, the kit's own
 * AT25XE021A on TARGET_KIT (a real flash there too, so the SPI ops complete).
 */
static void bootlog_clear(void)
{
    static const uint8_t magic[4] = { 0xAD, 0xDE, 0xAD, 0xDE };  /* 0xDEADDEAD LE */

    flash_on();
    spi_flash_block_erase(BOOTLOG_ADDR, SPI_FLASH_OP_SE);
    spi_flash_page_program((uint8_t *)magic, BOOTLOG_ADDR, sizeof magic);
    flash_off();
}

void enter_failsafe(void)
{
    uint8_t h[3];  /* sig[2] + validflag */
    uint32_t n;

    flash_on();
    if (spi_flash_read_data(h, IMAGE_ADDR, sizeof h, &n) < 0 || n < sizeof h ||
        h[0] != IMG_SIG0 || h[1] != IMG_SIG1 || h[2] != IMG_VALID) {
        flash_off();   /* nothing valid there -> benign no-op, give the button back */
        return;
    }

    uint8_t zero = 0x00;   /* 0xAA -> 0x00 (bit-clear, no erase needed) */
    spi_flash_page_program(&zero, IMAGE_ADDR + VALIDFLAG_OFF, 1);
    platform_reset(0);
}

/* The GATT control-point handler (write 0x00 -> enter_failsafe) now lives in cfw_ctrl.c,
 * gated by WITH_CTRL_POINT. enter_failsafe() stays here because the 5-click gesture below
 * calls it too. */

static void click_reset(void)   /* no new click within CLICK_WINDOW => start over */
{
    click_timer = EASY_TIMER_INVALID_TIMER;
    click_run = 0;
}

static void on_wakeup(void)
{
    if (wait_release) {                 /* this wake is the release */
        button_rearm();                 /* pin is up now -> arm for the next press */
        return;
    }

    /* one click */
    click_n++;
    if (++click_run >= CLICKS_TO_FAILSAFE) {
        click_run = 0;
        enter_failsafe();               /* 5 fast taps => recovery (resets; no-op is benign) */
    } else {
        if (click_timer != EASY_TIMER_INVALID_TIMER) app_easy_timer_cancel(click_timer);
        click_timer = app_easy_timer(CLICK_WINDOW, click_reset);  // run expires on a gap
    }
    adv_update(click_n);                /* advertising is continuous -> update the counter */
    button_rearm();                     /* still held -> wait for release; already up -> next press */
}

void app_on_init(void)
{
    default_app_on_init();
    /* Button pull-up now lives in set_pad_functions() (user_periph_setup.c) so it is
     * re-applied on every wake from extended sleep — app_on_init runs only once, which
     * is why a boot-only pull-up let the click freeze after the first press. */
    /* Clear the failsafe boot log once boot is stable, so the CFW does not self-brick
     * into recovery after 4 boots (the anti-brick contract). Talks to the ring's flash,
     * or the kit's own AT25XE021A under TARGET_KIT — a real flash on both. */
    bootlog_clear();
    wkupct_register_callback(on_wakeup);
    button_rearm();                     /* pin idle-high at boot -> arms for the first press */
    /* Bonus recovery net (see POR_HOLD_TICKS): holding the button ~2.5 s POR-resets the
     * chip from any RUNNING state, e.g. if the app ever hangs. Active-low. */
    GPIO_EnablePorPin(BTN_PORT, BTN_PIN, GPIO_POR_PIN_POLARITY_LOW, POR_HOLD_TICKS);
}
