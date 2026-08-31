/*
 * Featureless Pebble Index CFW — this file is the whole app.
 *
 * Each button press bumps a counter in the advertisement; the phone reads "counter
 * changed" as one click (it matches on dev company id 0xFFFF + the fixed address).
 * Model: BURST-FROM-SLEEP — silent when idle (extended sleep, radio off, the wkupct
 * is the only wake source); each click fires a short, FAST advertising burst carrying
 * the new counter, then the device idles again.
 *
 * Continuous advertising (~1-2 s) was reliable and lasted years because cell
 * self-discharge dominated. Earlier burst, hibernation and extended-sleep attempts
 * were flaky in the SDK/ROM sleep<->advertising state machine; this burst model is
 * therefore validated on the kit first.
 *
 * Recovery nets: 5 fast clicks -> failsafe bootloader; hold ~2.5 s -> hardware POR
 * reset; GATT Control Point write 0x00 -> failsafe (button-independent, cfw_ctrl.c).
 */
#include <rwip_config.h>
#include <board_config.h>   /* BTN_PIN/PORT + FLASH_* pins — ring vs kit (TARGET_KIT) */
#include <user_app.h>
#include <led.h>
#include <app_easy_gap.h>
#include <app_easy_timer.h>
#include <app_default_handlers.h>   /* default_app_on_set_dev_config_complete */
#include <gapm_task.h>              /* struct gapm_start_advertise_cmd */
#include <wkupct_quadec.h>
#include <gpio.h>
#include <spi.h>
#include <spi_flash.h>
#include <arch.h>
#include <string.h>

/*
 * Button: BTN_PIN/BTN_PORT come from board_config.h — the one source of truth for the
 * per-board pinout. The wkupct is active-low, 1 event, 20 ms debounce. Button and
 * flash CS are separate pins on both boards, so the release-triggered gesture is a
 * conservative choice, not a necessity.
 *
 * wkupct is one-shot; re-arm after every edge, for the edge OPPOSITE the pin's
 * CURRENT level, read live — never from a remembered flag. A static "wait for
 * release" flag desyncs from the pin and freezes the counter: after counting a press,
 * adv_update() takes a few ms; a fast release during that window passes before we
 * re-arm, so arming for the release edge leaves us waiting for a HIGH that already
 * happened — the next press (falling edge) is then ignored and clicks stop
 * registering until reboot. Reading the level at arm time closes that race: if the
 * button is already up we arm for the next press instead. armed_for_release records
 * which edge was armed for the count logic.
 */
static bool armed_for_release;

static void button_rearm(void)
{
    bool pressed = GPIO_GetPinStatus(BTN_PORT, BTN_PIN) == 0; /* active-low */
    wkupct_enable_irq(WKUPCT_PIN_SELECT(BTN_PORT, BTN_PIN),
                      WKUPCT_PIN_POLARITY(BTN_PORT, BTN_PIN,
                          pressed ? WKUPCT_PIN_POLARITY_HIGH : WKUPCT_PIN_POLARITY_LOW),
                      1, 20);
    armed_for_release = pressed;
}

/*
 * Advertisement = manufacturer-specific counter + the device name, rebuilt in full on
 * every click. app_easy_gap_update_adv_data() REPLACES the adv data and scan response
 * wholesale (SDK app.c:543-562), and the SDK only inserts the device name when
 * advertising STARTS (app.c:387-395) — pushing just the 5-byte counter blob dropped
 * the name on the first click (still findable by address, gone from name-based
 * scans). So we emit both AD structures together every time.
 */
#define ADV_MFR_LEN  5   /* len + type + company(2) + counter */
#define ADV_NAME_LEN (2 + USER_DEVICE_NAME_LEN)

_Static_assert(ADV_MFR_LEN + ADV_NAME_LEN <= ADV_DATA_LEN,
               "USER_DEVICE_NAME too long: advertisement would overflow");

static uint8_t build_adv(uint8_t *adv, uint8_t counter)
{
    adv[0] = 0x04;              /* length of this AD structure */
    adv[1] = 0xFF;              /* manufacturer specific data */
    adv[2] = 0xFF;              /* company id 0xFFFF (dev/unassigned), LSB */
    adv[3] = 0xFF;              /*                                    MSB */
    adv[4] = counter;           /* the click counter the phone watches */
    adv[5] = USER_DEVICE_NAME_LEN + 1;
    adv[6] = GAP_AD_TYPE_COMPLETE_NAME;
    memcpy(&adv[7], USER_DEVICE_NAME, USER_DEVICE_NAME_LEN);
    return ADV_MFR_LEN + ADV_NAME_LEN;
}

