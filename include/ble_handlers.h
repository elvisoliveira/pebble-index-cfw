#ifndef BLE_HANDLERS_H_
#define BLE_HANDLERS_H_

#include <ke_msg.h>

void user_catch_rest_hndl(ke_msg_id_t const msgid, void const *param, ke_task_id_t const dest_id, ke_task_id_t const src_id);

#endif // BLE_HANDLERS_H_
