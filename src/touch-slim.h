#ifndef TOUCH_SLIM_H
#define TOUCH_SLIM_H

#if defined(CONFIG_SLIM_MENUS) && defined(CONFIG_TOUCHSCREEN)

void touch_slim_poll(void);
int touch_slim_get_xy(int *tx, int *ty);

#else

static inline void touch_slim_poll(void) {}
static inline int touch_slim_get_xy(int *tx, int *ty)
{
    (void) tx;
    (void) ty;
    return 0;
}

#endif

#endif
