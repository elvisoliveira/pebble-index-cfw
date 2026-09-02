/* Clip transfer over GATT notifications — see include/clip_tx.h for the protocol. */
#include <clip_tx.h>
#include <mic.h>
#include <led.h>
#include <user_custs1_def.h>
#include <custs1_task.h>
#include <prf.h>
#include <ke_msg.h>
#include <string.h>

#define CHUNK_MIN 20    /* what a default 23-byte MTU carries */
#define CHUNK_MAX DEF_SVC1_AUDIO_CHAR_LEN

#define CMD_SEND  0x01
#define CMD_DONE  0x02

static const uint8_t *clip;
static uint16_t total;      /* bytes to send */
static uint16_t sent;
static uint16_t chunk_len;
static uint8_t  conn;
static bool     active;
static bool     subscribed;     /* the Audio CCCD, as the peer last wrote it */

static void notify(uint16_t handle, const uint8_t *data, uint16_t len)
{
    struct custs1_val_ntf_ind_req *req = KE_MSG_ALLOC_DYN(CUSTS1_VAL_NTF_REQ,
        prf_get_task_from_id(TASK_ID_CUSTS1), TASK_APP, custs1_val_ntf_ind_req, len);

    req->conidx       = conn;
    req->notification = true;
    req->handle       = handle;
    req->length       = len;
    memcpy(req->value, data, len);
    KE_MSG_SEND(req);
}

static void send_next(void)
{
    uint16_t left = total - sent;

    if (left == 0) {
        const uint8_t done = CMD_DONE;
        active = false;
        led_off();
        /* Released only here, after every chunk was confirmed — so the advertisement
         * stops offering a clip that has already been taken, and a transfer that dies
         * halfway leaves the recording intact to be asked for again. */
        mic_clip_release();
        notify(SVC1_IDX_CONTROL_POINT_VAL, &done, 1);
        return;
    }
    if (left > chunk_len) {
        left = chunk_len;
    }
    notify(SVC1_IDX_AUDIO_VAL, &clip[sent], left);
    sent += left;
}

void clip_tx_start(uint8_t conidx, uint16_t chunk)
{
    uint16_t samples;

    if (active) {
        return;
    }
    /*
     * Without the subscription there is nothing to send INTO, and the stack will not say
     * so: custs1_exe_operation skips a connection whose CCCD is clear and then confirms
     * the notification with GAP_ERR_NO_ERROR anyway (custs1_task.c:240-278). Every chunk
     * would "succeed" without reaching the air, at kernel-message speed, and the DONE at
     * the end would release a clip the phone never received. There is no status to test
     * afterwards — the test has to happen here, before the clip is spent.
     */
    if (!subscribed) {
        return;
    }
    /* No recording can be running here TODAY: mic_capture blocks the task context this
     * write arrives in, so the two cannot overlap. The day capture goes DMA and stops
     * blocking, that protection evaporates — add a recording check here, or this walks
     * a buffer being overwritten. */
    clip = mic_clip(&samples);
    if (samples == 0) {
        return;
    }

    /* Clamp rather than trust: a chunk of 0 would never finish, and one above the
     * characteristic's own length would be truncated by the stack without saying so. */
    if (chunk < CHUNK_MIN) chunk = CHUNK_MIN;
    if (chunk > CHUNK_MAX) chunk = CHUNK_MAX;

    conn      = conidx;
    chunk_len = chunk;
    total     = (samples + 1) / 2;      /* two 4-bit samples per byte */
    sent      = 0;
    active    = true;

    led_hold(LED_TRANSFER);     /* lit for the whole send, dark when it ends either way */

    const uint8_t hdr[3] = { CMD_SEND, (uint8_t)samples, (uint8_t)(samples >> 8) };
    notify(SVC1_IDX_CONTROL_POINT_VAL, hdr, sizeof hdr);
    /* The first chunk waits for THIS notification's confirmation, so the header and the
     * data cannot race each other out of order. */
}

void clip_tx_on_sent(uint16_t handle)
{
    if (!active) {
        return;
    }
    /* Both handles pace the same transfer: the header's confirmation releases the first
     * chunk, each chunk's releases the next. */
    if (handle == SVC1_IDX_CONTROL_POINT_VAL || handle == SVC1_IDX_AUDIO_VAL) {
        send_next();
    }
}

void clip_tx_set_subscribed(bool on)
{
    subscribed = on;
}

void clip_tx_abort(void)
{
    if (active) {
        active = false;
        led_off();
    }
}

bool clip_tx_busy(void)
{
    return active;
}
