/*
 * Pebble Index CFW — the gestures, the beacon and the recovery nets. The LED player,
 * the microphone, the codec and the clip transfer live in their own files.
 *
 * Each button press bumps a counter in the advertisement (dev company id 0xFFFF,
 * payload[0]), and the phone accumulates the DIFFERENCE between successive readings
 * rather than counting one click per change. That is why a burst that never reaches
 * the air is not a lost click: the counter kept climbing, so the next advertisement
 * carries the higher value and the phone adds the whole jump.
 *
 * The contract ends where the counter stops climbing: it is a uint8_t in RAM, so a
 * reboot restarts it at zero and a difference taken across that boundary is nonsense —
 * 200 to 0 reads as 56 clicks that never happened. Reboots are not hypothetical here
 * (the POR gesture, the NMI, coming back from the failsafe), and nothing on either side
 * detects one. Living with it beats the retained counter it would take to fix.
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
 * Gestures: a click bumps the counter; a hold records audio for as long as it lasts, up
 * to the 6.1 s the RAM buffer holds (mic.c), and the phone pulls the clip over GATT
 * (clip_tx.c).
 *
 * Recovery nets: 5 fast clicks -> failsafe bootloader; a hold that healthy firmware
 * never acknowledges -> hardware POR reset at ~5 s (por_arm, and hold_detected is what
 * disarms it); GATT Control Point write 0x00 -> failsafe (button-independent,
 * cfw_ctrl.c).
 */
#include <rwip_config.h>
#include <board_config.h>   /* BTN_PIN/PORT + FLASH_* pins — ring vs kit (TARGET_KIT) */
#include <user_app.h>
#include <led.h>
#include <mic.h>
#include <clip_tx.h>
#include <user_periph_setup.h>   /* por_arm / por_disarm */
#include <app_easy_gap.h>
#include <app.h>            /* app_env[].connection_active */
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
#include <arch_wdg.h>       /* wdg_reload around the boot-log erase */
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
#define ADV_MFR_LEN  9   /* len + type + company(2) + counter + reserved(2) + clip samples(2) */
#define ADV_NAME_LEN (2 + USER_DEVICE_NAME_LEN)

/* Minus 3: the stack reserves the Flags AD for itself, so the host buffer this payload
 * lands in (gapm_adv_host.adv_data, filled by stage_adv) is ADV_DATA_LEN - 3 bytes.
 * Bounding by the full 31 would let 29-31 bytes compile clean and overflow it. */
_Static_assert(ADV_MFR_LEN + ADV_NAME_LEN <= ADV_DATA_LEN - 3,
               "advertisement too long for the host buffer (ADV_DATA_LEN minus Flags)");

/* Bytes 5-6 of the AD structure once carried the microphone's peak-to-peak from a 4 ms
 * ADC burst taken in the button ISR. That burst is gone (see handle_click: the SDK
 * polls the same ADC from task context), but the bytes stay, zeroed, so the clip count
 * keeps its offset for a phone that already parses it. Free for the next field. */

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
    adv[5] = 0;                 /* reserved (was microphone peak-to-peak), LSB */
    adv[6] = 0;                 /*                                        MSB */
    adv[7] = (uint8_t)clip_samples;        /* samples in the last clip, LSB */
    adv[8] = (uint8_t)(clip_samples >> 8); /*                           MSB */
    adv[9] = USER_DEVICE_NAME_LEN + 1;
    adv[10] = GAP_AD_TYPE_COMPLETE_NAME;
    memcpy(&adv[11], USER_DEVICE_NAME, USER_DEVICE_NAME_LEN);
    return ADV_MFR_LEN + ADV_NAME_LEN;
}

/*
 * Write the payload into the command the SDK will send the next time it starts
 * advertising. app.c keeps one such message in adv_cmd and only lets go of it when an
 * advertising start actually consumes it, so staging is durable: whoever starts next
 * carries this, whether that is a click, the boot, or default_app_on_disconnect.
 */
static void stage_adv(uint8_t counter)
{
    struct gapm_start_advertise_cmd *cmd = app_easy_gap_undirected_advertise_get_active();
    cmd->info.host.adv_data_len = build_adv(cmd->info.host.adv_data, counter);
}

/* BURST_TU: advertise this long after a click, then stop (timer units, 10 ms each).
 * ADV_BURST_MS is shared with the SDK's boot burst (user_config.h), so the two cannot
 * drift apart. */
