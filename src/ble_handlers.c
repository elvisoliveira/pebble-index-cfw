#include "ble_handlers.h"

#include <rwip_config.h>

#include <custs1.h>
#include <custs1_task.h>
#include <user_custs1_def.h>
#include <cfw_ctrl.h>
#include <clip_tx.h>

void user_catch_rest_hndl(ke_msg_id_t const msgid, void const *param, ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    if (msgid == CUSTS1_VAL_WRITE_IND) {
        struct custs1_val_write_ind const *p = (struct custs1_val_write_ind const *)param;
        if (p->handle == SVC1_IDX_CONTROL_POINT_VAL)
            cfw_ctrl_write(p->conidx, p->value, p->length);
        /* A CCCD write lands here like any other, on the descriptor's own handle, and
         * this is the only way the app gets to see it: the SDK keeps CCC values to
         * itself. clip_tx needs both — sending unsubscribed silently succeeds, and the
         * framing markers travel on the Control Point. */
        else if ((p->handle == SVC1_IDX_AUDIO_NTF_CFG ||
                  p->handle == SVC1_IDX_CONTROL_POINT_NTF_CFG) && p->length >= 1)
            clip_tx_set_subscribed(p->handle, (p->value[0] & 0x01) != 0);
    }
    /* Every notification the stack finishes lands here, and that is the clip transfer's
     * clock: one chunk goes out per confirmation. Without it, pushing chunks in a loop
     * exhausts the kernel message heap. */
    else if (msgid == CUSTS1_VAL_NTF_CFM) {
        struct custs1_val_ntf_cfm const *p = (struct custs1_val_ntf_cfm const *)param;
        clip_tx_on_sent(p->handle);
    }
    (void)dest_id; (void)src_id;
}
