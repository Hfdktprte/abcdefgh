/* Native Filmatura startup image. */
#include "dryos.h"
#include "bmp.h"
#include "gui-common.h"
#include "lvinfo.h"
#include "menu.h"
#include "zebra.h"

#include "boot-logo-data.h"

#define BOOT_LOGO_W 720
#define BOOT_LOGO_H 480

extern int ml_started;

static void boot_logo_draw(void)
{
    /* Native 8-bit pixels, copied directly into the normal LCD canvas.
     * BMPPITCH is 960, so a complete 720-pixel row is one safe bulk copy. */
    uint8_t *vram = bmp_vram();
    for (int y = 0; y < BOOT_LOGO_H; y++)
        memcpy(vram + y * BMPPITCH, boot_logo_pixels + y * BOOT_LOGO_W, BOOT_LOGO_W);
}

static volatile int boot_logo_active = 0;
static int boot_logo_hide_time = 0;
static volatile int boot_logo_handoff_pending = 0;
static volatile int boot_logo_hud_mask = 0;

int boot_logo_is_active(void)
{
    return boot_logo_active;
}

/* Keep ML's own status bars off the splash.  They are permitted only for
 * the final handoff frame, while Canon remains masked. */
int boot_logo_allows_overlay_draw(void)
{
    return !boot_logo_active || boot_logo_handoff_pending;
}

/* Called by the normal ML status-bar renderer.  Do not reveal Canon's
 * overlay until both status bars have had a chance to replace the splash. */
void boot_logo_overlay_updated(int top, int bottom)
{
    if (!boot_logo_handoff_pending) return;
    if (top)    boot_logo_hud_mask |= 1;
    if (bottom) boot_logo_hud_mask |= 2;
}

static void boot_logo_present(void)
{
    bmp_draw_to_idle(1);
    boot_logo_draw();
    bmp_idle_copy(1, 0);
    bmp_draw_to_idle(0);
}

static void boot_logo_clear(void)
{
    bmp_draw_to_idle(1);
    /* Keep the canvas opaque during the handoff.  A transparent frame here
     * exposes a stale Canon fragment before ML draws its own HUD. */
    bmp_fill(COLOR_BLACK, 0, 0, 720, 480);
    bmp_idle_copy(1, 0);
    bmp_draw_to_idle(0);
}

static void boot_logo_release_canvas(void)
{
    bmp_draw_to_idle(1);
    bmp_fill(COLOR_EMPTY, 0, 0, 720, 480);
    bmp_idle_copy(1, 0);
    bmp_draw_to_idle(0);
}

static void boot_logo_task(void *unused)
{
    (void) unused;

    const int fallback_handoff_time = boot_logo_hide_time + 500;
    while (boot_logo_active)
    {
        int splash_time_done = get_ms_clock() >= boot_logo_hide_time;
        int ml_display_ready = ml_started &&
            (liveview_display_idle() || get_ms_clock() >= fallback_handoff_time);
        if (splash_time_done && ml_display_ready) break;
        msleep(20);
    }

    if (boot_logo_active)
    {
        /* Request ML's first HUD redraw while Canon remains masked. */
        boot_logo_hud_mask = 0;
        boot_logo_handoff_pending = 1;
        lens_display_set_dirty();
        menu_set_dirty();
        BMP_LOCK( boot_logo_clear(); )
        const int hud_deadline = get_ms_clock() + 1000;
        while (boot_logo_hud_mask != 3 && get_ms_clock() < hud_deadline)
            msleep(20);
        boot_logo_handoff_pending = 0;
        BMP_LOCK( boot_logo_release_canvas(); )
        boot_logo_active = 0;
    }
}

void boot_logo_show(void)
{
    if (!bmp_vram_raw()) return;

    /* Keep Canon's dialogs from overwriting the splash while it is visible. */
    boot_logo_active = 1;
    canon_gui_disable_front_buffer();
    boot_logo_hide_time = get_ms_clock() + 2000;
    BMP_LOCK( boot_logo_present(); )
    task_create("boot_logo", 0x1e, 0x1000, boot_logo_task, 0);
}
