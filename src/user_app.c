/*
 * Featureless Pebble Index CFW — this file is the whole app.
 *
 * Each button press bumps a counter in the advertisement (dev company id 0xFFFF,
 * payload[0]), and the phone accumulates the DIFFERENCE between successive readings
 * rather than counting one click per change. That is why a burst that never reaches
 * the air is not a lost click: the counter kept climbing, so the next advertisement
 * carries the higher value and the phone adds the whole jump.
 *
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
#include <mic.h>
#include <clip_tx.h>
#include <user_periph_setup.h>   /* por_arm / por_disarm */
#include <app_easy_gap.h>
#include <app_easy_timer.h>
#include <app_default_handlers.h>   /* default_advertise_operation, default_app_on_init */
#include <gapm_task.h>              /* struct gapm_start_advertise_cmd */
#include <gapc_task.h>              /* struct gapc_disconnect_ind */
#include <wkupct_quadec.h>
#include <gpio.h>
#include <ll.h>             /* GLOBAL_INT_DISABLE / GLOBAL_INT_RESTORE */
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
#define ADV_MFR_LEN  9   /* len + type + company(2) + counter + mic pp(2) + clip samples(2) */
#define ADV_NAME_LEN (2 + USER_DEVICE_NAME_LEN)

_Static_assert(ADV_MFR_LEN + ADV_NAME_LEN <= ADV_DATA_LEN,
               "USER_DEVICE_NAME too long: advertisement would overflow");

/* Peak-to-peak of the last microphone burst, raw ADC counts, measured at the click that
 * started this burst. Appended AFTER the counter, so the phone — which reads payload[0]
 * and ignores the rest — is unaffected. Any BLE scanner shows it. */
static mic_reading_t mic;

/* Samples in the clip the ring is HOLDING — not in the last one recorded. It reads back
 * from mic rather than being remembered here, so that a delivered clip stops being
 * advertised: the phone treats non-zero as "audio you have not taken yet". */
static uint16_t clip_samples;

static void refresh_clip_count(void)
{
    (void)mic_clip(&clip_samples);
}

static uint8_t build_adv(uint8_t *adv, uint8_t counter)
{
    adv[0] = ADV_MFR_LEN - 1;   /* length of this AD structure */
    adv[1] = 0xFF;              /* manufacturer specific data */
    adv[2] = 0xFF;              /* company id 0xFFFF (dev/unassigned), LSB */
    adv[3] = 0xFF;              /*                                    MSB */
    adv[4] = counter;           /* the click counter the phone watches */
    adv[5] = (uint8_t)mic.pp;         /* microphone peak-to-peak, LSB */
    adv[6] = (uint8_t)(mic.pp >> 8);  /*                            MSB */
    adv[7] = (uint8_t)clip_samples;        /* samples in the last clip, LSB */
    adv[8] = (uint8_t)(clip_samples >> 8); /*                           MSB */
    adv[9] = USER_DEVICE_NAME_LEN + 1;
    adv[10] = GAP_AD_TYPE_COMPLETE_NAME;
    memcpy(&adv[11], USER_DEVICE_NAME, USER_DEVICE_NAME_LEN);
    return ADV_MFR_LEN + ADV_NAME_LEN;
}

/* BURST_TU: advertise this long after a click, then stop (timer units, 10 ms each). */
#define BURST_TU  MS_TO_TIMERUNITS(3000)

/*
 * "A burst is on the air right now." advertise_click reads it to choose between
 * refreshing the adv data and starting a burst, and getting it wrong costs a click:
 * refreshing an advertiser that is not running drops that count silently.
 *
 * Set at the two places bursts start; cleared in ONE place,
 * user_on_adv_undirect_complete, once the controller has actually stopped — the far
 * edge, identical for our bursts and the SDK's own (boot, post-disconnect), so the two
 * kinds cannot drift apart. Setting it in config-complete was wrong (with
 * WITH_CTRL_POINT the SDK advertises from db_init_complete, not there); the
 * user_advertise_operation funnel sees every start regardless of which path makes it.
 *
 * Two windows are accepted rather than closed, both self-healing:
 *
 *  - Stop in flight (timeout fired, GAPM cancel not yet complete — up to one adv
 *    interval, 40-80 ms): the flag still reads true, so a click there refreshes a
 *    dying advertiser and its count rides out with the NEXT click — losing nothing,
 *    because the phone adds differences (see the top of this file). Clearing at the
 *    near edge instead was tried and is worse: a click then STARTS a burst whose flag
 *    the old burst's completion promptly wipes, desyncing flag and air for up to 3 s.
 *  - Boot, during the GATT database build: the flag is false with the SDK's start
 *    pending, so a click's own start collides with it. The loser's error lands in
 *    adv_undirect_complete like any completion; the desync heals at the burst's far
 *    edge. Boot-only, under 3 s.
 *
 * ponytail: the BURST_TU stop timer is allocated inside the SDK helper, out of sight —
 *           on an exhausted pool the burst never stops and the radio stays on until a
 *           connection, the failsafe gesture or a reboot. Own the burst timer (plain
 *           advertise_start + a checked app_easy_timer, like led_run's) if that ever
 *           stops being acceptable.
 */
