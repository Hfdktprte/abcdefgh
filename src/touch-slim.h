#ifndef TOUCH_SLIM_H
#define TOUCH_SLIM_H

#if defined(CONFIG_SLIM_MENUS) && defined(CONFIG_TOUCHSCREEN)

void touch_slim_poll(void);
void touch_slim_capture_xy(void);
int touch_slim_get_xy(int *tx, int *ty);
void touch_slim_clear_xy(void);

#else

static inline void touch_slim_poll(void) {}
static inline void touch_slim_capture_xy(void) {}
static inline int touch_slim_get_xy(int *tx, int *ty)
{
    (void) tx;
    (void) ty;
    return 0;
}
static inline void touch_slim_clear_xy(void) {}

#endif

#endif
