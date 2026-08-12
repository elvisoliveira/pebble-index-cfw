#include "ble_handlers.h"

#include <da1458x_config_basic.h>
#include <da1458x_config_advanced.h>
#include <user_config.h>
#include <rwip_config.h>

#include <ke_msg.h>
#include <custs1.h>
#include <custs1_task.h>
#include <user_custs1_def.h>
#include <user_app.h>

void user_catch_rest_hndl(ke_msg_id_t const msgid, void const *param, ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    switch(msgid)
    {
        case CUSTS1_VAL_WRITE_IND:
        {
            struct custs1_val_write_ind const *p = (struct custs1_val_write_ind const *)param;
            if (p->handle == SVC1_IDX_CONTROL_POINT_VAL)
                cfw_ctrl_write(p->value, p->length);  // redundant BLE path to recovery
            break;
        }
        default:
            break;
    }
}