#define BURST_TU  MS_TO_TIMERUNITS(ADV_BURST_MS)

/*
 * "A burst is on the air right now." advertise_click reads it to choose between
 * refreshing the adv data and starting a burst, and getting it wrong costs a click:
 * refreshing an advertiser that is not running drops that count silently.
 *
 * Set at the two places bursts start; cleared in ONE place,
 * user_on_adv_undirect_complete, once the controller has actually stopped — the far
 * edge, identical for our bursts and the SDK's own (boot, post-disconnect), so the two
 * kinds cannot drift apart. Setting it in config-complete was wrong (with the custom
 * profile present the SDK advertises from db_init_complete, not there); the
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

/* A hold that lands on a running burst waits for the burst to END before recording (see
 * hold_detected): the radio is cancelled, and the capture starts from the burst-end
 * handler, which is the one place that knows the air is quiet. */
static bool capture_pending;
static void capture(void);

/* The advertised counter — beacon state as much as gesture state, and up here because
 * the burst-end handler advertises it after a capture it started. */
static uint8_t click_count;

/* Set while a hold is being served. Declared here because the burst-end handler reads
 * it, and that sits above the gesture code that owns it. volatile because the capture
 * loop polls it (via still_recording) while the release ISR clears it — and this build
 * links with -flto (CMakeLists), so the cross-module call is NO barrier: the call can
 * inline and the read can hoist out of the loop, leaving `n < CLIP_SAMPLES` as the only
 * exit left — every hold would record the full buffer and releasing early would no
 * longer end it. The volatile is the only thing forcing the reload. */
static volatile bool recording;

/*
 * "This press was served as a hold" — which outlives the capture, because the two stop
 * at different moments: the buffer fills after 6.1 s whether or not the finger has
 * moved. Splitting them is what lets `recording` go false the instant the capture ends,
 * so the burst-end handler can sleep normally again instead of holding ARCH_SLEEP_OFF
 * until the button happens to come up. A button stuck down used to mean a ring that
 * never sleeps AND a POR that hold_detected already disarmed — both nets gone at once.
 * Both contexts write it — hold_detected under its guard, the release ISR when it
 * consumes the gesture — and neither needs volatile: every access is a single read or a
 * single store, and no loop polls it. That is the whole difference from `recording`
 * above, which a loop DOES poll and which therefore cannot do without it.
 */
static bool hold_served;

/* .default_operation_adv — every SDK-initiated start funnels through here. */
/*
 * A dropped link mid-transfer used to leave clip_tx believing it was still sending —
 * refusing every later recording and holding its channel lit, with nothing to clear it.
 * The stack tells us; it just was not being asked.
 */
void user_on_disconnect(struct gapc_disconnect_ind const *param)
{
    clip_tx_abort();    /* ends the transfer AND forgets the peer's CCCDs */
    default_app_on_disconnect(param);
}

void user_advertise_operation(void)
{
    advertising = true;
    default_advertise_operation();
}

static void advertise_click(uint8_t counter)
{
    /* While a link is up the ring is not advertising, and neither branch below is safe:
     * refreshing pushes data at a stopped advertiser and the count goes nowhere, while
     * starting asks for a second connectable advertiser with CFG_MAX_CONNECTIONS at 1.
     * Which one would run depends on whether the stack raises an advertising completion
     * on connect, which is not established — so the payload is staged in the command
     * instead and neither is reached. default_app_on_disconnect starts its burst from
     * that same command, so the counter is current the moment the ring is audible again.
     * Reachable in ordinary use: the phone holds the link for seconds to fetch a clip. */
    bool connected = app_env[0].connection_active;

    /* Stage FIRST, on every path: the staged command is durable (see stage_adv) and is
     * what the next start — click, boot or post-disconnect — sends. Staging only on the
     * start path left the update path with an older counter in the command, and a
     * connect-during-burst plus a click-free disconnect then re-advertised it — a uint8
     * regression the phone's delta accumulator reads as ~250 phantom clicks. */
    stage_adv(counter);
    if (connected) {
        return;
    }
    if (advertising) {
        /* Refresh the air from the freshly staged command: one build_adv, both current. */
        struct gapm_start_advertise_cmd *cmd = app_easy_gap_undirected_advertise_get_active();
        app_easy_gap_update_adv_data(cmd->info.host.adv_data, cmd->info.host.adv_data_len,
                                     NULL, 0);
        return;
    }
    advertising = true;
    app_easy_gap_undirected_advertise_with_timeout_start(BURST_TU, NULL);
}