/* BURST_TU: advertise this long after a click, then stop (timer units, 10 ms each). */
#define BURST_TU  MS_TO_TIMERUNITS(3000)
static bool advertising;

static void advertise_click(uint8_t counter)
{
    if (advertising) {
        uint8_t adv[ADV_MFR_LEN + ADV_NAME_LEN];
        app_easy_gap_update_adv_data(adv, build_adv(adv, counter), NULL, 0);
        return;
    }

    /* Set the active command before starting, so the first packet is complete. */
    struct gapm_start_advertise_cmd *cmd = app_easy_gap_undirected_advertise_get_active();
    cmd->info.host.adv_data_len = build_adv(cmd->info.host.adv_data, counter);
    app_easy_gap_undirected_advertise_with_timeout_start(BURST_TU, NULL);
    advertising = true;
}

/* Burst ended (timeout) -> go idle. The SDK sets extended sleep here; the device
 * sleeps until the next button press. */
void user_on_adv_undirect_complete(uint8_t status)
{
    (void)status;
    advertising = false;
    /* Never sleep with the LED lit: the pad latch holds the pin high through extended
     * sleep, so a stray channel would burn ~3 mA until the next click. The click blink
     * is far shorter than the burst, so this only ever matters for a long pattern. */
    led_off();
    arch_set_sleep_mode(app_default_sleep_mode);
}

/* Boot: the SDK's config-complete starts the first (timeout) burst — track it so a
 * click during that boot burst updates the counter instead of starting a second one. */
void user_on_set_dev_config_complete(void)
{
    default_app_on_set_dev_config_complete();
    advertising = true;
}

/*
 * Recovery gesture: 5 clicks in quick succession (each gap under CLICK_WINDOW) drop
 * into the failsafe bootloader — the boot then sees an invalid image and enters the
 * failsafe (see ../../failsafe-recovery-test). enter_failsafe() fires SYNCHRONOUSLY
 * on the 5th click, no timer in the trigger path: a click is a wkupct edge that
 * reliably wakes the system (the counter climbs cleanly). It replaced a
 * press-and-hold whose detection timer never fired — proven dead on hardware across
 * two flashes.
 *
 * The gap timer that RESETS a partial run (click_reset, an app_easy_timer callback)
 * was once silently dead too: clicks 3 s apart (2x CLICK_WINDOW) still accumulated to
 * 5 and tripped the failsafe on the ring. Root cause was NOT extended sleep:
 * EXCLUDE_DLG_TIMER=1 dropped app_timer_api_process_handler from
 * app_process_handlers (app_entry_point.c), so every app_easy_timer expiry reached
 * TASK_APP with no handler and no callback ever ran. EXCLUDE_DLG_TIMER=0
 * (user_modules_config.h) restores it — the dead press-and-hold was likely the same
 * bug, misread as "the timer fires late in sleep".
 */
#define CLICKS_TO_FAILSAFE 5
#define CLICK_WINDOW 150   /* 1.5 s in 10 ms units: a longer gap restarts the count */

/* Keep the burst longer than the window, so the gap timer expires during the burst
 * (device awake), never inside extended sleep. */
_Static_assert(BURST_TU > CLICK_WINDOW, "BURST_TU must exceed CLICK_WINDOW");

static timer_hnd click_timer = EASY_TIMER_INVALID_TIMER;
static uint8_t fast_clicks;
static uint8_t click_count; /* advertised counter */

/*
 * Every click flashes one of the seven colours the three channels can make. It is the
 * cheapest possible exercise of the LED: over a handful of clicks each channel and
 * each combination gets driven, with no tooling and nothing to read — you just look.
 * It also settles, on a real ring, which channel is which colour, which the firmware
 * itself never says.
 *
 * ponytail: xorshift32 from a fixed seed, so the colour sequence repeats every boot.
 *           That is a feature while testing (reproducible) and nobody depends on it
 *           being unpredictable. Swap in the stack's RNG if that ever changes.
 */
