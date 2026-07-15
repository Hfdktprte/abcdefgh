/** Slim menu grid launcher — 2x2 category picker with minimal square icons. */
#include "dryos.h"
#include "math.h"
#include "bmp.h"
#include "font.h"
#include "config.h"
#include "menu.h"
#include "menu-grid.h"

#ifdef CONFIG_SLIM_MENUS

#define GRID_COLS       2
#define GRID_ROWS       2
#define GRID_COUNT      (GRID_COLS * GRID_ROWS)
/* One spacing value: left = mid = right = top = between = bottom. */
#define GRID_SPACE      40
#define GRID_RADIUS     36   /* soft modern card corners */
#define GRID_SEL_BORDER 6    /* orange ring thickness outside the grey fill */

static int grid_active = 0;
static int grid_launched = 0;
static CONFIG_INT("menu.grid.sel", grid_sel, 0);

typedef void (*grid_icon_draw_fn)(int cx, int cy, int size);

typedef struct
{
    const char *label;
    const char *menu_name;
    grid_icon_draw_fn draw;
} grid_tile_t;

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
    /* True circular corners (same as fill) — not chamfer diagonals. */
    if (w <= 0 || h <= 0) return;
    r = MIN(r, MIN(w, h) / 2);

    for (int t = 0; t < thick; t++)
    {
        int xi = x - t;
        int yi = y - t;
        int wi = w + 2 * t;
        int hi = h + 2 * t;
        int ri = MIN(r + t, MIN(wi, hi) / 2);

        draw_line(xi + ri, yi,      xi + wi - ri - 1, yi,              color);
        draw_line(xi + ri, yi + hi - 1, xi + wi - ri - 1, yi + hi - 1, color);
        draw_line(xi,      yi + ri, xi,              yi + hi - ri - 1, color);
        draw_line(xi + wi - 1, yi + ri, xi + wi - 1, yi + hi - ri - 1, color);

        draw_circle(xi + ri,           yi + ri,           ri, color);
        draw_circle(xi + wi - ri - 1,  yi + ri,           ri, color);
        draw_circle(xi + ri,           yi + hi - ri - 1,  ri, color);
        draw_circle(xi + wi - ri - 1,  yi + hi - ri - 1,  ri, color);
    }
}

/* Square icon tile: filled rounded square with centered glyph. */
static void grid_icon_square_frame(int cx, int cy, int size, int fill_c, int stroke_c)
{
    int hs = size / 2;
    grid_fill_round_rect(cx - hs, cy - hs, size, size, size / 6, fill_c);
    grid_stroke_round_rect(cx - hs, cy - hs, size, size, size / 6, stroke_c, 2);
}

static void grid_icon_exposure(int cx, int cy, int size)
{
    grid_icon_square_frame(cx, cy, size, COLOR_GRAY(24), COLOR_ORANGE);

    int inner = size * 55 / 100;
    int is = inner / 2;
    grid_stroke_round_rect(cx - is, cy - is, inner, inner, inner / 5, COLOR_YELLOW, 1);

    int dot = MAX(4, size / 10);
    fill_circle(cx, cy, dot, COLOR_WHITE);
}

static void grid_icon_overlays(int cx, int cy, int size)
{
    grid_icon_square_frame(cx, cy, size, COLOR_GRAY(14), COLOR_GREEN1);

    int pad = size / 5;
    int sx = cx - size / 2 + pad;
    int sy = cy - size / 2 + pad;
    int sw = size - 2 * pad;
    int sh = size - 2 * pad;
    bmp_fill(COLOR_ALMOST_BLACK, sx, sy, sw, sh);

    for (int d = -sh; d < sw + sh; d += MAX(8, size / 7))
        draw_line(sx + d, sy, sx + d - sh, sy + sh, COLOR_GREEN2);
}

static void grid_icon_movie(int cx, int cy, int size)
{
    grid_icon_square_frame(cx, cy, size, COLOR_GRAY(18), COLOR_RED);

    int hs = size * 30 / 100;
    for (int dy = -hs; dy <= hs; dy++)
    {
        int y = cy + dy;
        int xl = cx - hs / 2;
        int xr = (dy <= 0)
            ? cx - hs / 2 + (hs + dy)
            : cx + hs / 2 - dy;
        if (xl <= xr)
            draw_line(xl, y, xr, y, COLOR_WHITE);
    }
}