/* Burst ended — by its timeout, or by the cancel a hold sent — so go idle, or record.
 * The SDK sets extended sleep here; the device sleeps until the next button press. */
void user_on_adv_undirect_complete(uint8_t status)
{
    (void)status;
    bool run_capture = false;

    /* The ONE place the flag clears (see its comment). Paired with led_off() under one
     * guard so a click cannot land between them, start a burst, light a blink, and
     * have that blink cancelled and darkened by the led_off() below. */
    GLOBAL_INT_DISABLE();
    advertising = false;
    if (capture_pending) {
        /* A hold cancelled this burst to record in silence. Still a hold — the release
         * clears `recording` — and still ours to record: gapm lives in ROM, so whether a
         * central taking the air also reports this completion is not established, and a
         * capture under a live link would block the host for seconds. */
        capture_pending = false;
        run_capture = recording && !app_env[0].connection_active;
        if (!run_capture) {
            recording = false;      /* nothing to record: released, or a link is up */
        }
    }
    /* Never sleep with a channel lit — the pad latch holds it high through extended
     * sleep, burning ~3 mA until the next click. Two lit channels legitimately outlive
     * a burst and must not be cut here:
     *
     *  - the recording light, when this completion is the cancel a hold asked for: the
     *    capture is about to start (run_capture), and it must not sleep either, since
     *    it needs the peripherals running. Cutting the LED unconditionally is what once
     *    made the recording light go out after BURST_TU — the light was not failing, a
     *    three-second timer was ending.
     *  - a transfer: clicking during a send starts a burst whose end, three seconds
     *    later, would darken it mid-send. Same reason handle_click refuses to blink
     *    over it. */
    if (!recording && !clip_tx_busy()) {
        led_off();
    }
    GLOBAL_INT_RESTORE();
    arch_set_sleep_mode(recording ? ARCH_SLEEP_OFF : app_default_sleep_mode);

    if (run_capture) {
        capture();
    }
}

/*
 * Recovery gesture: 5 clicks in quick succession (each gap under CLICK_WINDOW) drop
 * into the failsafe bootloader — the boot then sees an invalid image and enters the
 * failsafe. enter_failsafe() fires SYNCHRONOUSLY
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

/* See user_app.h. Task context (clip_tx's done branch); stages only, starts nothing —
 * the burst that carries it is whichever one starts next. */
void user_beacon_restage(void)
{
    refresh_clip_count();
    stage_adv(click_count);
}

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
 * Telesto record 0x40060000 lands at physical 0x5000, and a BLE dump of a real ring's
 * flash confirms it byte-for-byte. The boot validates
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

/* SPI-flash pinout lives in board_config.h, remapped to the kit's own AT25XE021A
 * under TARGET_KIT. */
#define FLASH_CHIP_SIZE (256 * 1024)

/* Block-protect bits in the flash status register (SR1). If ANY are set the
 * failsafe region and the image slot are read-only, and every recovery write
 * (enter_failsafe, cfw_ctrl_write, bootlog_clear) fails silently. */
#define FLASH_SR_BP_MASK  (SPI_FLASH_SR_BP0 | SPI_FLASH_SR_BP1 | SPI_FLASH_SR_BP2 | \
                           SPI_FLASH_SR_BP3 | SPI_FLASH_SR_BP4)

/* Failsafe boot-attempt log — physical == Telesto record 0x40060002. The ring's
 * failsafe marks one slot here on every boot and, after 4 uncleared attempts,
 * refuses the primary image and stays in recovery. 4 KB-aligned,
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
    /* Wake the flash from deep power-down. The ring parks it there after every access
     * (0xB9) and wakes it first (0xAB plus about 30 us), which was confirmed on the
     * DA14535 kit. A Telesto PROGRAM+reset is a *software* reset that does NOT power-cycle
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
 * (cmd 0xB9) and wakes it with 0xAB + 30 us in flash_on(). We do not — and the reason
 * is NOT that idle current does not matter. It does: this firmware sleeps with the
 * radio off between clicks, so a flash idling at tens of microamps would dominate a
 * SoC idling at a few. What saves us is accidental. Extended sleep drops the pad
 * configuration, so P0_4/P0_3 fall back to their reset mode and stop powering the chip
 * without anyone asking — the same mechanism that forced led_reapply() and that puts
 * the button pull-up in set_pad_functions(). Which makes flash_on() re-driving those
 * pins on every call load-bearing, not defensive. Send the 0xB9 if the flash ever has
 * to survive a sleep powered. */
