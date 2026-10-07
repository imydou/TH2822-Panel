#include "ui.hpp"
#include "i18n.hpp"
#include "lvgl.h"
#include "traffic.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace th;
LV_FONT_DECLARE(panel_cjk_14);
LV_FONT_DECLARE(panel_cjk_20);
LV_FONT_DECLARE(panel_cjk_24);
struct FontFamily {
    const char *id;
    const lv_font_t *small, *normal, *heading;
};
static lv_font_t with_symbols(const lv_font_t &base, const lv_font_t *fallback) {
    auto font = base;
    font.fallback = fallback;
    return font;
}
static const lv_font_t latin14 = with_symbols(lv_font_montserrat_14, &panel_cjk_14);
static const lv_font_t latin20 = with_symbols(lv_font_montserrat_20, &panel_cjk_20);
static const lv_font_t latin24 = with_symbols(lv_font_montserrat_24, &panel_cjk_24);
static const FontFamily families[] = {{"latin", &latin14, &latin20, &latin24},
                                      {"noto_cjk", &panel_cjk_14, &panel_cjk_20, &panel_cjk_24}};
static const FontFamily &fonts() {
    for (auto &f : families)
        if (!strcmp(f.id, i18n::current().font))
            return f;
    return families[0];
}
static const char *tr(const char *key) {
    return i18n::text(key);
}
static std::string fmt(const char *key, const std::string &value) {
    return i18n::format(key, {{"value", value}});
}
static std::function<void(Action)> send_action;
static State view;
static std::string previous;
static bool main_page = false;
static lv_obj_t *title, *status, *main_value, *sub_value, *main_name, *sub_name, *message,
    *hold_label, *tol_button, *tol_badge, *tol_result_label, *mode_buttons[2], *reset_button;
static lv_obj_t *control[6], *control_text[6], *control_lock[6], *modal = nullptr;
static lv_obj_t *tol_status = nullptr, *tol_values = nullptr, *tol_capture = nullptr,
                *tol_ranges[4] = {};
static bool tolerance_dialog = false, capture_dialog = false, logs_dialog = false,
            recording_dialog = false, fault_dialog = false;
static bool fault_latched = false;
static std::string fault_reason;
static lv_obj_t *fault_detail, *fault_retry, *connection_retry;
static lv_obj_t *rec_status, *rec_toggle, *rec_hint, *rec_tabs[4], *rec_update;
static lv_obj_t *rec_primary, *rec_secondary, *rec_primary_name, *rec_secondary_name, *rec_stamp;
static const RecordingView rec_views[] = {RecordingView::Present, RecordingView::Maximum,
                                          RecordingView::Average, RecordingView::Minimum};
static const char *rec_keys[] = {"rec.present", "rec.max", "rec.avg", "rec.min"};
static const char *rec_actions[] = {"present", "max", "avg", "min"};
static lv_obj_t *capture_value, *capture_confirm, *logs_body, *logs_text, *logs_follow_button;
static bool logs_follow = true, logs_scrolling = false;
static uint64_t logs_rendered = 0, logs_cleared = 0;
static void refresh_logs();
static void instrument_info(lv_event_t *);
static bool ui_pending = false;
static lv_obj_t *activity_dot;
static TrafficActivity activity_seen;
static uint32_t setting_tick = 0, query_tick = 0;
static bool setting_pulse = false, query_pulse = false;
static void refresh_activity() {
    if (!main_page)
        return;
    const auto current = traffic_log().activity();
    const auto now = lv_tick_get();
    if (current.setting != activity_seen.setting) {
        setting_tick = now;
        setting_pulse = true;
    }
    if (current.query != activity_seen.query) {
        query_tick = now;
        query_pulse = true;
    }
    activity_seen = current;
    setting_pulse = setting_pulse && uint32_t(now - setting_tick) < 150;
    query_pulse = query_pulse && uint32_t(now - query_tick) < 150;
    // Latch a short setter pulse even if its readback arrives between UI ticks.
    const auto color = lv_color_hex(setting_pulse ? 0xff5364 : query_pulse ? 0x529bff : 0x35435a);
    // LVGL 8 refreshes the style even if its value is unchanged. On the RGB full-refresh
    // display, avoid requesting a full redraw on every UI timer tick for an unchanged dot.
    if (lv_obj_get_style_bg_color(activity_dot, 0).full != color.full)
        lv_obj_set_style_bg_color(activity_dot, color, 0);
}
static constexpr uint32_t BG = 0x0b1423, CARD = 0x162338, INK = 0xecf4ff, MUTED = 0x99aecb,
                          ACCENT = 0x40dfc8;
