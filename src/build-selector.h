#ifndef _build_selector_h_
#define _build_selector_h_

/* Apply a selection before anything opens files below ML/. */
void build_selector_early_apply(void);

/* True only for the short hold-UP-at-boot selection window. */
int build_selector_boot_window_active(void);

/* Capture UP while the normal ML event router is not started yet. */
int build_selector_note_boot_up(int event_code);

/* Open the selector using the normal Slim menu renderer. */
void gui_open_build_selector(void);

#endif