static void flash_off(void)
{
    GPIO_ConfigurePin(FLASH_PORT, FLASH_EN_PIN, OUTPUT, PID_GPIO, true);
}

/*
 * Clear the failsafe boot-attempt log — the CFW's half of the anti-brick contract.
 *
 * The failsafe stamps a slot at BOOTLOG_ADDR on every boot and, after 4 uncleared
 * attempts, refuses the primary image and stays in recovery. The stock app clears the
 * log once it is up and stable; a featureless
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
    /* The erase blocks for 50-300 ms inside a watchdog window that periph_init armed and
     * the SDK only reloads after app_on_init returns (arch_system.c), with ble_init and
     * the RCX calibration already spent from it. Reload first: a slow sector must not
     * turn a healthy boot into the NMI path. */
    wdg_reload(WATCHDOG_DEFAULT_PERIOD);
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
 * deliberate hold while staying clear of a tap, which people do in 50-150 ms. 300 ms was
 * the starting point and 500 ms is what the bench settled on — it is a number that can
 * only be judged by feel, so it stays one number in one place. It cannot go to zero — the red can only appear once the firmware
 * KNOWS the press is a hold, and lighting it on the press instead would put a red
 * flicker in front of every click and blur the two gestures again.
 *
 * It must also stay comfortably under the POR's ~5 s (see por_arm) so healthy firmware
 * always disarms first.
 */
#define HOLD_TICKS 50                   /* 500 ms, in 10 ms timer units */

/* The relation the whole gesture rests on: healthy firmware must see the hold and
 * disarm long before the silicon reset lands. The factor of two is not caution for its
 * own sake — the POR runs off an uncalibrated RC oscillator, so its window is a range
 * (see POR_HOLD_MS_NOMINAL). And it is the assert rather than the comment that keeps
 * this true when someone lengthens the hold to fight accidental taps. */
_Static_assert(HOLD_TICKS * 10 * 2 <= POR_HOLD_MS_NOMINAL,
               "HOLD_TICKS too close to the POR: a hold would reset instead of record");

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
    /* Refused while a link is up, not only during a transfer: mic_capture owns the task
     * context for up to 6.1 s, so every GATT message — the phone's fetch included —
     * would sit undispatched under a live connection, and the BLE ISRs preempting the
     * poll loop stretch the measured-unloaded sample clock into pitch-warped audio. */
    recording = held && !clip_tx_busy() && !app_env[0].connection_active;
    hold_served = recording;    /* same condition, so a REFUSED hold still ends as a
                                 * click on release, exactly as before the split */
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

    /* A running burst is stopped before recording, never recorded under. The capture
     * loop's sample clock is its own timing (board_config.h), measured with the radio
     * off; the radio's interrupts preempting it would stretch that clock and warp the
     * pitch. An advertiser is also connectable, so a phone fetching on sight could take
     * a link into a host that will not answer for six seconds. The cancel is a kernel
     * message and the kernel runs in this very task context, so it cannot complete
     * while a blocking loop runs here: the capture is handed to the burst-end handler,
     * where the completion arrives. The SDK's own timeout timer is stopped too, or it
     * fires a second cancel later at nothing.
     *
     * A hold from idle — the common path — has no burst to stop and records at once. */
    if (advertising) {
        capture_pending = true;
        app_easy_gap_advertise_with_timeout_stop();
        app_easy_gap_advertise_stop();
        return;
    }
    capture();
}

/* The recording itself, and the beacon that announces it. Task context, with the air
 * quiet: straight from hold_detected, or from the burst-end handler once the burst a
 * hold cancelled has actually ended. */