static bool advertising;

/* Set while a hold is being served. Declared here because the burst-end handler reads
 * it, and that sits above the gesture code that owns it. volatile because the capture
 * loop polls it (via still_recording) while the release ISR clears it — and this build
 * links with -flto (CMakeLists), so the cross-module call is NO barrier: the call can
 * inline and the read can hoist out of the loop, leaving `n < CLIP_SAMPLES` as the only
 * exit left — every hold would record the full buffer and releasing early would no
 * longer end it. The volatile is the only thing forcing the reload. */
static volatile bool recording;

/* .default_operation_adv — every SDK-initiated start funnels through here. */
/*
 * A dropped link mid-transfer used to leave clip_tx believing it was still sending —
 * refusing every later recording and holding its channel lit, with nothing to clear it.
 * The stack tells us; it just was not being asked.
 */
void user_on_disconnect(struct gapc_disconnect_ind const *param)
{
    clip_tx_abort();
    default_app_on_disconnect(param);
}

void user_advertise_operation(void)
{
    advertising = true;
    default_advertise_operation();
}

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
    advertising = true;
    app_easy_gap_undirected_advertise_with_timeout_start(BURST_TU, NULL);
}

/* Burst ended (timeout) -> go idle. The SDK sets extended sleep here; the device
 * sleeps until the next button press. */
