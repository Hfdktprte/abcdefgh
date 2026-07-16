/** EOS M slim: Canon touch hook + poll → ML BGMT_TOUCH_* events. */
#include "dryos.h"
#include "consts.h"
#include "gui-common.h"
#include "touch-slim.h"

#if defined(CONFIG_SLIM_MENUS) && defined(CONFIG_TOUCHSCREEN)

static uint32_t touch_orig_cbr = 0;
static int touch_prev_fingers = -1;
static int touch_cached_x = 0;
static int touch_cached_y = 0;
static int touch_cached_valid = 0;

static int touch_read_fingers(void)
{
#ifdef TOUCH_MULTI
    return MEM(TOUCH_MULTI) & 0xF;
#else
    return 0;
#endif
}

static int touch_read_xy_live(int *tx, int *ty)
{
#ifdef TOUCH_XY_RAW1
    uint32_t raw = MEM(TOUCH_XY_RAW1);
    if (raw == 0)
        raw = MEM(TOUCH_XY_RAW2);
    *tx = COERCE((int)(raw & 0xFFF), 0, 719);
    *ty = COERCE((int)((raw >> 12) & 0xFFF), 0, 479);
    return touch_read_fingers() >= 1 || raw != 0;
#else
    (void) tx;
    (void) ty;
    return 0;
#endif
}

void touch_slim_capture_xy(void)
{
    int tx, ty;
    if (!touch_read_xy_live(&tx, &ty))
        return;
    touch_cached_x = tx;
    touch_cached_y = ty;
    touch_cached_valid = 1;
}

void touch_slim_clear_xy(void)
{
    touch_cached_valid = 0;
}

static void touch_emit_edges(int fingers)
{
    int prev = touch_prev_fingers;
    if (prev < 0)
    {
        touch_prev_fingers = fingers;
        return;
    }
    if (fingers == prev)
        return;

    if (fingers >= 1 && prev < 1)
    {
        touch_slim_capture_xy();
        fake_simple_button(BGMT_TOUCH_1_FINGER);
    }
    if (fingers >= 2 && prev < 2)
        fake_simple_button(BGMT_TOUCH_2_FINGER);

    if (fingers < 1 && prev >= 1)
        fake_simple_button(BGMT_UNTOUCH_1_FINGER);
    if (fingers < 2 && prev >= 2)
        fake_simple_button(BGMT_UNTOUCH_2_FINGER);

    touch_prev_fingers = fingers;
}

void touch_slim_poll(void)
{
    int fingers = touch_read_fingers();
    if (fingers >= 1)
        touch_slim_capture_xy();
    touch_emit_edges(fingers);
}

int touch_slim_get_xy(int *tx, int *ty)
{
    if (touch_cached_valid)
    {
        *tx = touch_cached_x;
        *ty = touch_cached_y;
        return 1;
    }
    return touch_read_xy_live(tx, ty);
}

#ifdef HIJACK_TOUCH_CBR_PTR
/* Canon invokes the RAM hook when touch state changes (EOS M: void callback). */
static void touch_hook_cbr(void)
{
    touch_slim_poll();
    if (touch_orig_cbr)
        ((void (*)(void))touch_orig_cbr)();
}
#endif

static void touch_slim_install(void)
{
    touch_prev_fingers = touch_read_fingers();

#ifdef HIJACK_TOUCH_CBR_PTR
    touch_orig_cbr = MEM(HIJACK_TOUCH_CBR_PTR);
    if (touch_orig_cbr && touch_orig_cbr != (uint32_t)touch_hook_cbr)
        MEM(HIJACK_TOUCH_CBR_PTR) = (uint32_t)touch_hook_cbr;
#endif
}

INIT_FUNC("touch-slim", touch_slim_install);

#endif
