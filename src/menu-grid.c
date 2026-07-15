/** Slim menu grid launcher — 2x2 category picker with custom vector icons. */
#include "dryos.h"
#include "math.h"
#include "bmp.h"
#include "font.h"
#include "menu.h"
#include "menu-grid.h"

#ifdef CONFIG_SLIM_MENUS

#define GRID_COLS       2
#define GRID_ROWS       2
#define GRID_COUNT      (GRID_COLS * GRID_ROWS)
#define GRID_MARGIN     48
#define GRID_GAP        40
#define GRID_TOP        52
#define GRID_RADIUS     18
#define GRID_SEL_PAD    3

static int grid_active = 0;
static int grid_launched = 0;
static int grid_sel = 0;

typedef void (*grid_icon_draw_fn)(int cx, int cy, int size);

typedef struct
{
    const char *label;
    const char *menu_name;
    grid_icon_draw_fn draw;
} grid_tile_t;

static void grid_ring(int cx, int cy, int r_outer, int r_inner, int color)
{
    if (r_outer <= 0) return;
    fill_circle(cx, cy, r_outer, color);
    if (r_inner > 0)
        fill_circle(cx, cy, r_inner, COLOR_ALMOST_BLACK);
}

static void grid_fill_tri(int x0, int y0, int x1, int y1, int x2, int y2, int color)
{
    int xs[3] = { x0, x1, x2 };
    int ys[3] = { y0, y1, y2 };

    /* sort vertices by y */
    for (int i = 0; i < 2; i++)
    {
        for (int j = i + 1; j < 3; j++)
        {
            if (ys[j] < ys[i])
            {
                int tx = xs[i]; xs[i] = xs[j]; xs[j] = tx;
                int ty = ys[i]; ys[i] = ys[j]; ys[j] = ty;
            }
        }
    }

    int y_min = ys[0];
    int y_max = ys[2];

    for (int y = y_min; y <= y_max; y++)
    {
        int x_left = 720;
        int x_right = -1;

        for (int e = 0; e < 3; e++)
        {
            int xa = xs[e];
            int ya = ys[e];
            int xb = xs[(e + 1) % 3];
            int yb = ys[(e + 1) % 3];

            if (ya == yb)
            {
                if (y == ya)
                {
                    if (xa < x_left) x_left = xa;
                    if (xb < x_left) x_left = xb;
                    if (xa > x_right) x_right = xa;
                    if (xb > x_right) x_right = xb;
                }
            }
            else if (y >= MIN(ya, yb) && y <= MAX(ya, yb))
            {
                int x = xa + (xb - xa) * (y - ya) / (yb - ya);
                if (x < x_left) x_left = x;
                if (x > x_right) x_right = x;
            }
        }

        if (x_left <= x_right)
            draw_line(x_left, y, x_right, y, color);
    }
}

static void grid_fill_round_rect(int x, int y, int w, int h, int r, int color)
{
    if (w <= 0 || h <= 0) return;
    r = MIN(r, MIN(w, h) / 2);

    bmp_fill(color, x + r, y, w - 2 * r, h);
    bmp_fill(color, x, y + r, w, h - 2 * r);

    fill_circle(x + r, y + r, r, color);
    fill_circle(x + w - r - 1, y + r, r, color);
    fill_circle(x + r, y + h - r - 1, r, color);
    fill_circle(x + w - r - 1, y + h - r - 1, r, color);
}

static void grid_stroke_round_rect(int x, int y, int w, int h, int r, int color, int thick)
{
    for (int t = 0; t < thick; t++)
        bmp_draw_rect_chamfer(color, x - t, y - t, w + 2 * t, h + 2 * t, r + t, t > 0);
}

static void grid_polar_xy(int cx, int cy, int r, int ang, int *px, int *py)
{
    #define GRID_PI_1800 0.00174532925f
    #define GRID_MUL 16384
    *px = cx + (int)(r * cosf(ang * GRID_PI_1800) * GRID_MUL) / GRID_MUL;
    *py = cy + (int)(r * sinf(ang * GRID_PI_1800) * GRID_MUL) / GRID_MUL;
}

static void grid_icon_exposure(int cx, int cy, int size)
{
    int r = size;

    grid_ring(cx, cy, r, r - 7, COLOR_ORANGE);
    grid_ring(cx, cy, r - 8, r - 14, COLOR_YELLOW);

    for (int i = 0; i < 6; i++)
    {
        int a0 = i * 600 + 80;
        int a1 = a0 + 420;
        int x0, y0, x1, y1;
        grid_polar_xy(cx, cy, 12, a0, &x0, &y0);
        grid_polar_xy(cx, cy, r - 16, a0, &x1, &y1);
        int x2, y2;
        grid_polar_xy(cx, cy, r - 16, a1, &x2, &y2);
        grid_fill_tri(x0, y0, x1, y1, x2, y2, COLOR_CYAN);
        grid_fill_tri(x0, y0, x2, y2, cx, cy, COLOR_CYAN);
    }

    fill_circle(cx, cy, 11, COLOR_ALMOST_BLACK);
    fill_circle(cx, cy, 7, COLOR_GRAY(55));
}

