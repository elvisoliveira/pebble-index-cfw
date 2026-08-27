/* Custom server (CUSTS1) profile registration (from the Dialog SDK template).
 * CUSTS2 is not used; its template block was dropped. */

#include "app_prf_types.h"
#include "app_customs.h"
#include "user_custs1_def.h"

#if (BLE_CUSTOM1_SERVER)
extern const struct attm_desc_128 custs1_att_db[CUSTS1_IDX_NB];
#endif

/// Custom server function callback table
const struct cust_prf_func_callbacks cust_prf_funcs[] =
{
#if (BLE_CUSTOM1_SERVER)
    {   TASK_ID_CUSTS1,
        custs1_att_db,
        CUSTS1_IDX_NB,
        #if (BLE_APP_PRESENT)
        app_custs1_create_db, NULL,
        #else
        NULL, NULL,
        #endif
        NULL, NULL,
    },
#endif
    {TASK_ID_INVALID, NULL, 0, NULL, NULL, NULL, NULL},   // DO NOT MOVE. Must always be last
};
