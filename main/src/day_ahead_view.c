#include "inc/day_ahead_view.h"
#include "inc/day_ahead.h"
#include "inc/display.h"
#include <math.h>
#include <stdio.h>
#include <stdatomic.h>
#include "bsp/esp-box-3.h"
#include "lvgl.h"

static atomic_bool s_tomorrow;
static lv_obj_t *s_panel, *s_heading, *s_now, *s_chart, *s_scale, *s_cheapest, *s_note;
static lv_chart_series_t *s_prices;
static lv_obj_t *s_y_ticks[3], *s_x_ticks[3];

// Leave the bottom 60 pixels for the existing navigation and day button.
#define CHART_X 52
#define CHART_Y 46
#define CHART_WIDTH 258
#define CHART_HEIGHT 92

void day_ahead_view_toggle(void)
{
    atomic_store(&s_tomorrow, !atomic_load(&s_tomorrow));
}

const char *day_ahead_view_action_label(void)
{
    return atomic_load(&s_tomorrow) ? "Vandaag" : "Morgen";
}

void day_ahead_view_hide(void)
{
    if (bsp_display_lock(1000)) {
        if (s_panel) lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        bsp_display_unlock();
    }
}

static lv_obj_t *s_label(int y)
{
    lv_obj_t *label = lv_label_create(s_panel);
    lv_obj_set_width(label, 304);
    lv_obj_set_pos(label, 8, y);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF2F5F0), LV_PART_MAIN);
    return label;
}