static void grid_icon_custom(int cx, int cy, int size)
{
    grid_icon_square_frame(cx, cy, size, COLOR_GRAY(20), COLOR_BLUE);

    int track_w = size * 65 / 100;
    int tx = cx - track_w / 2;
    int row_h = MAX(10, size / 6);
    int y0 = cy - row_h;

    for (int row = 0; row < 3; row++)
    {
        int ty = y0 + row * row_h;
        bmp_fill(COLOR_GRAY(40), tx, ty + 3, track_w, 4);
        int knob = tx + track_w * (row + 1) / 4 - 3;
        grid_fill_round_rect(knob, ty, 6, 10, 2, COLOR_LIGHT_BLUE);
    }
}

static const grid_tile_t grid_tiles[GRID_COUNT] =
{
    { "Exposure",   "Expo",     grid_icon_exposure },
    { "Monitoring", "Overlay",  grid_icon_overlays },
    { "Movie",      "Movie",    grid_icon_movie },
    { "Settings",   "Settings", grid_icon_custom },
};

static void grid_layout(int *ox, int *oy, int *cw, int *ch)
{
    /* Margin == gap on each axis: 2 cells + 3 equal spaces fill the screen. */
    *cw = (720 - 3 * GRID_SPACE) / GRID_COLS;
    *ch = (480 - 3 * GRID_SPACE) / GRID_ROWS;
    *ox = GRID_SPACE;
    *oy = GRID_SPACE;
}

static void grid_cell_rect(int idx, int *x, int *y, int *w, int *h)
{
    int ox, oy, cw, ch;
    grid_layout(&ox, &oy, &cw, &ch);
    int col = idx % GRID_COLS;
    int row = idx / GRID_COLS;
    *x = ox + col * (cw + GRID_SPACE);
    *y = oy + row * (ch + GRID_SPACE);
    *w = cw;
    *h = ch;
}

int menu_grid_is_active(void)   { return grid_active; }
int menu_grid_is_launched(void) { return grid_launched; }

void menu_grid_open(void)
{
    grid_active = 1;
    grid_launched = 0;
    grid_sel = COERCE(grid_sel, 0, GRID_COUNT - 1);
}

void menu_grid_close(void)
{
    grid_active = 0;
    grid_launched = 0;
}

void menu_grid_return(void)
{
    grid_active = 1;
    grid_launched = 0;
    grid_sel = COERCE(grid_sel, 0, GRID_COUNT - 1);
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
    bmp_fill(COLOR_BLACK, 0, 0, 720, 480);

    int label_h = fontspec_font(FONT_CANON)->height;
    int label_gap = 12;
    int bottom_pad = 14;
    int b = GRID_SEL_BORDER;

    for (int i = 0; i < GRID_COUNT; i++)
    {
        int x, y, w, h;
        grid_cell_rect(i, &x, &y, &w, &h);
        int selected = (i == grid_sel);
        int r = MIN(GRID_RADIUS, MIN(w, h) / 2);

        /* Orange ring = outer rounded fill, then grey punched on top (same geometry). */
        if (selected)
            grid_fill_round_rect(x - b, y - b, w + 2 * b, h + 2 * b, r + b, COLOR_ORANGE);

        grid_fill_round_rect(x, y, w, h, r, COLOR_GRAY(20));

        int icon_zone_h = h - label_h - bottom_pad - label_gap;
        int icon_cy = y + icon_zone_h / 2;
        int icon_size = MIN(MIN(w, h) * 42 / 100, icon_zone_h * 72 / 100);
        icon_size = MIN(icon_size, 64);
        icon_size = MAX(icon_size, 40);
        grid_tiles[i].draw(x + w / 2, icon_cy, icon_size);

        int label_w = strlen(grid_tiles[i].label) * fontspec_font(FONT_CANON)->width;
        int label_x = x + (w - label_w) / 2;
        int label_y = y + h - bottom_pad - label_h;
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
        row = (row + GRID_ROWS - 1) % GRID_ROWS;
        grid_sel = row * GRID_COLS + col;
        break;

    case BGMT_PRESS_DOWN:
    case BGMT_WHEEL_DOWN:
        row = (row + 1) % GRID_ROWS;
        grid_sel = row * GRID_COLS + col;
        break;

    case BGMT_PRESS_LEFT:
    case BGMT_WHEEL_LEFT:
        col = (col + GRID_COLS - 1) % GRID_COLS;
        grid_sel = row * GRID_COLS + col;
        break;

    case BGMT_PRESS_RIGHT:
    case BGMT_WHEEL_RIGHT:
        col = (col + 1) % GRID_COLS;
        grid_sel = row * GRID_COLS + col;
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
