/* user_config.h — user configuration (from the Dialog SDK template). The SDK reads
 * these static const structs from every TU that includes it (app.c and friends). */

#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

#include "app_user_config.h"
#include "arch_api.h"
#include "app_default_handlers.h"
#include "app_adv_data.h"
#include "co_bt.h"

/* Addressing/privacy: no privacy, public BDA. (Other options: APP_CFG_ADDR_STATIC,
 * APP_CFG_HOST_PRIV_RPA/NRPA, APP_CFG_CNTL_PRIV_RPA_PUB/RAND.) */
#define USER_CFG_ADDRESS_MODE       APP_CFG_ADDR_PUB
#define USER_CFG_CNTL_PRIV_MODE     APP_CFG_CNTL_PRIV_MODE_NETWORK

/* Default sleep mode (ARCH_SLEEP_OFF / ARCH_EXT_SLEEP_ON / ARCH_EXT_SLEEP_OTP_COPY_ON). */
static const sleep_state_t app_default_sleep_mode = ARCH_EXT_SLEEP_ON;

/*
 * Advertising configuration
 */
static const struct advertise_configuration user_adv_conf = {
    .addr_src = APP_CFG_ADDR_SRC(USER_CFG_ADDRESS_MODE),

    // BURST experiment: FAST interval so the phone catches the short post-click burst in
    // well under a second. Power is fine because the burst lasts only BURST_TU and the
    // device is SILENT (asleep) between bursts.
    .intv_min = MS_TO_BLESLOTS(40),                    // 40ms
    .intv_max = MS_TO_BLESLOTS(80),                    // 80ms

    .channel_map = ADV_ALL_CHNLS_EN,                   // advertise on 37+38+39
    .mode = GAP_GEN_DISCOVERABLE,
    .adv_filt_policy = ADV_ALLOW_SCAN_ANY_CON_ANY,     // allow scan + connect from anyone

    .peer_addr = {0x1, 0x2, 0x3, 0x4, 0x5, 0x6},       // unused (undirected advertising)
    .peer_addr_type = 0,
};

/* Static advertising/scan-response payload: none — user_app.c rebuilds the adv data
 * (counter + name) at runtime. For ADV_IND the user data budget is 28 bytes: the ROM
 * prepends the 3-byte Flags AD structure on its own. */
#define USER_ADVERTISE_DATA                   ""
#define USER_ADVERTISE_DATA_LEN               (sizeof(USER_ADVERTISE_DATA)-1)
#define USER_ADVERTISE_SCAN_RESPONSE_DATA     ""
#define USER_ADVERTISE_SCAN_RESPONSE_DATA_LEN (sizeof(USER_ADVERTISE_SCAN_RESPONSE_DATA)-1)

/* Device name — also copied into the advertisement by user_app.c (build_adv). */
#define USER_DEVICE_NAME        "Pebble Index CFW"
#define USER_DEVICE_NAME_LEN    (sizeof(USER_DEVICE_NAME)-1)

/*
 * GAPM configuration
 */
static const struct gapm_configuration user_gapm_conf = {
    .role = GAP_ROLE_PERIPHERAL,

    /// Maximal MTU. 23 for Legacy Pairing, 65 for Secure Connection, more if required
    .max_mtu = 23,

    .addr_type = APP_CFG_ADDR_TYPE(USER_CFG_ADDRESS_MODE),
    /// Duration before regenerating the random private address when privacy is enabled
    .renew_dur = 15000,    // 15000 * 10ms = 150s is the minimum value

    /// Private static address; the {0} null address means auto-generate
    .addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},

    /// Device IRK for resolvable random BD address generation (LSB first)
    .irk = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f},

    /// Attribute database configuration (@see enum gapm_att_cfg_flag)
    .att_cfg = GAPM_MASK_ATT_SVC_CHG_EN,

    /// GAP/GATT service start handles (0 = auto)
    .gap_start_hdl = 0,
    .gatt_start_hdl = 0,

    /// Data packet length extension (4.2): MPS / connInitialMaxTxOctets / connInitialMaxTxTime
    .max_mps = 0,
    .max_txoctets = 0,
    .max_txtime = 0,
};

/*
 * Parameter update configuration
 */
static const struct connection_param_configuration user_connection_param_conf = {
    /// Connection interval min/max in ble double slots (1.25ms each)
    .intv_min = MS_TO_DOUBLESLOTS(100),
    .intv_max = MS_TO_DOUBLESLOTS(200),

    /// Latency in connection events
    .latency = 0,

    /// Supervision timeout in timer units (10ms each)
    .time_out = MS_TO_TIMERUNITS(1250),

    /// Connection event duration min/max in ble double slots
    .ce_len_min = MS_TO_DOUBLESLOTS(0),
    .ce_len_max = MS_TO_DOUBLESLOTS(0),
};

/*
 * Default handlers configuration (applies only for app_default_handlers.c)
 */
static const struct default_handlers_configuration  user_default_hnd_conf = {
    // BURST experiment: advertise WITH_TIMEOUT (not DEF_ADV_FOREVER) so each burst
    // auto-stops and the device idles/sleeps. The boot burst uses advertise_period;
    // per-click bursts use BURST_TU (user_app.c). Keep the two in sync.
    .adv_scenario = DEF_ADV_WITH_TIMEOUT,

    // Boot burst length (timer units, 10 ms each). Matches BURST_TU in user_app.c.
    .advertise_period = MS_TO_TIMERUNITS(3000),

    // Never initiate a security request (security is off anyway).
    .security_request_scenario = DEF_SEC_REQ_NEVER
};

/*
 * Central configuration — not used by this peripheral-role app, but referenced
 * unconditionally by SDK app.c, so it must exist. Omitted fields zero-init.
 */
static const struct central_configuration user_central_conf = {
    .code = GAPM_CONNECTION_DIRECT,
    .addr_src = APP_CFG_ADDR_SRC(USER_CFG_ADDRESS_MODE),
    .scan_interval = 0x180,
    .scan_window = 0x160,
    .con_intv_min = 100,
    .con_intv_max = 100,
    .con_latency = 0,
    .superv_to = 0x1F4,
    .ce_len_min = 0,
    .ce_len_max = 0x5,
};

/*
 * Security configuration — unused (CFG_APP_SECURITY off). These are the SDK template
 * defaults; the USER_CFG_FEAT_* per-field overrides it offered are defined nowhere.
 */
static const struct security_configuration user_security_conf = {
    .iocap          = GAP_IO_CAP_NO_INPUT_NO_OUTPUT,
    .oob            = GAP_OOB_AUTH_DATA_NOT_PRESENT,
    .auth           = GAP_AUTH_NONE,
    .key_size       = KEY_LEN,
    .ikey_dist      = GAP_KDIST_NONE,
    .rkey_dist      = GAP_KDIST_ENCKEY,
    .sec_req        = GAP_NO_SEC,
};

#endif // _USER_CONFIG_H_