static void s_build(void)
{
    s_panel = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(s_panel, 0, 0);
    lv_obj_set_size(s_panel, 320, 180);
    lv_obj_set_style_pad_all(s_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0x14324A), LV_PART_MAIN);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_heading = s_label(4);
    s_now = s_label(23);
    s_chart = lv_chart_create(s_panel);
    lv_obj_set_pos(s_chart, CHART_X, CHART_Y);
    lv_obj_set_size(s_chart, CHART_WIDTH, CHART_HEIGHT);
    lv_obj_remove_flag(s_chart, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x14324A), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_chart, 2, LV_PART_MAIN);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(s_chart, 3, 3);
    s_prices = lv_chart_add_series(s_chart, lv_color_hex(0xF3C969), LV_CHART_AXIS_PRIMARY_Y);
    for (size_t i = 0; i < 3; i++) {
        s_y_ticks[i] = s_label(CHART_Y - 6 + (int)i * (CHART_HEIGHT - 6) / 2);
        lv_obj_set_x(s_y_ticks[i], 2);
        lv_obj_set_width(s_y_ticks[i], 44);
        lv_obj_set_style_text_align(s_y_ticks[i], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        s_x_ticks[i] = s_label(140);
        lv_obj_set_x(s_x_ticks[i], CHART_X + (int)i * (CHART_WIDTH - 60) / 2);
        lv_obj_set_width(s_x_ticks[i], 60);
        lv_obj_set_style_text_align(s_x_ticks[i], i == 0 ? LV_TEXT_ALIGN_LEFT :
                                   i == 1 ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }
    s_scale = s_label(90); // Only used for the empty/error state.
    s_cheapest = s_label(160);
    s_note = s_label(23); // A warning shares the current-price row when needed.
    lv_obj_set_x(s_note, 150);
    lv_obj_set_width(s_note, 162);
    lv_obj_set_style_text_align(s_note, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
}

static void s_comma(char *text)
{
    for (; *text; text++) if (*text == '.') *text = ',';
}

esp_err_t day_ahead_view_show(void)
{
    day_ahead_snapshot_t snapshot;
    day_ahead_snapshot(&snapshot);
    bool tomorrow = atomic_load(&s_tomorrow);
    time_t now = time(NULL);
    day_ahead_day_t expected;
    bool clock_ok = now >= 1735689600 && day_ahead_day_init(&expected, now, tomorrow ? 1 : 0);
    const day_ahead_day_t *day = &snapshot.days[0];
    if (clock_ok && snapshot.days[1].start == expected.start) day = &snapshot.days[1];
    bool has_day = clock_ok && day->start == expected.start && day->available > 0;
    esp_err_t err = display_show_report("", "", day_ahead_view_action_label(), 0x14324A);
    if (err != ESP_OK) return err;
    if (!bsp_display_lock(1000)) return ESP_ERR_TIMEOUT;
    if (!s_panel) s_build();
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    char text[160], date[16];
    struct tm local;
    if (clock_ok) {
        day_ahead_localtime(expected.start, &local);
        strftime(date, sizeof(date), "%d-%m", &local);
    } else snprintf(date, sizeof(date), "--");
    snprintf(text, sizeof(text), "Stroomprijzen %s %s", tomorrow ? "morgen" : "vandaag", date);
    lv_label_set_text(s_heading, text);
    int current = has_day ? day_ahead_current(day, now) : -1;
    if (current >= 0) snprintf(text, sizeof(text), "Nu %.2f ct/kWh", (double)day->eur_kwh[current] * 100);
    else snprintf(text, sizeof(text), "%s", has_day ? "Day-ahead - ct/kWh" : "Geen prijzen beschikbaar");
    s_comma(text);
    lv_label_set_text(s_now, text);
    for (size_t i = 0; i < 3; i++) {
        if (has_day) {
            lv_obj_remove_flag(s_y_ticks[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_x_ticks[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_y_ticks[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_x_ticks[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (has_day) {
        lv_obj_add_flag(s_scale, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_chart, LV_OBJ_FLAG_HIDDEN);
        lv_chart_set_point_count(s_chart, (uint32_t)day->count);
        int32_t low = INT32_MAX, high = INT32_MIN;
        for (size_t i = 0; i < day->count; i++) {
            int32_t value = day->present[i] ? (int32_t)lroundf(day->eur_kwh[i] * 10000) : LV_CHART_POINT_NONE;
            lv_chart_set_series_value_by_id(s_chart, s_prices, (uint32_t)i, value);
            if (day->present[i]) {
                if (value < low) low = value;
                if (value > high) high = value;
            }
        }
        // Use the day's actual range, including negative prices. Only a flat
        // day needs padding (0.5 ct/kWh either side) to avoid a zero-height axis.
        if (high == low) {
            low -= 50;
            high += 50;
        }
        lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, low, high);
        lv_chart_refresh(s_chart);
        for (size_t i = 0; i < 3; i++) {
            double value = ((double)high - ((double)high - low) * (double)i / 2) / 100;
            snprintf(text, sizeof(text), "%.1f", value);
            s_comma(text);
            lv_label_set_text(s_y_ticks[i], text);
            // Label actual plotted quarters, also on 23/25-hour clock-change days.
            size_t index = i == 0 ? 0 : i == 1 ? day->count / 2 : day->count - 1;
            time_t at = day->start + (time_t)index * DAY_AHEAD_QUARTER_SECONDS;
            day_ahead_localtime(at, &local);
            strftime(text, sizeof(text), "%H:%M", &local);
            lv_label_set_text(s_x_ticks[i], text);
        }
    } else {
        lv_obj_remove_flag(s_scale, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_chart, LV_OBJ_FLAG_HIDDEN);
        const char *message = "Nog niet gepubliceerd";
        if (!clock_ok || snapshot.state == DAY_AHEAD_WAIT_TIME) message = "Tijd synchroniseren...";
        else if (snapshot.state == DAY_AHEAD_WAIT_WIFI) message = "Wachten op wifi";
        else if (snapshot.state == DAY_AHEAD_LOADING) message = "Prijzen ophalen...";
        else if (snapshot.state == DAY_AHEAD_ERROR) message = "Ophalen mislukt; nieuwe poging volgt";
        lv_label_set_text(s_scale, message);
    }
    int best = has_day ? day_ahead_cheapest(day, now) : -1;
    if (best >= 0) {
        time_t start = day->start + best * DAY_AHEAD_QUARTER_SECONDS;
        day_ahead_localtime(start, &local);
        char when[20];
        strftime(when, sizeof(when), "%H:%M", &local);
        snprintf(text, sizeof(text), "Laagste %s %s: %.2f ct/kWh", when,
                 local.tm_isdst ? "CEST" : "CET", (double)day->eur_kwh[best] * 100);
        s_comma(text);
        lv_label_set_text(s_cheapest, text);
    } else lv_label_set_text(s_cheapest, "");
    const char *note = "";
    if (has_day && snapshot.state == DAY_AHEAD_WAIT_WIFI) note = "Offline (bewaard)";
    else if (has_day && snapshot.state == DAY_AHEAD_ERROR) note = "Update mislukt";
    else if (has_day && day->available < day->count) note = "Onvolledig";
    lv_label_set_text(s_note, note);
    bsp_display_unlock();
    return ESP_OK;
}
