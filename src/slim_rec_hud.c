/**
 * Cinema-style recording HUD for EOS M slim builds (BMCC-like bars).
 *
 * Top: Ready/Rec, timecode, RAW, res/fps, aperture, ISO, shutter°, Kelvin, battery%
 * Bottom: wide zebra/hist strip (no top border) | free space GB | dual AU meters
 */

#include <dryos.h>
#include <bmp.h>
#include <lens.h>
#include <property.h>
#include <propvalues.h>
#include <battery.h>
#include <histogram.h>
#include <audio.h>
#include <zebra.h>
#include <fps.h>
#include <raw.h>
#include <gui.h>
#include <fio-ml.h>
#include "slim_rec_hud.h"

#ifndef CONFIG_SLIM_REC_HUD

int slim_rec_hud_display(void)
{
    return 0;
}

#else

#ifndef COLOR_GREEN1
#define COLOR_GREEN1 0x0F
#endif

#define HUD_TOP_Y       0
#define HUD_TOP_H       28
#define HUD_BOT_Y       438
#define HUD_BOT_H       42
#define HUD_ZEBRA_X     6
#define HUD_ZEBRA_W     300
#define HUD_GB_CX       360
#define HUD_METER_X     420
#define HUD_METER_W     285

static int hud_rec_t0_ms = 0;
static int hud_was_recording = 0;

static int hud_is_recording(void)
{
    return RECORDING || RECORDING_RAW;
}

static void hud_fill_bar(int y, int h)
{
    bmp_fill(COLOR_BLACK, 0, y, 720, h);
}

static void hud_draw_top(void)
{
    char buf[64];
    int y = HUD_TOP_Y + 6;
    int x = 8;
    int fnt = FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK);
    int fnt_ready = FONT(FONT_MED, COLOR_CYAN, COLOR_BLACK);
    int fnt_rec = FONT(FONT_MED, COLOR_RED, COLOR_BLACK);

    int recording = hud_is_recording();
    if (recording && !hud_was_recording)
        hud_rec_t0_ms = get_ms_clock();
    if (!recording)
        hud_rec_t0_ms = 0;
    hud_was_recording = recording;

    hud_fill_bar(HUD_TOP_Y, HUD_TOP_H);

    if (recording)
        bmp_printf(fnt_rec, x, y, "Rec");
    else
        bmp_printf(fnt_ready, x, y, "Ready");
    x += 70;

    {
        int fps_x1000 = fps_get_current_x1000();
        if (fps_x1000 < 1) fps_x1000 = 24000;
        int elapsed_ms = recording ? (get_ms_clock() - hud_rec_t0_ms) : 0;
        if (elapsed_ms < 0) elapsed_ms = 0;
        int ff = (elapsed_ms % 1000) * fps_x1000 / 1000000;
        int ss = (elapsed_ms / 1000) % 60;
        int mm = (elapsed_ms / 60000) % 60;
        int hh = elapsed_ms / 3600000;
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d:%02d", hh, mm, ss, MIN(ff, 99));
        bmp_printf(fnt, x, y, "%s", buf);
        x += 120;
    }

    bmp_printf(fnt, x, y, "RAW");
    x += 48;

    {
        int fps_x1000 = fps_get_current_x1000();
        int fps_i = (fps_x1000 + 500) / 1000;
        const char *res =
            video_mode_resolution == 0 ? "1080p" :
            video_mode_resolution == 1 ? "720p" :
            video_mode_resolution == 2 ? "480p" : "MOV";
        if (raw_lv_is_enabled() && raw_info.width > 0)
        {
            int rh = raw_info.active_area.y2 - raw_info.active_area.y1;
            if (rh > 1200) res = "1440p";
            else if (rh > 1000) res = "1080p";
            else if (rh > 700) res = "720p";
        }
        snprintf(buf, sizeof(buf), "%s%d", res, fps_i);
        bmp_printf(fnt, x, y, "%s", buf);
        x += 78;
    }

    if (lens_info.raw_aperture && lens_info.lens_exists)
    {
        snprintf(buf, sizeof(buf), "%s", lens_format_aperture(lens_info.raw_aperture));
        bmp_printf(fnt, x, y, "%s", buf);
    }
    x += 55;

    if (lens_info.iso)
    {
        snprintf(buf, sizeof(buf), "ISO%d", lens_info.iso);
        bmp_printf(fnt, x, y, "%s", buf);
    }
    x += 70;

    {
        int shut_x1000 = get_current_shutter_reciprocal_x1000();
        int fps_x1000 = fps_get_current_x1000();
        if (shut_x1000 > 0 && fps_x1000 > 0)
        {
            int deg = (360 * fps_x1000 + shut_x1000 / 2) / shut_x1000;
            snprintf(buf, sizeof(buf), "%ddeg", deg);
            bmp_printf(fnt, x, y, "%s", buf);
        }
        x += 60;
    }

    if (lens_info.kelvin)
    {
        snprintf(buf, sizeof(buf), "%dK", lens_info.kelvin);
        bmp_printf(fnt, x, y, "%s", buf);
    }

    {
        int bat = GetBatteryLevel();
        if (bat >= 0 && bat <= 100)
        {
            snprintf(buf, sizeof(buf), "%d%%", bat);
            bmp_printf(fnt, 720 - bmp_string_width(fnt, buf) - 8, y, "%s", buf);
        }
    }
}

