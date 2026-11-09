#ifndef DAY_AHEAD_VIEW_H
#define DAY_AHEAD_VIEW_H
#include "esp_err.h"
esp_err_t day_ahead_view_show(void);
void day_ahead_view_hide(void);
void day_ahead_view_toggle(void);
const char *day_ahead_view_action_label(void);
#endif
