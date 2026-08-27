/* user_modules_config.h — SDK app-module selection (from the Dialog SDK template).
 * 0 = module included (its messages handled by the SDK); 1 = excluded. No effect for
 * BLE profiles that are not enabled in user_profiles_config.h. */

#ifndef _USER_MODULES_CONFIG_H_
#define _USER_MODULES_CONFIG_H_

#define EXCLUDE_DLG_GAP             (0)
/* Must stay 0: the click-gap reset (click_reset) uses app_easy_timer, whose expiry
 * is dispatched by app_timer_api_process_handler — and that handler is only added to
 * app_process_handlers when EXCLUDE_DLG_TIMER==0 (app_entry_point.c). With it excluded,
 * every app_easy_timer callback silently never fires, so click_run never reset and normal
 * clicking accumulated into the failsafe gesture. See the recovery-gesture note in user_app.c. */
#define EXCLUDE_DLG_TIMER           (0)
#define EXCLUDE_DLG_MSG             (0)
#define EXCLUDE_DLG_SEC             (1)
#define EXCLUDE_DLG_DISS            (1)
#define EXCLUDE_DLG_PROXR           (1)
#define EXCLUDE_DLG_BASS            (1)
#define EXCLUDE_DLG_FINDL           (1)
#define EXCLUDE_DLG_FINDT           (1)
#define EXCLUDE_DLG_SUOTAR          (1)
#if defined(WITH_CTRL_POINT)                /* the custom server carries the Control Point */
#define EXCLUDE_DLG_CUSTS1          (0)
#else
#define EXCLUDE_DLG_CUSTS1          (1)
#endif
#define EXCLUDE_DLG_CUSTS2          (1)

#endif // _USER_MODULES_CONFIG_H_