/* One step at its longest: 31 x 50 ms = 1.55 s, the most the ring's own format
 * expresses without a second step. Long enough to read the colour comfortably.
 *
 * A late click IS cut short, and that is fine: advertise_click() does not restart
 * BURST_TU while a burst is already running, so a click 2.5 s into a 3 s burst gets
 * ~0.5 s of light before the burst-end led_off(). The assert below only guarantees
 * the blink fits a burst it starts. */
#define BLINK_UNITS  LED_STEP_DUR_MASK

_Static_assert(MS_TO_TIMERUNITS(BLINK_UNITS * LED_UNIT_MS) <= BURST_TU,
               "a blink that starts a burst must fit in it, or led_off() always truncates");

static uint8_t random_colour(void)
{
    static uint32_t seed = 0x2E1D5A3B;
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return (uint8_t)(seed % 7) + 1;   /* 1..7 — never 0, which would be "dark" */
}

static void blink_random_colour(void)
{
    const uint8_t pattern[] = { LED_STEP(random_colour(), BLINK_UNITS), 0x00 };
    led_play(pattern, sizeof pattern);
}

/* POR hold: por_time x 4096 x RC32K period ~= por_time x 128 ms (~2.5 s). */
#define POR_HOLD_TICKS 20

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

/* SPI-flash pinout lives in board_config.h — read out of the ring's own factory
 * firmware (2026-08-10), remapped to the kit's own AT25XE021A under TARGET_KIT. */
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
 * power cost). Pair with flash_off() on any path that does NOT reset. */
static void flash_on(void)
{
    /* Power the flash first (ring only — FLASH_HAS_PWR_PINS), then let it settle
     * before any SPI. On the kit those pins (P0_4/P0_3) ARE the SPI CLK/MISO, so
     * board_config.h omits FLASH_HAS_PWR_PINS there. */
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
     * block-protect bits set (a protected part read 0x1C this session) every
     * recovery write fails silently. Clearing costs one reversible WRSR(0) — no OTP
     * lock bit, no address so no opcode overflow — and only when actually
     * protected, so an already-open flash is never written. WRSR needs its own WREN
     * (unlike the erase/program helpers, which send it internally). */
    if (spi_flash_read_status_reg() & FLASH_SR_BP_MASK) {
        spi_flash_set_write_enable();
        spi_flash_write_status_reg(0x00);
    }
}

/* Release the flash pins. Button and flash CS are separate pins on both boards (see
 * board_config.h), so there is nothing to hand back — this only deasserts CS.
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

static void click_reset(void)   /* no new click within CLICK_WINDOW => start over */
{
    click_timer = EASY_TIMER_INVALID_TIMER;
    fast_clicks = 0;
}

static void on_wakeup(void)
{
    if (armed_for_release) {            /* this wake is the release */
        button_rearm();                 /* pin is up now -> arm for the next press */
        return;
    }

    /* one click */
    click_count++;
    blink_random_colour();
    if (++fast_clicks >= CLICKS_TO_FAILSAFE) {
        fast_clicks = 0;
        enter_failsafe();               /* 5 fast taps => recovery (resets; no-op is benign) */
    } else {
        if (click_timer != EASY_TIMER_INVALID_TIMER) app_easy_timer_cancel(click_timer);
        click_timer = app_easy_timer(CLICK_WINDOW, click_reset);  // run expires on a gap
    }
    advertise_click(click_count);       /* refresh active burst or start a new one */
    button_rearm();                     /* still held -> wait for release; already up -> next press */
}

void app_on_init(void)
{
    default_app_on_init();
    /* set_pad_functions() reapplies the pull-up after every extended-sleep wake;
     * doing it only here froze clicks after the first press. */
    /* Satisfy the anti-brick contract once boot is stable; TARGET_KIT uses its
     * AT25XE021A, while the ring uses its mapped flash. */
    bootlog_clear();
    wkupct_register_callback(on_wakeup);
    button_rearm();                     /* pin idle-high at boot -> arms for the first press */
    /* Bonus recovery net: holding the button ~POR_HOLD_TICKS (~2.5 s) POR-resets the
     * chip from any RUNNING state, e.g. if the app ever hangs. Active-low. A tap
     * stays a click; 5 fast taps stay the failsafe. */
    GPIO_EnablePorPin(BTN_PORT, BTN_PIN, GPIO_POR_PIN_POLARITY_LOW, POR_HOLD_TICKS);
}