static void hud_draw_zebra_strip(void)
{
    int x0 = HUD_ZEBRA_X;
    int y0 = HUD_BOT_Y + 6;
    int w = HUD_ZEBRA_W;
    int h = 24;

    /* Side + bottom edges only (no top line) */
    bmp_fill(COLOR_GRAY(50), x0, y0, 1, h);
    bmp_fill(COLOR_GRAY(50), x0 + w, y0, 1, h);
    bmp_fill(COLOR_GRAY(70), x0, y0 + h, w + 1, 1);

    for (int i = 0; i < 5; i++)
    {
        int ty = y0 + 2 + i * (h - 4) / 4;
        bmp_fill(COLOR_WHITE, x0 + w - 4, ty, 4, 1);
    }

    if (histogram.max > 0)
    {
        int maxv = (int)histogram.max;
        for (int i = 0; i < HIST_WIDTH; i++)
        {
            uint32_t v = histogram.hist[i];
            int bh = (int)((v * (h - 3)) / maxv);
            if (bh < 1 && v) bh = 1;
            int px = x0 + 2 + (i * (w - 10)) / HIST_WIDTH;
            int pw = MAX(1, (w - 10) / HIST_WIDTH);
            if (bh > 0)
                bmp_fill(COLOR_WHITE, px, y0 + h - bh, pw, bh);
        }
    }
    else
    {
        bmp_fill(COLOR_GRAY(30), x0 + 2, y0 + h - 2, w - 8, 1);
    }
}

static void hud_draw_gb(void)
{
    char buf[16];
    int free_space_32k = get_free_space_32k(get_shooting_card());
    int fsg = free_space_32k >> 15;
    int fsgr = free_space_32k - (fsg << 15);
    int fsgf = (fsgr * 10) >> 15;
    snprintf(buf, sizeof(buf), "%d.%d GB", fsg, fsgf);

    int fnt = FONT(FONT_MED, COLOR_WHITE, COLOR_BLACK);
    int tw = bmp_string_width(fnt, buf);
    bmp_printf(fnt, HUD_GB_CX - tw / 2, HUD_BOT_Y + 12, "%s", buf);
}

static void hud_draw_audio_meters(void)
{
    struct audio_level *levels = get_audio_levels();
    if (!levels) return;
    if (!sound_recording_enabled()) return;

    int x0 = HUD_METER_X;
    int y0 = HUD_BOT_Y + 4;
    int width = HUD_METER_W - 24;
    int meter_h = 8;
    int mx = x0 + 12;

    bmp_printf(FONT(FONT_SMALL, COLOR_WHITE, COLOR_BLACK), x0, y0, "1");
    bmp_printf(FONT(FONT_SMALL, COLOR_WHITE, COLOR_BLACK), x0, y0 + 12, "2");

    for (int ch = 0; ch < 2; ch++)
    {
        int my = y0 + ch * 12;
        int db = audio_level_to_db(levels[ch].peak_fast);
        int db_peak = audio_level_to_db(levels[ch].peak);
        int bar_w = COERCE((db + 36) * width / 36, 0, width);
        int peak_x = COERCE((db_peak + 36) * width / 36, 0, width);

        bmp_fill(COLOR_BLACK, mx, my, width, meter_h);
        if (bar_w > 0)
            bmp_fill(COLOR_GREEN1, mx, my, bar_w, meter_h);
        if (peak_x > 0 && peak_x < width)
            bmp_fill(COLOR_WHITE, mx + peak_x, my, 2, meter_h);
    }

    int sy = y0 + 26;
    bmp_printf(FONT(FONT_SMALL, COLOR_GRAY(50), COLOR_BLACK), mx, sy, "-36");
    bmp_printf(FONT(FONT_SMALL, COLOR_GRAY(50), COLOR_BLACK), mx + width / 2 - 10, sy, "-18");
    bmp_printf(FONT(FONT_SMALL, COLOR_GRAY(50), COLOR_BLACK), mx + width - 12, sy, "0");
}

static void hud_draw_bottom(void)
{
    hud_fill_bar(HUD_BOT_Y, HUD_BOT_H);
    hud_draw_zebra_strip();
    hud_draw_gb();
    hud_draw_audio_meters();
}

int slim_rec_hud_display(void)
{
    if (!get_global_draw()) return 0;
    if (!is_movie_mode()) return 0;
    if (!liveview_display_idle()) return 0;
    if (gui_menu_shown()) return 0;

    BMP_LOCK(
        hud_draw_top();
        hud_draw_bottom();
    );
    return 1;
}

#endif /* CONFIG_SLIM_REC_HUD */