void user_on_adv_undirect_complete(uint8_t status)
{
    (void)status;
    /* The ONE place the flag clears (see its comment). Paired with led_off() under one
     * guard so a click cannot land between them, start a burst, light a blink, and
     * have that blink cancelled and darkened by the led_off() below. */
    GLOBAL_INT_DISABLE();
    advertising = false;
    /* Never sleep with a channel lit — the pad latch holds it high through extended
     * sleep, burning ~3 mA until the next click. A recording is the one case where a
     * lit channel is deliberate and outlives its burst, and it is also a case that must
     * not sleep at all, since the capture needs the peripherals running. Cutting the
     * LED here unconditionally is what made the recording light go out after BURST_TU:
     * the light was not failing, a three-second timer was ending. */
    if (!recording) {
        led_off();
    }
    GLOBAL_INT_RESTORE();
    arch_set_sleep_mode(recording ? ARCH_SLEEP_OFF : app_default_sleep_mode);
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

/* The LED is the only feedback a sealed ring can give, so it says what the firmware is
 * doing. One channel at a time, never a mix: a mix cannot answer which channel is which
 * colour, and that is the question the LED exists to settle. Meanings live in led.h,
 * because the transfer lights one too. */

/* A click gets a flash, not a light: two units is 100 ms, long enough to read and
 * short enough that it reads as an acknowledgement rather than a state. Recording is
 * the thing that holds a channel lit, and the two should not look alike. */
#define BLINK_UNITS  2

_Static_assert(MS_TO_TIMERUNITS(BLINK_UNITS * LED_UNIT_MS) <= BURST_TU,
               "a blink that starts a burst must fit in it, or led_off() always truncates");

static void blink(uint8_t channel)
{
    const uint8_t pattern[] = { LED_STEP(channel, BLINK_UNITS), 0x00 };
    led_play(pattern, sizeof pattern);
}

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

/* No new click within CLICK_WINDOW => start the count over. Task context, so the click
 * ISR can preempt it. The guard closes the tear between the two writes; what it cannot
 * close is the usual dispatch window before the first one — a click landing there has
 * its count and fresh gap timer undone here, and the orphaned timer can wipe a retry
 * too. Bounded and recoverable: the gesture just takes another run. The timer-slot
 * mechanics, and the cancel-before-allocate order that keeps the stale cancel here
 * harmless, are documented ONCE, at on_wakeup. */
static void click_reset(void)
{
    GLOBAL_INT_DISABLE();
    click_timer = EASY_TIMER_INVALID_TIMER;
    fast_clicks = 0;
    GLOBAL_INT_RESTORE();
}

/*
 * Hold to record — and the same hold is the last-resort reset when the firmware is not
 * answering. por_disarm() below is what separates them: healthy firmware sees the hold,
 * disarms the silicon reset and records; wedged firmware never gets here, the POR keeps
 * counting, and the chip reboots at ~5 s. One gesture, and the right thing happens in
 * both states with nothing for the user to remember.
 *
 * HOLD_TICKS is an ergonomics constant, not a derived one: it has to feel instant on a
 * deliberate hold while staying clear of a tap, which people do in 50-150 ms. 300 ms is
 * the starting point; it can only really be settled by feel on hardware, so it is one
 * number in one place. It cannot go to zero — the red can only appear once the firmware
 * KNOWS the press is a hold, and lighting it on the press instead would put a red
 * flicker in front of every click and blur the two gestures again.
 *
 * It must also stay comfortably under the POR's ~5 s (see por_arm) so healthy firmware
 * always disarms first.
 */
#define HOLD_TICKS 50                   /* 500 ms, in 10 ms timer units */

static timer_hnd hold_timer = EASY_TIMER_INVALID_TIMER;

/* The capture loop's stop signal: the release interrupt clears `recording`. */
static bool still_recording(void)
{
    return recording;
}

static void hold_detected(void)
{
    hold_timer = EASY_TIMER_INVALID_TIMER;

    /* The release can land in the dispatch window before this callback's first line —
     * and it settles the press as a CLICK (recording still false), so recording here
     * would capture with no finger on the button until the buffer filled. The pin
     * answers: up means the click path already ran. Checked and set under one guard so
     * a release cannot slip between the two; one landing just after clears `recording`
     * again and the refusal below turns the stale hold into a no-op. */
    bool held;   /* outside the guard: DISABLE/RESTORE are a brace pair, its own scope */
    GLOBAL_INT_DISABLE();
    held = GPIO_GetPinStatus(BTN_PORT, BTN_PIN) == 0;   /* active-low */
    recording = held && !clip_tx_busy();
    GLOBAL_INT_RESTORE();
    if (!held) {
        return;
    }

    /* Disarmed because the gesture was SEEN, not because a recording succeeded. A
     * legitimate refusal to record must never reboot the device; what the POR tests is
     * whether the firmware responds at all.
     *
     * A release racing past the guard above has already re-armed, so this disarms
     * again and the ring sits net-off with the button up until the next wake re-arms
     * it (which is why the arm lives in set_pad_functions). Chosen over moving the
     * disarm below the refusal, which would close that sliver by breaking the rule
     * this comment states. Same stale pass, same sliver: the reset below also eats a
     * rapid-click run in progress, since the press was consumed as a hold. */
    por_disarm();
    fast_clicks = 0;                    /* a hold is not part of a rapid-click run */

    /* A transfer is reading the very buffer a recording would overwrite. Refuse rather
     * than corrupt what is already on its way out; the POR stays disarmed either way,
     * because the firmware DID see the gesture. (Also the exit for a release that raced
     * the guard above.) */
    if (!recording) {
        return;
    }
    led_hold(LED_RECORD);          /* stays lit: a pattern would end on its own */

    /* Blocks here for the whole recording. The release interrupt still runs — it is an
     * interrupt — and clearing `recording` is what ends the loop below. Advertising the
     * result therefore belongs here, after the count exists, not in the release path. */
    (void)mic_capture(still_recording);

    /* The light goes out when RECORDING ends, which is not the same moment the button
     * comes up: the buffer holds a few seconds and fills while a finger is still down.
     * Leaving the release path to do it meant the ring looked like it was still
     * recording long after it had stopped, with no way to tell. Harmless if the release
     * already ran — led_off does nothing when nothing is lit. The POR stays disarmed
     * until the button actually comes up, because a hold should never reset a ring that
     * is plainly alive. */
    led_off();
    refresh_clip_count();
    advertise_click(click_count);
    /* Step 2 starts the capture chain here. Until then the LED is the whole feature,
     * which is the point: the gesture machine gets proven on its own. */
}

/*
 * The click itself — counting, the failsafe run, the beacon. Runs on RELEASE, because
 * a press is ambiguous: a click and a hold are the same event until the button comes
 * back up.
 *
 * ORDER IS LOAD-BEARING: every cancel first, every allocation after. This is the ONE
 * place the timer-slot mechanics live; led_run and click_reset point here.
 *
 * app_easy_timer handles carry no identity — cancel acts on a bare slot index, a slot
 * is freed BEFORE its callback runs, and allocation reuses the lowest free slot
 * (app_easy_timer.c). So when this ISR preempts a timer callback at dispatch, the
 * handle we still hold for it points at a free slot: cancelling it now is an
 * ASSERT_WARNING no-op, but an allocation made first can take that very slot and be
 * cancelled in its place. That cross-kill is not hypothetical — with the blink leading,
 * the fresh gap timer died in the LED's freed slot, click_reset never ran, and slow
 * clicks accumulated into an unasked-for failsafe.
 */
static void handle_click(void)
{
    click_count++;
    led_cancel();                       /* the LED's cancel, split from its darkness so
                                         * it cannot be "simplified" away as redundant */
    if (++fast_clicks >= CLICKS_TO_FAILSAFE) {
        fast_clicks = 0;
        enter_failsafe();               /* 5 fast taps => recovery (resets; no-op is benign) */
    } else {
        if (click_timer != EASY_TIMER_INVALID_TIMER) app_easy_timer_cancel(click_timer);
        click_timer = app_easy_timer(CLICK_WINDOW, click_reset);  // run expires on a gap
        if (click_timer == EASY_TIMER_INVALID_TIMER) {
            /* Out of timer slots, so nothing will ever expire this run. Leaving
             * fast_clicks armed is the unsafe half of that: clicks would keep
             * accumulating across arbitrary gaps until the fifth dropped the ring into
             * the failsafe. Start the count over instead — a missed gesture is cheap,
             * an unasked-for recovery is not. led_run makes the same call for the same
             * reason.
             *
             * ponytail: under SUSTAINED exhaustion this disables the gesture outright,
             *           because the reset only ever lands on clicks 1-4 (the fifth
             *           allocates nothing). Acceptable while exhaustion stays a
             *           transient: the pool is 10, a press-and-release holds at most a
             *           hold timer, a gap timer, an LED timer and the SDK's adv timer,
             *           and the scheduler drains
             *           cancelled slots between clicks. If it ever stops being
             *           transient, keep the count at CLICKS_TO_FAILSAFE - 1 instead of
             *           special-casing anything earlier. */
            fast_clicks = 0;
        }
    }
    /* Not while a transfer owns the LED: a 100 ms flash would replace the transfer's
     * light and leave it dark for the rest of the send, which says something false. */
    if (!clip_tx_busy()) {
        blink(LED_CLICK);               /* its own cancel is a no-op: led_cancel led */
    }
    mic = mic_read();                   /* ~4 ms; goes out with this click's burst */
    refresh_clip_count();               /* a delivered clip must stop being advertised */
    advertise_click(click_count);       /* refresh active burst or start a new one */
    button_rearm();
}

/*
 * A press decides nothing. It starts the clock that tells a click from a hold, and
 * that is all it may do — doing the click here is what made a hold also count as a
 * click and flash the click colour before the recording colour.
 *
 * Keeping this path short also nearly closes the race the old code lived with. The
 * press handler used to run the whole click, several milliseconds of it, and a release
 * inside that window slipped past the re-arm and was lost. What little window remains
 * is closed outright below: if the button is already back up when we re-arm, the
 * release has happened and no second wake is coming, so the click is settled here
 * instead of dropped. button_rearm() reads the pin live, so armed_for_release says
 * exactly that.
 */
static void on_wakeup(void)
{
    if (armed_for_release) {            /* the release: now we know which gesture it was */
        if (hold_timer != EASY_TIMER_INVALID_TIMER) {
            app_easy_timer_cancel(hold_timer);
            hold_timer = EASY_TIMER_INVALID_TIMER;
        }
        if (recording) {                /* the hold clock ran out: it was a hold */
            recording = false;
            led_off();
            por_arm();                  /* the net is back before the button is idle */
            arch_set_sleep_mode(app_default_sleep_mode);   /* held off while recording */
            button_rearm();                 /* hold_detected advertises once its loop ends */
            return;
        }
        handle_click();                 /* it did not: a click */
        return;
    }

    led_cancel();
    if (hold_timer != EASY_TIMER_INVALID_TIMER) app_easy_timer_cancel(hold_timer);
    hold_timer = app_easy_timer(HOLD_TICKS, hold_detected);
    /* ponytail: unchecked on purpose. With the pool exhausted there is no hold
     *           detection, so the POR stays armed and a long hold becomes the silicon
     *           reset at ~5 s — the right failure for a ring too wedged to run timers.
     *           A tap is still a click via the release path below. */
    button_rearm();

    if (!armed_for_release) {
        /* Already back up — too fast for a hold, and no release wake will arrive. */
        if (hold_timer != EASY_TIMER_INVALID_TIMER) {
            app_easy_timer_cancel(hold_timer);
            hold_timer = EASY_TIMER_INVALID_TIMER;
        }
        handle_click();
    }
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
    /* The POR is armed from set_pad_functions(), not here: periph_init re-runs on every
     * wake, so a path that forgets to re-arm loses the net until the next sleep instead
     * of silently forever. */
}
