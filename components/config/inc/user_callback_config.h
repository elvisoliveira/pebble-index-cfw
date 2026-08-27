/* user_callback_config.h — callback wiring (from the Dialog SDK template).
 * BLE security is off (no CFG_APP_SECURITY), so the whole security/bond-db callback
 * scaffolding is gone — see the SDK template if it ever comes back. */

#ifndef _USER_CALLBACK_CONFIG_H_
#define _USER_CALLBACK_CONFIG_H_

#include "app_api.h"
#include "app_callback.h"
#include "app_prf_types.h"

#include <ble_handlers.h>
#include <user_app.h>

/* Unlisted members are NULL (no handler). */
static const struct app_callbacks user_app_callbacks = {
    .app_on_connection                  = default_app_on_connection,
    .app_on_disconnect                  = default_app_on_disconnect,
    .app_on_set_dev_config_complete     = user_on_set_dev_config_complete,
    .app_on_adv_undirect_complete       = user_on_adv_undirect_complete,
    .app_on_db_init_complete            = default_app_on_db_init_complete,
    .app_on_get_dev_name                = default_app_on_get_dev_name,
    .app_on_get_dev_appearance          = default_app_on_get_dev_appearance,
    .app_on_get_dev_slv_pref_params     = default_app_on_get_dev_slv_pref_params,
    .app_on_set_dev_info                = default_app_on_set_dev_info,
    .app_on_update_params_request       = default_app_update_params_request,
    .app_on_generate_static_random_addr = default_app_generate_static_random_addr,
};

/*
 * "app_process_catch_rest_cb" symbol handling:
 * - Use #define if "user_catch_rest_hndl" is defined by the user
 * - Use const declaration if "user_catch_rest_hndl" is NULL
 */
#define app_process_catch_rest_cb       user_catch_rest_hndl
//static const catch_rest_event_func_t app_process_catch_rest_cb = NULL;

static const struct default_app_operations user_default_app_operations = {
    .default_operation_adv = default_advertise_operation,
};

static const struct arch_main_loop_callbacks user_app_main_loop_callbacks = {
    .app_on_init            = app_on_init,
    // All other hooks NULL: the SDK reloads the watchdog on wake and applies
    // app_default_sleep_mode on its own.
};

// place here the app_<profile>_db_create/enable functions for SIG profiles the SDK
// does not already implement (see the SDK's prf_func array)
static const struct prf_func_callbacks user_prf_funcs[] =
{
    {TASK_ID_INVALID,    NULL, NULL}   // DO NOT MOVE. Must always be last
};

#endif // _USER_CALLBACK_CONFIG_H_
