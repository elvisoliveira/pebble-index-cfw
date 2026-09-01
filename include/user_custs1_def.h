/* Custom Server 1 (CUSTS1) profile database definitions (from the Dialog SDK template). */

#ifndef _USER_CUSTS1_DEF_H_
#define _USER_CUSTS1_DEF_H_

#include "attm_db_128.h"

// Service 1 of the custom server 1
#define DEF_SVC1_UUID_128                {0x59, 0x5a, 0x08, 0xe4, 0x86, 0x2a, 0x9e, 0x8f, 0xe9, 0x11, 0xbc, 0x7c, 0x98, 0x43, 0x42, 0x18}

#define DEF_SVC1_CTRL_POINT_UUID_128     {0x20, 0xEE, 0x8D, 0x0C, 0xE1, 0xF0, 0x4A, 0x0C, 0xB3, 0x25, 0xDC, 0x53, 0x6A, 0x68, 0x86, 0x2D}
#define DEF_SVC1_CTRL_POINT_CHAR_LEN     32
#define DEF_SVC1_CONTROL_POINT_USER_DESC "Control Point"

/* Audio out: notify-only, sized to what a 247-byte MTU carries (MTU minus the 3-byte
 * ATT header). The Control Point could carry the clip too, but at 32 bytes a value it
 * would be the bottleneck, and it is a command channel — mixing bulk data into it
 * makes both harder to reason about. */
#define DEF_SVC1_AUDIO_UUID_128          {0x21, 0xEE, 0x8D, 0x0C, 0xE1, 0xF0, 0x4A, 0x0C, 0xB3, 0x25, 0xDC, 0x53, 0x6A, 0x68, 0x86, 0x2D}
#define DEF_SVC1_AUDIO_CHAR_LEN          244
#define DEF_SVC1_AUDIO_USER_DESC         "Audio"

/// Custom1 Service Data Base Characteristic enum
enum
{
    SVC1_IDX_SVC = 0,

    SVC1_IDX_CONTROL_POINT_CHAR,
    SVC1_IDX_CONTROL_POINT_VAL,
    SVC1_IDX_CONTROL_POINT_NTF_CFG,
    SVC1_IDX_CONTROL_POINT_USER_DESC,

    SVC1_IDX_AUDIO_CHAR,
    SVC1_IDX_AUDIO_VAL,
    SVC1_IDX_AUDIO_NTF_CFG,
    SVC1_IDX_AUDIO_USER_DESC,

    CUSTS1_IDX_NB
};

#endif // _USER_CUSTS1_DEF_H_
