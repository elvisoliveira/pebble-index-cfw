/* da1458x_config_advanced.h — advanced compile configuration (from the Dialog SDK template). */

#ifndef _DA1458X_CONFIG_ADVANCED_H_
#define _DA1458X_CONFIG_ADVANCED_H_

#include "da1458x_stack_config.h"

/* Low-power clock: internal RCX20 (no external XTAL32 on the ring). */
#define CFG_LP_CLK              LP_CLK_RCX20

/* Periodic wakeup period (ms): GTL polling / without GTL. */
#define CFG_MAX_SLEEP_DURATION_PERIODIC_WAKEUP_MS                  500  // 0.5s
#define CFG_MAX_SLEEP_DURATION_EXTERNAL_WAKEUP_MS              600000  // 600s

/* No external host processor. */
#undef CFG_EXTERNAL_WAKEUP
#undef CFG_WAKEUP_EXT_PROCESSOR

/* TRNG seeds the C stdlib RNG at init; buffer size trades code size vs start-up time
 * (32/64/.../1024 bytes, undefined = off). */
#define CFG_TRNG (32)

/* No ECDH/secure-connection keys (faster start-up, smaller code). */
#undef CFG_ENABLE_SMP_SECURE

/* Stdlib RNG, not ChaCha20. */
#undef CFG_USE_CHACHA20_RAND

/* Custom heap sizes. */
#define DB_HEAP_SZ              2048
// #define ENV_HEAP_SZ             4928
// #define MSG_HEAP_SZ             3880
// #define NON_RET_HEAP_SZ         2048

/* NVDS defaults: bdaddress (ignored if OTP has one), LP clock drift (ppm), channel
 * assessment timers/thresholds.
 *
 * The address is the SDK's stock one, not ours. Every board whose OTP carries no
 * address falls back to it — the kit may be one — so two such boards in range look like
 * ONE device to a scanner, and the phone's click-delta accumulator would merge them. The
 * ring is expected to carry its own in OTP. */
#define CFG_NVDS_TAG_BD_ADDRESS             {0x03, 0x69, 0x70, 0xCA, 0xEA, 0x80}

#define CFG_NVDS_TAG_LPCLK_DRIFT            DRIFT_500PPM
#define CFG_NVDS_TAG_BLE_CA_TIMER_DUR       2000
#define CFG_NVDS_TAG_BLE_CRA_TIMER_DUR      6
#define CFG_NVDS_TAG_BLE_CA_MIN_RSSI        0x40
#define CFG_NVDS_TAG_BLE_CA_NB_PKT          100
#define CFG_NVDS_TAG_BLE_CA_NB_BAD_PKT      50

/* Debug/metrics features off. */
#undef CFG_LOG_HEAP_USAGE
#undef CFG_BLE_METRICS
#undef CFG_PRODUCTION_DEBUG_OUTPUT

/* Max TX/RX data packet length (27-251 octets; the matching MaxTx/RxTime is computed
 * by the ROM as (octets + 14) * 8 us). */
#define CFG_MAX_TX_PACKET_LENGTH        (251)
#define CFG_MAX_RX_PACKET_LENGTH        (251)

/* External transport layer: 0 = GTL (auto). */
#define CFG_USE_H4TL                    (0)

/* Scan-report duplicate filter: list size (max 100); flag defined = devices beyond the
 * list are considered seen and not reported. */
#define CFG_BLE_DUPLICATE_FILTER_MAX    (10)
#undef CFG_BLE_DUPLICATE_FILTER_FOUND

/* Resolving list maximum size. */
#define CFG_LLM_RESOLVING_LIST_MAX      LLM_RESOLVING_LIST_MAX

/* No automatic data length negotiation (only safe if the peer supports DLE). */
#undef AUTO_DATA_LENGTH_NEGOTIATION_UPON_NEW_CONNECTION

/* Retention memory: max retained bytes (base address derives from it) + uninitialized
 * retained data. */
#define CFG_RET_DATA_SIZE    (2048)
#define CFG_RET_DATA_UNINIT_SIZE (0)

/* GCC/GNU-ld build: there's no Keil scatter file for the SDK to infer retention
 * from. Without these defines, arch_turn_peripherals_off() powers RAM1+RAM2 down
 * in extended sleep and the wake hangs in lockup (vectors/code in dead RAM1). With
 * all three defined, arch.h clears DO_NOT_RETAIN_ALL_RAM_BLOCKS and everything stays
 * retained. */
#define CFG_RETAIN_RAM_1_BLOCK
#define CFG_RETAIN_RAM_2_BLOCK
#define CFG_RETAIN_RAM_3_BLOCK

/* Code runs from external storage (SPI flash via Telesto), not OTP. */
#define CFG_CODE_LOCATION_EXT
#undef CFG_CODE_LOCATION_OTP

/* Ambient temperature range (-40C to +40C). */
#define CFG_AMB_TEMPERATURE

/* Disable the quadrature decoder on start-up — it is enabled at power-up by default and
 * can leave WKUP_QUADEC_IRQn pending. */
#define CFG_DISABLE_QUADEC_ON_START_UP

#endif // _DA1458X_CONFIG_ADVANCED_H_
