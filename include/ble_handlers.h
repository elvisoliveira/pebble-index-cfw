#ifndef BLE_HANDLERS_H_
#define BLE_HANDLERS_H_

#include <stdint.h>
#include <stdbool.h>

#include <da1458x_config_basic.h>
#include <da1458x_config_advanced.h>
#include <user_config.h>
#include <rwip_config.h>

#include <arch_api.h>
#include <ke_msg.h>
#include <arch.h>

void user_catch_rest_hndl(ke_msg_id_t const msgid, void const *param, ke_task_id_t const dest_id, ke_task_id_t const src_id);

#if defined(CONN_PROTO)
/* Variant B prototype: connection callbacks that wrap the SDK defaults to track the
 * live connection (see user_app.c cfw_on_connect/cfw_on_disconnect). */
#include <gapc_task.h>
void user_on_connection(uint8_t connection_idx, struct gapc_connection_req_ind const *param);
void user_on_disconnect(struct gapc_disconnect_ind const *param);
#endif

#endif // BLE_HANDLERS_H_