static void set_text(lv_obj_t *o, const std::string &s) {
    if (s != lv_label_get_text(o))
        lv_label_set_text(o, s.c_str());
}
static std::string secondary_name(const std::string &value) {
    return value == "NULL" ? tr("reading.none") : value == "THETA" ? tr("value.theta") : value;
}
static void fit_reading(lv_obj_t *object, int width, bool primary) {
    const lv_font_t *sizes[] = {&lv_font_montserrat_48, &lv_font_montserrat_32,
                                &lv_font_montserrat_24, &lv_font_montserrat_20,
                                &lv_font_montserrat_14};
    const lv_font_t *font = sizes[4];
    for (int i = primary ? 0 : 1; i < 5; ++i) {
        lv_point_t text_size;
        lv_txt_get_size(&text_size, lv_label_get_text(object), sizes[i], 0, 0, LV_COORD_MAX,
                        LV_TEXT_FLAG_NONE);
        if (text_size.x <= width) {
            font = sizes[i];
            break;
        }
    }
    lv_obj_set_style_text_font(object, font, 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
}
static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w,
                       const lv_font_t *font, uint32_t color) {
    auto *o = lv_label_create(parent);
    lv_label_set_text(o, text);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    return o;
}
static lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h) {
    auto *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(CARD), 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 18, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static lv_obj_t *button(lv_obj_t *p, const char *text, int x, int y, int w, int h, lv_event_cb_t cb,
                        void *data = nullptr) {
    auto *o = lv_btn_create(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x23354f), 0);
    lv_obj_set_style_radius(o, 12, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, data);
    auto *t = lv_label_create(o);
    lv_label_set_text(t, text);
    lv_obj_set_style_text_font(t, fonts().normal, 0);
    lv_obj_center(t);
    return o;
}
static void close_modal() {
    tolerance_dialog = capture_dialog = logs_dialog = recording_dialog = fault_dialog = false;
    if (modal) {
        lv_obj_del(modal);
        modal = nullptr;
    }
}
// A full-screen input shield prevents taps reaching controls behind a dialog.
static lv_obj_t *open_modal(int x, int y, int w, int h) {
    close_modal();
    modal = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(modal, 0, 0);
    lv_obj_set_size(modal, 800, 480);
    lv_obj_set_style_pad_all(modal, 0, 0);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_radius(modal, 0, 0);
    lv_obj_set_style_bg_color(modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_60, 0);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    return card(modal, x, y, w, h);
}
static bool is_current(const Action &a) {
    switch (a.type) {
    case ActionType::Primary:
        return a.value == view.primary;
    case ActionType::Secondary:
        return a.value == view.secondary;
    case ActionType::Equivalent:
        return a.value == view.equivalent;
    case ActionType::Frequency:
        return a.value == std::to_string(view.hz);
    case ActionType::Level:
        return std::abs(std::strtod(a.value.c_str(), nullptr) - view.level) < 1e-6;
    case ActionType::PollInterval:
        return a.value == std::to_string(view.poll_ms);
    case ActionType::Language:
        return a.value == view.locale;
    default:
        return false;
    }
}
static void choose(lv_event_t *e) {
    auto *a = (Action *)lv_event_get_user_data(e);
    Action copy = *a;
    const bool unchanged = is_current(copy);
    close_modal();
    // Rewriting an unchanged primary/frequency can disable native AUTO on the meter.
    if (unchanged || (copy.type != ActionType::Language && !view.ready))
        return;
    send_action(copy);
}
static void cancel(lv_event_t *) {
    close_modal();
}
struct Choice {
    std::string value, label;
    bool enabled = true;
};
static void menu(ActionType type, const char *heading, const std::vector<Choice> &items,
                 bool native_names = false) {
    auto *panel = open_modal(80, 24, 640, 430);
    label(panel, tr(heading), 22, 19, 490, fonts().normal, INK);
    button(panel, tr("button.cancel"), 532, 8, 90, 48, cancel);
    auto *list = lv_obj_create(panel);
    lv_obj_set_pos(list, 16, 66);
    lv_obj_set_size(list, 608, 304);
    lv_obj_set_style_bg_opa(list, 0, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    static std::vector<Action> actions;
    actions.clear();
    actions.reserve(items.size());
    for (auto &i : items)
        actions.push_back({type, i.value});
    for (size_t i = 0; i < items.size(); ++i) {
        auto *b = button(list, items[i].label.c_str(), (int)(i % 2) * 306, (int)(i / 2) * 98, 294,
                         88, choose, &actions[i]);
        auto *text = lv_obj_get_child(b, 0);
        lv_obj_set_width(text, 266);
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(text, LV_ALIGN_CENTER, 0, 8);
        if (native_names)
            lv_obj_set_style_text_font(text, &panel_cjk_20, 0);
        if (!items[i].enabled) {
            lv_obj_add_state(b, LV_STATE_DISABLED);
            auto *note = label(b, tr("choice.unavailable"), 0, 0, 266, fonts().small, MUTED);
            lv_obj_align(note, LV_ALIGN_TOP_MID, 0, -8);
            lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
        } else if (is_current(actions[i])) {
            lv_obj_set_style_bg_color(b, lv_color_hex(0x1c4c50), 0);
            lv_obj_set_style_border_width(b, 2, 0);
            lv_obj_set_style_border_color(b, lv_color_hex(ACCENT), 0);
            auto *note = label(b, tr("choice.current"), 0, 0, 266, fonts().small, ACCENT);
            lv_obj_align(note, LV_ALIGN_TOP_MID, 0, -8);
            lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
        }
    }
    label(panel, tr(type == ActionType::Language ? "choice.language_hint" : "choice.hint"), 22, 385,
          596, fonts().small, MUTED);
}

static void controls(lv_event_t *e) {
    auto i = (intptr_t)lv_event_get_user_data(e);
    bool ac = view.primary != "DCR";
    if (ui_pending || !view.ready)
        return;
    const bool rec_locked = view.recording.known && view.recording.enabled && i < 5;
    if (rec_locked || (view.tolerance.known && view.tolerance.enabled &&
                       (i == 0 || i == 1 || i == 2 || i == 4))) {
        auto *p = open_modal(80, 116, 640, 248);
        label(p, tr(rec_locked ? "error.rec_locked" : "error.tol_locked"), 24, 40, 592,
              fonts().heading, INK);
        button(p, tr("button.cancel"), 220, 162, 200, 56, cancel);
        return;
    }
    if (i == 0)
        menu(ActionType::Primary, "menu.mode",
             {{"L", "L"},
              {"C", "C"},
              {"R", "R"},
              {"Z", "Z"},
              {"DCR", "DCR", profile(view.model).dcr}});
    if (i == 1)
        menu(ActionType::Frequency, "menu.freq",
             {{"100", "100 Hz", ac},
              {"120", "120 Hz", ac},
              {"1000", "1 kHz", ac},
              {"10000", "10 kHz", ac && valid_frequency(view.model, 10000)},
              {"100000", "100 kHz", ac && profile(view.model).max_hz == 100000}});
    if (i == 2)
        menu(ActionType::Level, "menu.level", {{"0.3", "0.3 V"}, {"0.6", "0.6 V"}, {"1", "1 V"}});
    if (i == 3)
        menu(ActionType::Equivalent, "menu.circuit",
             {{"SER", tr("value.series")}, {"PAL", tr("value.parallel")}});
    if (i == 4)
        menu(ActionType::Secondary, "menu.second",
             {{"D", "D"}, {"Q", "Q"}, {"THETA", tr("value.theta")}, {"ESR", "ESR"}});
    if (i == 5)
        menu(ActionType::PollInterval, "menu.source",
             {{"250", "250 ms"},
              {"333", "333 ms"},
              {"500", tr("value.query_medium")},
              {"667", "667 ms"},
              {"1000", "1000 ms"}});
}
static std::string percent_text(Value value) {
    if (value.status != Value::Valid)
        return value.status == Value::Overrange ? "OL" : "--";
    char text[48];
    std::snprintf(text, sizeof(text), "%+.5g %%", value.number);
    return text;
}
static void enable_button(lv_obj_t *button, bool enabled) {
    if (enabled)
        lv_obj_clear_state(button, LV_STATE_DISABLED);
    else
        lv_obj_add_state(button, LV_STATE_DISABLED);
}
static void update_tolerance_dialog() {
    if (!tolerance_dialog)
        return;
    const auto &t = view.tolerance;
    const bool usable = view.ready && t.known && !ui_pending;
    set_text(tol_status, view.error.empty()
                             ? std::string(tr(!t.known    ? "tol.unknown"
                                              : t.enabled ? "tol.on"
                                                          : "tol.off"))
                             : i18n::format(view.error.c_str(), {{"detail", view.error_detail}}));
    lv_obj_set_style_text_color(tol_status, lv_color_hex(view.error.empty() ? ACCENT : 0xffbf69),
                                0);
    const auto range = t.percent ? fmt("tol.range", std::to_string(t.percent)) : tr("tol.no_range");

    set_text(
        tol_values,
        fmt("tol.nominal",
            t.known && t.enabled ? format_value(t.nominal, primary_unit(view.primary)) : "--") +
            "\n" + fmt("tol.deviation", usable && t.enabled ? percent_text(t.deviation) : "--") +
            "\n" + (t.known && t.enabled ? range : "--"));
    enable_button(tol_capture, usable && t.enabled && !view.hold &&
                                   view.reading.primary.status == Value::Valid &&
                                   view.reading.primary.number != 0);
    const int values[] = {1, 5, 10, 20};
    for (int i = 0; i < 4; ++i) {
        enable_button(tol_ranges[i], usable && t.enabled);
        lv_obj_set_style_bg_color(
            tol_ranges[i],
            lv_color_hex(t.known && t.enabled && t.percent == values[i] ? 0x1c6c60 : 0x23354f), 0);
    }
}
static void tolerance_menu(lv_event_t *);
static ActionType capture_action;
static void confirm_capture(lv_event_t *) {
    if (!view.ready || ui_pending || view.hold || view.reading.primary.status != Value::Valid ||
        view.reading.primary.number == 0)
        return;
    const auto action = capture_action;
    close_modal();
    send_action({action, ""});
}
static void open_capture(ActionType action) {
    capture_action = action;
    auto *p = open_modal(48, 32, 704, 416);
    label(p, tr("tol.capture_title"), 24, 22, 650, fonts().heading, INK);
    label(p, tr("tol.capture_note"), 24, 84, 650, fonts().normal, MUTED);
    capture_dialog = true;
    capture_value =
        label(p,
              (view.primary + "  " + format_value(view.reading.primary, primary_unit(view.primary)))
                  .c_str(),
              24, 230, 650, fonts().heading, ACCENT);
    button(p, tr("tol.back"), 24, 330, 220, 60, cancel);
    capture_confirm = button(p, tr("tol.confirm"), 310, 330, 370, 60, confirm_capture);
}
static void request_capture(lv_event_t *) {
    if (!ui_pending && view.ready)
        open_capture(ActionType::ToleranceCapture);
}
static void switch_mode(lv_event_t *e) {
    if (!view.ready || ui_pending || !view.tolerance.known)
        return;
    const bool comparison = (intptr_t)lv_event_get_user_data(e) == 1;
    if (comparison == view.tolerance.enabled)
        return;
    if (comparison && view.recording.known && view.recording.enabled) {
        auto *p = open_modal(80, 116, 640, 248);
        label(p, tr("error.rec_locked"), 24, 40, 592, fonts().heading, INK);
        button(p, tr("button.cancel"), 220, 162, 200, 56, cancel);
        return;
    }
    if (comparison)
        open_capture(ActionType::ToleranceEnable);
    else
        send_action({ActionType::ToleranceDisable, ""});
}
static void tolerance_range(lv_event_t *e) {
    const int percent = (int)(intptr_t)lv_event_get_user_data(e);
    if (!view.ready || ui_pending || !view.tolerance.known || !view.tolerance.enabled ||
        view.tolerance.percent == percent)
        return;
    send_action({ActionType::ToleranceRange, std::to_string(percent)});
}
static void tolerance_menu(lv_event_t *) {
    auto *p = open_modal(32, 16, 736, 448);
    tolerance_dialog = true;
    label(p, tr("tol.title"), 20, 18, 598, fonts().heading, INK);
    button(p, tr("button.cancel"), 624, 8, 94, 48, cancel);
    tol_status = label(p, "", 24, 65, 688, fonts().normal, ACCENT);
    lv_obj_set_height(tol_status, 40);
    lv_label_set_long_mode(tol_status, LV_LABEL_LONG_DOT);
    tol_values = label(p, "", 24, 112, 688, fonts().normal, INK);
    const int values[] = {1, 5, 10, 20};
    for (int i = 0; i < 4; ++i)
        tol_ranges[i] = button(p, (std::to_string(values[i]) + "%").c_str(), 24 + i * 174, 221, 164,
                               57, tolerance_range, (void *)(intptr_t)values[i]);
    tol_capture = button(p, tr("tol.capture"), 24, 302, 688, 60, request_capture);
    label(p, tr("tol.hint"), 24, 367, 688, fonts().small, MUTED);
    update_tolerance_dialog();
    if (view.ready && !view.tolerance.monitored)
        send_action({ActionType::ToleranceInspect, ""});
}
static void hold(lv_event_t *) {
    send_action({ActionType::Hold, ""});
}
static void update_recording_dialog() {
    if (!recording_dialog)
        return;
    const auto &r = view.recording;
    const bool active = r.known && r.enabled;
    const bool usable = view.ready && active && !ui_pending;
    const bool current = r.view == RecordingView::Present;
    set_text(rec_status, !view.error.empty()
                             ? i18n::format(view.error.c_str(), {{"detail", view.error_detail}})
                             : tr(ui_pending   ? "status.pending"
                                  : !r.known   ? "rec.unknown"
                                  : !r.enabled ? "rec.off"
                                  : r.live     ? "rec.live"
                                               : "rec.snapshot"));
    for (int i = 0; i < 4; ++i) {
        enable_button(rec_tabs[i], usable);
        lv_obj_set_style_bg_color(
            rec_tabs[i], lv_color_hex(active && r.view == rec_views[i] ? 0x1c6c60 : 0x23354f), 0);
    }
    set_text(rec_primary_name, fmt("reading.primary", view.primary));
    set_text(rec_secondary_name, fmt("reading.secondary", secondary_name(view.secondary)));
    set_text(rec_primary, active && !ui_pending
                              ? format_value(r.value.primary, primary_unit(view.primary))
                              : "--");
    set_text(rec_secondary, active && current && !ui_pending
                                ? format_value(r.value.secondary, secondary_unit(view.secondary))
                                : "--");
    // Native secondary extrema/average return zero on the measured instrument; do not label
    // those fields as valid statistics. Present retains the actual secondary reading.
    for (auto *o : {rec_secondary_name, rec_secondary}) {
        if (active && current && view.secondary != "NULL")
            lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_width(rec_primary, current ? 360 : 688);
    fit_reading(rec_primary, current ? 360 : 688, true);
    fit_reading(rec_secondary, 300, false);
    char time[40];
    std::snprintf(time, sizeof(time), "%02llu:%02llu:%02llu",
                  (unsigned long long)(r.read_at_ms / 3600000),
                  (unsigned long long)(r.read_at_ms / 60000 % 60),
                  (unsigned long long)(r.read_at_ms / 1000 % 60));
    set_text(rec_stamp, !active || ui_pending ? ""
                        : r.live              ? ""
                        : r.read_at_ms        ? fmt("rec.read_at", time)
                                              : "");
    set_text(rec_hint, tr(active && !r.live ? "rec.snapshot_hint" : "rec.hint"));
    set_text(lv_obj_get_child(rec_toggle, 0), tr(r.enabled ? "rec.stop" : "rec.start"));
    enable_button(rec_toggle, view.ready && r.known && !ui_pending &&
                                  (r.enabled || (view.tolerance.known && !view.tolerance.enabled)));
    if (active && !r.live && r.view != RecordingView::Unknown) {
        lv_obj_clear_flag(rec_update, LV_OBJ_FLAG_HIDDEN);
        enable_button(rec_update, usable);
        lv_obj_set_x(rec_toggle, 376);
        lv_obj_set_width(rec_toggle, 336);
    } else {
        lv_obj_add_flag(rec_update, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(rec_toggle, 24);
        lv_obj_set_width(rec_toggle, 688);
    }
}
static void recording_toggle(lv_event_t *) {
    if (!view.ready || ui_pending || !view.recording.known)
        return;
    send_action(
        {view.recording.enabled ? ActionType::RecordingDisable : ActionType::RecordingEnable, ""});
}
static void recording_select(lv_event_t *event) {
    const auto i = (int)(intptr_t)lv_event_get_user_data(event);
    if (!view.ready || ui_pending || !view.recording.known || !view.recording.enabled ||
        view.recording.view == rec_views[i])
        return;
    send_action({ActionType::RecordingSelect, rec_actions[i]});
}
static void recording_update(lv_event_t *) {
    if (view.ready && !ui_pending && view.recording.known && view.recording.enabled &&
        !view.recording.live)
        send_action({ActionType::RecordingUpdate, ""});
}
static void recording_menu(lv_event_t *event) {
    auto *p = open_modal(32, 16, 736, 448);
    recording_dialog = true;
    label(p, tr("rec.title"), 24, 20, 576, fonts().heading, INK);
    button(p, tr("button.cancel"), 624, 8, 94, 48, cancel);
    rec_status = label(p, "", 24, 68, 688, fonts().normal, ACCENT);
    lv_label_set_long_mode(rec_status, LV_LABEL_LONG_DOT);
    for (int i = 0; i < 4; ++i)
        rec_tabs[i] = button(p, tr(rec_keys[i]), 24 + i * 174, 108, 166, 52, recording_select,
                             (void *)(intptr_t)i);
    rec_primary_name = label(p, "", 24, 186, 360, fonts().normal, MUTED);
    rec_secondary_name = label(p, "", 412, 186, 300, fonts().normal, MUTED);
    rec_primary = label(p, "--", 24, 222, 360, &lv_font_montserrat_48, INK);
    rec_secondary = label(p, "--", 412, 230, 300, &lv_font_montserrat_32, ACCENT);
    rec_stamp = label(p, "", 24, 289, 688, fonts().small, MUTED);
    rec_hint = label(p, "", 24, 326, 688, fonts().small, MUTED);
    rec_update = button(p, tr("rec.update"), 24, 376, 336, 52, recording_update);
    rec_toggle = button(p, tr("rec.start"), 24, 376, 688, 52, recording_toggle);
    update_recording_dialog();
    if (view.ready && event)
        send_action({ActionType::RecordingInspect, ""});
}
static void reconnect(lv_event_t *) {
    send_action({ActionType::Reconnect, ""});
}
static void retry_fault(lv_event_t *) {
    if (!ui_pending && (!view.connected || !view.ready)) {
        if (!fault_dialog)
            close_modal();
        send_action({view.connected ? ActionType::Resync : ActionType::Reconnect, ""});
    }
}
static void update_fault_dialog() {
    if (!fault_latched)
        return;
    if (!fault_dialog) {
        auto *p = open_modal(80, 60, 640, 360);
        fault_dialog = true;
        label(p, tr("connection.error_title"), 24, 22, 592, fonts().heading, INK);
        fault_detail = label(p, "", 24, 94, 592, fonts().normal, 0xffbf69);
        lv_obj_set_height(fault_detail, 130);
        lv_label_set_long_mode(fault_detail, LV_LABEL_LONG_DOT);
        fault_retry = button(p, tr("button.sync"), 24, 278, 592, 56, retry_fault);
    }
    set_text(fault_detail, ui_pending ? tr("connection.reading") : fault_reason);
    enable_button(fault_retry, !ui_pending);
}
static void logs_scrolled(lv_event_t *) {
    if (!logs_scrolling && lv_indev_get_act()) {
        logs_follow = false;
        set_text(lv_obj_get_child(logs_follow_button, 0), tr("logs.latest"));
    }
}
static void logs_latest(lv_event_t *) {
    logs_follow = !logs_follow;
    logs_rendered = 0;
    set_text(lv_obj_get_child(logs_follow_button, 0),
             tr(logs_follow ? "logs.pause" : "logs.latest"));
    refresh_logs();
}
static void logs_clear(lv_event_t *) {
    logs_cleared = traffic_log().latest();
    logs_rendered = 0;
    lv_label_set_text(logs_text, tr("logs.empty"));
    refresh_logs();
}
static void refresh_logs() {
    if (!logs_dialog || !logs_follow)
        return;
    const auto latest = traffic_log().latest();
    if (latest == logs_rendered)
        return;
    std::string text;
    for (const auto &e : traffic_log().snapshot()) {
        if (e.sequence <= logs_cleared)
            continue;
        char prefix[80];
        const auto ms = e.milliseconds;
        std::snprintf(prefix, sizeof(prefix), "#%s %02u:%02u:%02u.%03u %s  ",
                      e.direction == 'E'   ? "ffbf69"
                      : e.direction == 'T' ? "40dfc8"
                                           : "ecf4ff",
                      unsigned(ms / 3600000), unsigned(ms / 60000 % 60), unsigned(ms / 1000 % 60),
                      unsigned(ms % 1000),
                      e.direction == 'T'   ? "TX"
                      : e.direction == 'R' ? "RX"
                                           : "ERR");
        text += std::string(prefix) + e.text.data() + "#\n";
    }
    logs_scrolling = true;
    lv_obj_update_layout(logs_body);
    set_text(logs_text, text.empty() ? tr("logs.empty") : text);
    lv_obj_update_layout(logs_body);
    lv_obj_scroll_to_y(logs_body, LV_COORD_MAX, LV_ANIM_OFF);
    logs_scrolling = false;
    logs_rendered = latest;
}
static void logs_back(lv_event_t *e) {
    if (view.connected)
        instrument_info(e);
    else
        close_modal();
}
static void communication_log(lv_event_t *) {
    auto *p = open_modal(0, 0, 800, 480);
    logs_dialog = true;
    logs_follow = true;
    logs_rendered = 0;
    label(p, tr("logs.title"), 24, 20, 560, fonts().heading, INK);
    button(p, tr("logs.back"), 664, 12, 112, 48, logs_back);
    logs_body = lv_obj_create(p);
    lv_obj_set_pos(logs_body, 24, 76);
    lv_obj_set_size(logs_body, 752, 308);
    lv_obj_set_style_bg_color(logs_body, lv_color_hex(BG), 0);
    lv_obj_set_style_border_width(logs_body, 0, 0);
    lv_obj_set_style_pad_all(logs_body, 10, 0);
    lv_obj_set_scroll_dir(logs_body, LV_DIR_VER);
    lv_obj_add_event_cb(logs_body, logs_scrolled, LV_EVENT_SCROLL_BEGIN, nullptr);
    logs_text = label(logs_body, tr("logs.empty"), 0, 0, 720, fonts().small, INK);
    lv_label_set_recolor(logs_text, true);
    logs_follow_button = button(p, tr("logs.pause"), 24, 405, 212, 52, logs_latest);
    button(p, tr("logs.clear"), 250, 405, 212, 52, logs_clear);
    label(p, tr("logs.hint"), 484, 414, 292, fonts().small, MUTED);
    refresh_logs();
}
static void languages(lv_event_t *);
static void instrument_info(lv_event_t *) {
    auto *panel = open_modal(80, 36, 640, 408);
    label(panel, tr("menu.instrument"), 24, 22, 352, fonts().heading, INK);
    button(panel, tr("button.cancel"), 518, 10, 98, 48, cancel);
    std::string text = fmt("info.model", profile(view.model).name) + "\n\n" +
                       fmt("info.firmware", view.firmware) + "\n\n" +
                       fmt("info.serial", view.serial);
    label(panel, text.c_str(), 28, 92, 584, fonts().normal, INK);
    button(panel, tr("button.language"), 24, 326, 180, 56, languages);
    button(panel, tr("logs.title"), 220, 326, 396, 56, communication_log);
}
static void languages(lv_event_t *) {
    std::vector<Choice> choices;
    for (size_t i = 0; i < i18n::locale_count; ++i)
        choices.push_back({i18n::locales[i].id, i18n::locales[i].name});
    menu(ActionType::Language, "menu.language", choices, true);
}
void panel_ui_create(std::function<void(Action)> send) {
    send_action = send;
    modal = nullptr;
    tolerance_dialog = capture_dialog = logs_dialog = recording_dialog = fault_dialog = false;
    previous.clear();
    main_page = view.connected;
    auto *root = lv_scr_act();
    lv_obj_clean(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(BG), 0);
    lv_obj_set_style_text_color(root, lv_color_hex(INK), 0);
    lv_obj_set_style_text_font(root, fonts().normal, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    if (!main_page) {
        label(root, "TH2822", 28, 20, 400, fonts().heading, INK);
        button(root, tr("button.language"), 664, 12, 112, 44, languages);
        auto *waiting = card(root, 80, 108, 640, 300);
        auto *badge = card(waiting, 32, 28, 72, 72);
        lv_obj_set_style_bg_color(badge, lv_color_hex(0x20394a), 0);
        auto *usb = label(badge, "USB", 0, 0, 72, &lv_font_montserrat_24, ACCENT);
        lv_obj_set_style_text_align(usb, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(usb);
        title = label(waiting, tr("connection.title"), 128, 26, 480, fonts().heading, INK);
        status = label(waiting, tr("connection.idle"), 128, 66, 480, fonts().normal, MUTED);
        message = label(waiting, tr("hint.offline"), 32, 130, 576, fonts().normal, MUTED);
        lv_obj_set_height(message, 68);
        lv_label_set_long_mode(message, LV_LABEL_LONG_DOT);
        connection_retry = button(waiting, tr("connection.retry"), 32, 216, 280, 52, reconnect);
        button(waiting, tr("logs.title"), 328, 216, 280, 52, communication_log);
        return;
    }
    title = label(root, "TH2822", 24, 21, 140, fonts().heading, INK);
    status = nullptr;
    activity_dot = card(root, 178, 31, 10, 10);
    lv_obj_set_style_radius(activity_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(activity_dot, lv_color_hex(0x35435a), 0);
    lv_obj_clear_flag(activity_dot, LV_OBJ_FLAG_CLICKABLE);
    activity_seen = traffic_log().activity();
    setting_pulse = query_pulse = false;
    mode_buttons[0] = button(root, tr("mode.measure"), 230, 12, 166, 44, switch_mode, (void *)0);
    mode_buttons[1] = button(root, tr("mode.compare"), 404, 12, 166, 44, switch_mode, (void *)1);
    lv_point_t info_size;
    lv_txt_get_size(&info_size, tr("button.info"), fonts().normal, 0, 0, LV_COORD_MAX,
                    LV_TEXT_FLAG_NONE);
    const int info_width = info_size.x + 32;
    button(root, tr("button.info"), 776 - info_width, 12, info_width, 44, instrument_info);
    lv_obj_t *b;
    auto *left = card(root, 24, 88, 440, 165);
    main_name = label(left, "", 20, 17, 400, fonts().normal, MUTED);
    main_value = label(left, "--", 20, 58, 400, &lv_font_montserrat_48, INK);
    tol_badge = label(left, "", 20, 135, 400, fonts().small, ACCENT);
    auto *right = card(root, 480, 88, 296, 165);
    sub_name = label(right, "", 18, 17, 260, fonts().normal, MUTED);
    sub_value = label(right, "--", 18, 63, 260, &lv_font_montserrat_32, ACCENT);
    tol_result_label = label(right, "", 18, 121, 260, fonts().normal, MUTED);
    for (int i = 0; i < 6; ++i) {
        int col = i % 3, row = i / 3;
        control[i] = button(root, "--", i == 5 ? 24 : 24 + col * 256, i == 5 ? 391 : 270 + row * 57,
                            240, 49, controls, (void *)(intptr_t)i);
        control_text[i] = lv_obj_get_child(control[i], 0);
        lv_obj_set_style_text_align(control_text[i], LV_TEXT_ALIGN_CENTER, 0);
        control_lock[i] = lv_obj_create(control[i]);
        lv_obj_set_pos(control_lock[i], 206, 14);
        lv_obj_set_size(control_lock[i], 16, 20);
        lv_obj_align(control_lock[i], LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_pad_all(control_lock[i], 0, 0);
        lv_obj_set_style_bg_opa(control_lock[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(control_lock[i], 0, 0);
        lv_obj_clear_flag(control_lock[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        auto *shackle = card(control_lock[i], 3, 0, 10, 13);
        lv_obj_set_style_bg_opa(shackle, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(shackle, 5, 0);
        lv_obj_set_style_border_width(shackle, 2, 0);
        lv_obj_set_style_border_color(shackle, lv_color_hex(MUTED), 0);
        lv_obj_clear_flag(shackle, LV_OBJ_FLAG_CLICKABLE);
        auto *body = card(control_lock[i], 1, 8, 14, 10);
        lv_obj_set_style_bg_color(body, lv_color_hex(MUTED), 0);
        lv_obj_set_style_radius(body, 2, 0);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(control_lock[i], LV_OBJ_FLAG_HIDDEN);
    }
    b = button(root, tr("button.hold"), 536, 391, 240, 49, hold);
    hold_label = lv_obj_get_child(b, 0);
    reset_button = button(root, tr("rec.button"), 280, 391, 240, 49, recording_menu);
    tol_button = button(root, tr("button.tolerance"), 280, 391, 240, 49, tolerance_menu);
    message = label(root, tr("hint.offline"), 24, 453, 752, fonts().small, MUTED);
    lv_label_set_long_mode(message, LV_LABEL_LONG_DOT);
}
void panel_ui_update(const State &s, bool pending) {
    const bool lost = view.connected && view.ready && (!s.connected || !s.ready);
    const bool failed = s.connected && !s.ready && !s.error.empty();
    if ((lost && !s.connected) || (!pending && (lost || failed))) {
        fault_latched = true;
        fault_reason = s.error.empty()
                           ? tr("usb.unplugged")
                           : i18n::format(s.error.c_str(), {{"detail", s.error_detail}});
    }
    if (s.connected && s.ready && !pending) {
        fault_latched = false;
        fault_reason.clear();
        if (fault_dialog)
            close_modal();
    }
    const bool enter_recording = s.connected && s.recording.known && s.recording.enabled &&
                                 (!main_page || !view.recording.known || !view.recording.enabled);
    ui_pending = pending;
    refresh_logs();
    const bool locale_changed = s.locale != i18n::current().id && i18n::set_locale(s.locale);
    if (locale_changed || s.connected != main_page) {
        view = s;
        panel_ui_create(send_action); // Drops open dialogs on disconnect; no main controls remain.
    }
    refresh_activity();
    const std::string signature =
        s.locale + s.identity + s.primary + s.secondary + s.equivalent + s.error + s.error_detail +
        std::to_string((int)s.model) + std::to_string(s.hz) + std::to_string(s.level) +
        std::to_string(s.connected) + std::to_string(s.ready) + std::to_string((int)s.phase) +
        std::to_string(s.hold) + std::to_string(s.poll_ms) + std::to_string(s.sample_sequence) +
        std::to_string(s.recording.revision) + std::to_string(s.recording.known) +
        std::to_string(s.recording.enabled) + std::to_string(pending) +
        std::to_string(s.tolerance.monitored) + std::to_string(s.tolerance.known) +
        std::to_string(s.tolerance.enabled) + std::to_string(s.tolerance.percent) +
        format_value(s.tolerance.nominal, "") + percent_text(s.tolerance.deviation);
    if (signature == previous)
        return;
    previous = signature;
    view = s;
    update_fault_dialog();
    if (enter_recording && s.ready && !fault_dialog && !recording_dialog)
        recording_menu(nullptr);
    if (capture_dialog) {
        set_text(capture_value,
                 s.primary + "  " + format_value(s.reading.primary, primary_unit(s.primary)));
        enable_button(capture_confirm, s.ready && !pending && !s.hold &&
                                           s.reading.primary.status == Value::Valid &&
                                           s.reading.primary.number != 0);
    }
    auto model = s.model == Model::Unknown ? tr("model.unknown") : profile(s.model).name;
    set_text(title,
             main_page ? i18n::format("app.title", {{"model", model}}) : tr("connection.title"));
    if (main_page) {
        lv_point_t model_size;
        lv_txt_get_size(&model_size, lv_label_get_text(title), fonts().heading, 0, 0, LV_COORD_MAX,
                        LV_TEXT_FLAG_NONE);
        lv_obj_set_x(activity_dot, 24 + model_size.x + 10);
    }
    const char *key = "status.wait";
    if (pending)
        key = "status.pending";
    else if (s.phase == ConnectionPhase::Unsupported)
        key = "status.unsupported";
    else if (s.phase == ConnectionPhase::Identifying)
        key = "status.identifying";
    else if (s.phase == ConnectionPhase::SerialReady)
        key = "status.serial";
    else if (s.phase == ConnectionPhase::UsbEnumerated)
        key = "status.enumerated";
    else if (s.connected && !s.ready)
        key = "status.locked";
    else if (s.ready)
        key = "status.query";
    if (status) {
        set_text(status, tr(key));
        lv_obj_set_style_text_color(status, lv_color_hex(MUTED), 0);
    }
    lv_obj_set_style_text_color(title, lv_color_hex(INK), 0);
    if (!main_page) {
        const bool expected = s.error.empty() || s.error == "usb.waiting" ||
                              s.error == "usb.closed" || s.error == "usb.reconnect" ||
                              s.error == "usb.unplugged" || s.error == "usb.cp210x";
        const bool failed = !expected;
        const bool identifying =
            s.phase == ConnectionPhase::Identifying || s.phase == ConnectionPhase::SerialReady;
        const bool probing = pending || identifying || s.phase == ConnectionPhase::UsbEnumerated;
        set_text(status, tr(s.phase == ConnectionPhase::Unsupported ? "connection.unsupported"
                            : failed                                ? "connection.failed"
                            : identifying                           ? "connection.identifying"
                            : probing                               ? "connection.connecting"
                                                                    : "connection.idle"));
        lv_obj_set_style_text_color(status,
                                    lv_color_hex(failed    ? 0xffbf69
                                                 : probing ? ACCENT
                                                           : MUTED),
                                    0);
        set_text(message, failed ? i18n::format(s.error.c_str(), {{"detail", s.error_detail}})
                                 : tr(probing ? "connection.keep_connected" : "hint.offline"));
        lv_obj_set_style_text_color(message, lv_color_hex(failed ? 0xffbf69 : MUTED), 0);
        enable_button(connection_retry, !pending && (!probing || failed));
        return;
    }
    set_text(main_name, fmt("reading.primary", s.primary.empty() ? "--" : s.primary) +
                            (s.hold ? std::string(" / ") + tr("reading.held") : ""));
    set_text(main_value, format_value(s.reading.primary, primary_unit(s.primary)));
    const bool native_tol = s.tolerance.monitored && (!s.tolerance.known || s.tolerance.enabled);
    const bool compare = s.tolerance.known && s.tolerance.enabled;
    for (int i = 0; i < 2; ++i) {
        enable_button(mode_buttons[i], s.ready && s.tolerance.known && !pending);
        lv_obj_set_style_bg_color(
            mode_buttons[i],
            lv_color_hex(s.tolerance.known && compare == bool(i) ? 0x1c6c60 : 0x23354f), 0);
    }
    set_text(tol_badge,
             compare
                 ? fmt("tol.nominal", format_value(s.tolerance.nominal, primary_unit(s.primary))) +
                       "   /   " +
                       (s.tolerance.percent ? fmt("tol.range", std::to_string(s.tolerance.percent))
                                            : tr("tol.no_range"))
                 : "");
    set_text(sub_name, native_tol ? tr("tol.secondary")
                                  : fmt("reading.secondary", secondary_name(s.secondary)));
    set_text(sub_value, native_tol ? (s.ready && s.tolerance.known && !pending
                                          ? percent_text(s.tolerance.deviation)
                                          : "--")
                        : s.secondary == "NULL"
                            ? "--"
                            : format_value(s.reading.secondary, secondary_unit(s.secondary)));
    const auto result = pending ? ToleranceResult::Unknown : tolerance_result(s);
    const char *result_key = result == ToleranceResult::Within      ? "tol.within"
                             : result == ToleranceResult::Outside   ? "tol.outside"
                             : result == ToleranceResult::NoRange   ? "tol.select_range"
                             : result == ToleranceResult::Held      ? "tol.result_held"
                             : result == ToleranceResult::Overrange ? "tol.result_overrange"
                             : result == ToleranceResult::Invalid   ? "tol.result_invalid"
                                                                    : "tol.result_wait";
    const auto result_color = result == ToleranceResult::Outside  ? 0xff6576u
                              : result == ToleranceResult::Within ? ACCENT
                                                                  : MUTED;
    set_text(tol_result_label, native_tol ? tr(result_key) : "");
    lv_obj_set_style_text_color(tol_result_label, lv_color_hex(result_color), 0);
    lv_obj_set_style_text_color(sub_value, lv_color_hex(native_tol ? result_color : ACCENT), 0);
    auto *result_card = lv_obj_get_parent(tol_result_label);
    lv_obj_set_style_border_width(result_card, native_tol ? 2 : 0, 0);
    lv_obj_set_style_border_color(result_card, lv_color_hex(result_color), 0);
    lv_obj_set_style_bg_color(
        result_card,
        lv_color_hex(native_tol && result == ToleranceResult::Outside ? 0x321d2a : CARD), 0);
    fit_reading(main_value, 400, true);
    fit_reading(sub_value, 260, false);
    char b[80];
    set_text(control_text[0], fmt("control.mode", s.primary.empty() ? "--" : s.primary));
    if (s.hz >= 1000)
        std::snprintf(b, sizeof(b), "%d kHz", s.hz / 1000);
    else
        std::snprintf(b, sizeof(b), "%d Hz", s.hz);
    set_text(control_text[1], fmt("control.freq", s.hz ? b : "--"));
    std::snprintf(b, sizeof(b), "%.1f V", s.level);
    std::string level = s.level ? b : "--";
    if (s.level && !profile(s.model).selectable_level)
        level += " " + std::string(tr("value.fixed"));
    set_text(control_text[2], fmt("control.level", level));
    set_text(control_text[3], fmt("control.circuit", s.equivalent == "SER"   ? tr("value.series")
                                                     : s.equivalent == "PAL" ? tr("value.parallel")
                                                                             : "--"));
    set_text(control_text[4], fmt("control.second", secondary_name(s.secondary)));
    set_text(control_text[5],
             fmt("control.source", !s.connected ? "--" : std::to_string(s.poll_ms) + " ms"));
    for (int i = 0; i < 6; ++i) {
        bool enabled = !pending && s.ready && (i == 0 || i == 5 || s.primary != "DCR") &&
                       (i != 2 || profile(s.model).selectable_level);
        if (enabled)
            lv_obj_clear_state(control[i], LV_STATE_DISABLED);
        else
            lv_obj_add_state(control[i], LV_STATE_DISABLED);
    }
    if (s.ready && compare)
        lv_obj_clear_flag(tol_button, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(tol_button, LV_OBJ_FLAG_HIDDEN);
    enable_button(tol_button, s.ready && !pending);
    if (compare) {
        // Exceptional externally-enabled REC remains stoppable without replacing the
        // comparison settings or Hold controls in the bottom row.
        if (s.recording.known && s.recording.enabled) {
            lv_obj_set_pos(reset_button, 536, 327);
            lv_obj_clear_flag(reset_button, LV_OBJ_FLAG_HIDDEN);
        } else
            lv_obj_add_flag(reset_button, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_pos(reset_button, 280, 391);
        lv_obj_clear_flag(reset_button, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 5; ++i) {
        if ((compare && i != 3) || (s.recording.known && s.recording.enabled)) {
            lv_obj_set_style_bg_color(control[i], lv_color_hex(0x192332), 0);
            lv_obj_set_style_text_color(control[i], lv_color_hex(MUTED), 0);
            lv_obj_clear_flag(control_lock[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_width(control_text[i], 192);
            lv_obj_set_style_text_align(control_text[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(control_text[i], LV_ALIGN_CENTER, -10, 0);
        } else {
            lv_obj_set_style_bg_color(control[i], lv_color_hex(0x23354f), 0);
            lv_obj_set_style_text_color(control[i], lv_color_hex(INK), 0);
            lv_obj_add_flag(control_lock[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_width(control_text[i], 220);
            lv_obj_set_style_text_align(control_text[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(control_text[i], LV_ALIGN_CENTER, 0, 0);
        }
    }
    update_tolerance_dialog();
    update_recording_dialog();
    set_text(hold_label, tr(s.hold ? "button.resume" : "button.hold"));
    std::string info;
    if (!s.error.empty())
        info = i18n::format(s.error.c_str(), {{"detail", s.error_detail}});
    else if (s.connected)
        info = compare ? tr("mode.lock_hint") : "";
    else
        info = tr("hint.offline");
    set_text(message, info);
    lv_obj_set_style_text_color(message, lv_color_hex(s.error.empty() ? MUTED : 0xffbf69), 0);
}
