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
    .app_on_connection                  = user_on_connection,   /* the key gate sees every link */
    .app_on_disconnect                  = user_on_disconnect,   /* releases a transfer cut short */
    .app_on_set_dev_config_complete     = default_app_on_set_dev_config_complete,
    .app_on_adv_undirect_complete       = user_on_adv_undirect_complete,
    .app_on_db_init_complete            = default_app_on_db_init_complete,
    .app_on_get_dev_name                = default_app_on_get_dev_name,
    .app_on_get_dev_appearance          = default_app_on_get_dev_appearance,
    .app_on_get_dev_slv_pref_params     = default_app_on_get_dev_slv_pref_params,
    .app_on_set_dev_info                = default_app_on_set_dev_info,
    .app_on_update_params_request       = default_app_update_params_request,
    .app_on_generate_static_random_addr = default_app_generate_static_random_addr,
};

#define app_process_catch_rest_cb       user_catch_rest_hndl

/* Our wrapper, not the SDK's default_advertise_operation: the SDK starts advertising
 * from paths we never call — after the GATT database is built, and after every
 * disconnect — and the burst model needs to know when that happens. See user_app.c. */
static const struct default_app_operations user_default_app_operations = {
    .default_operation_adv = user_advertise_operation,
};

static const struct arch_main_loop_callbacks user_app_main_loop_callbacks = {
    .app_on_init            = app_on_init,
    /* Other hooks are NULL; the SDK handles watchdog reload and sleep mode. */
};

static const struct prf_func_callbacks user_prf_funcs[] =
{
    {TASK_ID_INVALID,    NULL, NULL}   // DO NOT MOVE. Must always be last
};

#endif // _USER_CALLBACK_CONFIG_H_