static void grid_icon_overlays(int cx, int cy, int size)
{
    int fw = size + 26;
    int fh = size + 8;
    int fx = cx - fw / 2;
    int fy = cy - fh / 2 + 4;

    grid_fill_round_rect(fx, fy, fw, fh, 10, COLOR_GRAY(12));
    grid_stroke_round_rect(fx, fy, fw, fh, 10, COLOR_GREEN1, 2);

    int sx = fx + 10;
    int sy = fy + 10;
    int sw = fw - 20;
    int sh = fh - 20;
    bmp_fill(COLOR_ALMOST_BLACK, sx, sy, sw, sh);

    for (int d = -sh; d < sw + sh; d += 14)
        draw_line(sx + d, sy, sx + d - sh, sy + sh, COLOR_GREEN2);

    for (int d = -sh + 7; d < sw + sh; d += 14)
        draw_line(sx + d, sy, sx + d - sh, sy + sh, COLOR_YELLOW);

    /* corner brackets */
    int b = 6;
    draw_line(sx - 2, sy - 2, sx + b, sy - 2, COLOR_GREEN1);
    draw_line(sx - 2, sy - 2, sx - 2, sy + b, COLOR_GREEN1);
    draw_line(sx + sw + 2, sy - 2, sx + sw - b, sy - 2, COLOR_GREEN1);
    draw_line(sx + sw + 2, sy - 2, sx + sw + 2, sy + b, COLOR_GREEN1);
}

static void grid_icon_movie(int cx, int cy, int size)
{
    int bw = size + 30;
    int bh = size + 6;
    int bx = cx - bw / 2;
    int by = cy - bh / 2 + 8;
    int th = bh * 45 / 100;

    /* clapper top (striped) */
    grid_fill_round_rect(bx, by - th - 4, bw, th, 6, COLOR_RED);
    for (int i = 0; i < 5; i++)
    {
        int sx = bx + 6 + i * (bw - 12) / 5;
        int sw = (bw - 12) / 10;
        bmp_fill(COLOR_WHITE, sx, by - th - 2, sw, th - 4);
    }

    /* clapper body */
    grid_fill_round_rect(bx + 4, by - 4, bw - 8, bh, 8, COLOR_DARK_RED);
    grid_stroke_round_rect(bx + 4, by - 4, bw - 8, bh, 8, COLOR_RED, 2);

    /* film reel */
    int rx = cx + bw / 2 - 18;
    int ry = cy - 6;
    grid_ring(rx, ry, 16, 11, COLOR_RED);
    grid_ring(rx, ry, 9, 4, COLOR_WHITE);
    for (int i = 0; i < 6; i++)
    {
        int ax, ay;
        grid_polar_xy(rx, ry, 7, i * 600, &ax, &ay);
        draw_line(rx, ry, ax, ay, COLOR_RED);
    }
}

static void grid_icon_custom(int cx, int cy, int size)
{
    int gx = cx - size / 2 - 8;
    int gy = cy - size / 2 - 10;

    /* gear */
    grid_ring(cx - size / 2 - 6, cy - size / 2 - 4, 14, 8, COLOR_BLUE);
    for (int i = 0; i < 8; i++)
    {
        int ax, ay, bx, by;
        grid_polar_xy(cx - size / 2 - 6, cy - size / 2 - 4, 16, i * 450, &ax, &ay);
        grid_polar_xy(cx - size / 2 - 6, cy - size / 2 - 4, 20, i * 450 + 225, &bx, &by);
        draw_line(ax, ay, bx, by, COLOR_LIGHT_BLUE);
    }

    /* sliders */
    int track_w = size + 28;
    for (int row = 0; row < 3; row++)
    {
        int ty = gy + 18 + row * 22;
        int tx = gx + 22;
        bmp_fill(COLOR_GRAY(30), tx, ty + 4, track_w, 6);
        int knob = tx + (track_w - 14) * (row + 1) / 4;
        grid_fill_round_rect(knob, ty, 14, 14, 4, COLOR_BLUE);
        fill_circle(knob + 7, ty + 7, 5, COLOR_LIGHT_BLUE);
    }
}

static const grid_tile_t grid_tiles[GRID_COUNT] =
{
    { "Exposure",  "Expo",    grid_icon_exposure },
    { "Overlays",  "Overlay", grid_icon_overlays },
    { "Movie",     "Movie",   grid_icon_movie },
    { "Custom",    "Prefs",   grid_icon_custom },
};

