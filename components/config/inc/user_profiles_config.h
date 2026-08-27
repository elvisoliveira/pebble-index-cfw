/* user_profiles_config.h — BLE profile selection (used by rwprf_config.h). */

#ifndef _USER_PROFILES_CONFIG_H_
#define _USER_PROFILES_CONFIG_H_

/* The only profile: the custom server that carries the Control Point. Gated on
 * WITH_CTRL_POINT (defined at the top of da1458x_config_basic.h, before this file is
 * pulled in) — off => no custom profile, beacon only. */
#if defined(WITH_CTRL_POINT)
#define CFG_PRF_CUST1
#endif

#endif // _USER_PROFILES_CONFIG_H_