static void capture(void)
{
    /* Blocks here for the whole recording. The release interrupt still runs — it is an
     * interrupt — and clearing `recording` is what ends the loop below. Advertising the
     * result therefore belongs here, after the count exists, not in the release path. */
    (void)mic_capture(still_recording);

    /* The capture is over — by release or by a full buffer — so the reasons to stay
     * awake are too. Doing this here rather than in the release path is the whole point
     * of hold_served: a finger that stays down after the buffer fills no longer keeps
     * the radio and the CPU up with it. Idempotent when the release already ran. */
    recording = false;
    arch_set_sleep_mode(app_default_sleep_mode);

    /* The light goes out when RECORDING ends, which is not the same moment the button
     * comes up: the buffer holds a few seconds and fills while a finger is still down.
     * Leaving the release path to do it meant the ring looked like it was still
     * recording long after it had stopped, with no way to tell. Harmless if the release
     * already ran — led_off does nothing when nothing is lit. The POR stays disarmed
     * until the button actually comes up, because a hold should never reset a ring that
     * is plainly alive. */
    led_off();

    /* The advertisement is how the phone learns a clip exists: refresh_clip_count has
     * just read the new sample count, and non-zero is the whole signal. `advertising` is
     * false here — a hold from idle had no burst, a hold on a burst waited for its end —
     * so this takes the start path and the count gets a full BURST_TU on air. */
    refresh_clip_count();
    advertise_click(click_count);
}

/*
 * The click itself — counting, the failsafe run, the beacon. Runs on RELEASE, because
 * a press is ambiguous: a click and a hold are the same event until the button comes
 * back up.
 *
 * ORDER IS LOAD-BEARING: every cancel first, every allocation after. This is the ONE
 * place the timer-slot mechanics live; led_run and click_reset point here.
 *
 * AND ALL OF IT RUNS IN INTERRUPT CONTEXT, calling into the SDK's kernel-message
 * machinery: app_easy_timer allocates a message, so does app_easy_gap_update_adv_data,
 * and so does stage_adv via app_easy_gap_undirected_advertise_get_active. ke_malloc
 * lives in the ROM library (da14535_symbols.lds) with no source to read — but the SDK's
 * own design does the same: app_easy_wakeup() sends a kernel message and is registered
 * right beside the wkupct callback in the vendor examples (app_easy_msg_utils.c), so
 * allocating from this ISR is what the vendor relies on, not a bet of ours.
 *
 * What does NOT belong here is any peripheral the SDK also drives from task context.
 * The GP_ADC did, as a 4 ms level burst per click, until conditionally_run_radio_cals
 * (arch_system.c) turned out to poll that same converter every 2 s while awake: a burst
 * landing inside its poll hands it a microphone sample as the die temperature, or
 * leaves it spinning on a converter this path had just disabled — into the watchdog,
 * the NMI and the failsafe. The microphone is read from task context only (capture),
 * where the two cannot overlap.
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
        if (hold_served) {              /* the hold clock ran out: it was a hold */
            hold_served = false;
            /* Also the capture's stop signal, and the disarm for a hold_detected still
             * sitting in the dispatch window — its refusal below tests this flag. */
            recording = false;
            if (!clip_tx_busy()) {      /* a transfer may own the LED by now: the buffer
                                         * can fill, and the phone start pulling, while
                                         * the finger is still down */
                led_off();
            }
            por_arm();                  /* the net is back before the button is idle */
            button_rearm();                 /* hold_detected advertises once its loop ends */
            return;
        }
        handle_click();                 /* it did not: a click */
        return;
    }

    /* Cancel only, no darkness: a transfer may own the light. A press inside a click's
     * 100 ms flash therefore holds that flash until the release settles the gesture and
     * drives the pads again — cosmetic, and every path out of here ends in a led_set. */
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

    /* Without this the boot burst carries flags and the device name and nothing else:
     * USER_ADVERTISE_DATA is empty and app.c only appends the name, so the manufacturer
     * data that IDENTIFIES a CFW ring first appears on the first click. The phone keys
     * on company 0xFFFF — name and service UUID are explicitly not reliable for this —
     * so the ring spent its first three seconds unrecognisable, which is exactly when
     * you most want to see that the firmware came up: after a reflash, after the POR
     * gesture, on the way back from the failsafe. */
    stage_adv(click_count);

    wkupct_register_callback(on_wakeup);
    button_rearm();                     /* pin idle-high at boot -> arms for the first press */
    /* The POR is armed from set_pad_functions(), not here: periph_init re-runs on every
     * wake, so a path that forgets to re-arm loses the net until the next sleep instead
     * of silently forever. */
}