static void grid_cell_rect(int idx, int *x, int *y, int *w, int *h)
{
    int col = idx % GRID_COLS;
    int row = idx / GRID_COLS;
    int cw = (720 - 2 * GRID_MARGIN - GRID_GAP) / GRID_COLS;
    int ch = (480 - GRID_TOP - GRID_MARGIN - GRID_GAP) / GRID_ROWS;

    *x = GRID_MARGIN + col * (cw + GRID_GAP);
    *y = GRID_TOP + row * (ch + GRID_GAP);
    *w = cw;
    *h = ch;
}

int menu_grid_is_active(void)   { return grid_active; }
int menu_grid_is_launched(void) { return grid_launched; }

void menu_grid_open(void)
{
    grid_active = 1;
    grid_launched = 0;
    grid_sel = 0;
}

void menu_grid_close(void)
{
    grid_active = 0;
    grid_launched = 0;
    grid_sel = 0;
}

void menu_grid_return(void)
{
    grid_active = 1;
    grid_launched = 0;
}

static void menu_grid_launch(int idx)
{
    if (idx < 0 || idx >= GRID_COUNT) return;

    select_menu_by_name((char *) grid_tiles[idx].menu_name, 0);
    grid_active = 0;
    grid_launched = 1;
    grid_sel = idx;
}

void menu_grid_draw(void)
{
    bmp_fill(COLOR_BLACK, 0, 40, 720, 440);

    for (int i = 0; i < GRID_COUNT; i++)
    {
        int x, y, w, h;
        grid_cell_rect(i, &x, &y, &w, &h);
        int selected = (i == grid_sel);

        grid_fill_round_rect(x, y, w, h, GRID_RADIUS, COLOR_GRAY(20));

        if (selected)
        {
            grid_stroke_round_rect(x - GRID_SEL_PAD, y - GRID_SEL_PAD,
                w + 2 * GRID_SEL_PAD, h + 2 * GRID_SEL_PAD,
                GRID_RADIUS + GRID_SEL_PAD, COLOR_ORANGE, 3);
        }
        else
            grid_stroke_round_rect(x, y, w, h, GRID_RADIUS, COLOR_GRAY(32), 1);

        int icon_cy = y + h * 40 / 100;
        int icon_size = MIN(w, h) * 38 / 100;
        icon_size = MIN(icon_size, 72);
        icon_size = MAX(icon_size, 48);
        grid_tiles[i].draw(x + w / 2, icon_cy, icon_size);

        int label_w = strlen(grid_tiles[i].label) * fontspec_font(FONT_CANON)->width;
        int label_x = x + (w - label_w) / 2;
        int label_y = y + h - fontspec_font(FONT_CANON)->height - 18;
        bmp_printf(FONT(FONT_CANON, COLOR_WHITE, NO_BG_ERASE),
            label_x, label_y, "%s", grid_tiles[i].label);
    }
}

int menu_grid_handle_key(int button_code, int *needs_full_redraw)
{
    if (!grid_active)
        return 1;

    int col = grid_sel % GRID_COLS;
    int row = grid_sel / GRID_COLS;

    switch (button_code)
    {
    case BGMT_PRESS_UP:
    case BGMT_WHEEL_UP:
        if (row > 0) grid_sel -= GRID_COLS;
        break;

    case BGMT_PRESS_DOWN:
    case BGMT_WHEEL_DOWN:
        if (row < GRID_ROWS - 1) grid_sel += GRID_COLS;
        break;

    case BGMT_PRESS_LEFT:
    case BGMT_WHEEL_LEFT:
        if (col > 0) grid_sel -= 1;
        break;

    case BGMT_PRESS_RIGHT:
    case BGMT_WHEEL_RIGHT:
        if (col < GRID_COLS - 1) grid_sel += 1;
        break;

    case BGMT_PRESS_SET:
#if defined(CONFIG_7D)
    case BGMT_JOY_CENTER:
#endif
#ifdef BGMT_Q_SET
    case BGMT_Q_SET:
#endif
        menu_grid_launch(grid_sel);
        *needs_full_redraw = 1;
        return 0;

    case BGMT_MENU:
        /* MENU from grid closes ML menu — handled in menu.c */
        return 1;

    default:
        return 1;
    }

    *needs_full_redraw = 1;
    return 0;
}

#else /* !CONFIG_SLIM_MENUS */

int menu_grid_is_active(void)   { return 0; }
int menu_grid_is_launched(void) { return 0; }
void menu_grid_open(void)       { }
void menu_grid_close(void)      { }
void menu_grid_return(void)    { }
void menu_grid_draw(void)       { }
int menu_grid_handle_key(int button_code, int *needs_full_redraw)
{
    (void) button_code;
    (void) needs_full_redraw;
    return 1;
}

#endif
