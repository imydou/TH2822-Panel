#include "ui.hpp"
#include "i18n.hpp"
#include "lvgl.h"
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
static const FontFamily families[] = {
    {"latin", &lv_font_montserrat_14, &lv_font_montserrat_20, &lv_font_montserrat_24},
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
static lv_obj_t *title, *status, *main_value, *sub_value, *main_name, *sub_name, *message, *stats,
    *hold_label, *sync_button;
static lv_obj_t *control[6], *control_text[6], *modal = nullptr;
static constexpr uint32_t BG = 0x0b1423, CARD = 0x162338, INK = 0xecf4ff, MUTED = 0x99aecb,
                          ACCENT = 0x40dfc8;
static void set_text(lv_obj_t *o, const std::string &s) {
    if (s != lv_label_get_text(o))
        lv_label_set_text(o, s.c_str());
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
             {{"D", "D"}, {"Q", "Q"}, {"THETA", "THETA"}, {"ESR", "ESR"}});
    if (i == 5)
        menu(ActionType::PollInterval, "menu.source",
             {{"1000", tr("value.query_slow")},
              {"500", tr("value.query_medium")},
              {"250", tr("value.query_fast")}});
}
static void hold(lv_event_t *) {
    send_action({ActionType::Hold, ""});
}
static void reset_stats(lv_event_t *) {
    send_action({ActionType::ClearStats, ""});
}
static void reconnect(lv_event_t *) {
    send_action({ActionType::Reconnect, ""});
}
static void synchronize(lv_event_t *) {
    send_action({ActionType::Resync, ""});
}
static void guidance(lv_event_t *) {
    auto *panel = open_modal(65, 24, 670, 430);
    label(panel, tr("menu.guide"), 22, 19, 500, fonts().normal, INK);
    button(panel, tr("button.cancel"), 562, 8, 90, 48, cancel);
    label(panel, tr("hint.query_steps"), 24, 80, 622, fonts().normal, INK);
    label(panel, view.identity.c_str(), 24, 365, 622, fonts().small, MUTED);
}
static void instrument_info(lv_event_t *) {
    auto *panel = open_modal(40, 24, 720, 430);
    label(panel, tr("menu.instrument"), 22, 19, 530, fonts().heading, INK);
    button(panel, tr("button.cancel"), 612, 8, 90, 48, cancel);
    auto *body = lv_obj_create(panel);
    lv_obj_set_pos(body, 16, 70);
    lv_obj_set_size(body, 688, 340);
    lv_obj_set_style_bg_opa(body, 0, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 6, 0);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    std::string text =
        fmt("info.model", profile(view.model).name) + "\n" + fmt("info.firmware", view.firmware) +
        "\n" + fmt("info.serial", view.serial) + "\n\n" + fmt("info.identity", view.identity) +
        "\n\n" + fmt("info.usb", view.usb_info) + "\n" + tr("info.transport") + "\n\n" +
        tr("info.snapshot") + "\n" + fmt("info.function", view.primary + " / " + view.secondary) +
        "\n" + fmt("info.frequency", view.hz ? std::to_string(view.hz) + " Hz" : "--") + "\n";
    char volts[24];
    std::snprintf(volts, sizeof(volts), "%.1f V", view.level);
    text += fmt("info.level", view.level ? volts : "--") + "\n" +
            fmt("info.circuit", view.equivalent.empty() ? "--" : view.equivalent) + "\n\n" +
            tr("info.scope");
    label(body, text.c_str(), 0, 0, 650, fonts().normal, INK);
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
        auto *waiting = card(root, 64, 92, 672, 274);
        label(waiting, "USB", 24, 20, 100, &lv_font_montserrat_24, ACCENT);
        title = label(waiting, tr("connection.title"), 24, 60, 624, fonts().heading, INK);
        status = label(waiting, tr("status.wait"), 24, 105, 624, fonts().normal, MUTED);
        message = label(waiting, tr("hint.offline"), 24, 157, 624, fonts().normal, MUTED);
        button(root, tr("button.retry"), 300, 390, 200, 56, reconnect);
        return;
    }
    title = label(root, "TH2822", 24, 17, 450, fonts().heading, INK);
    status = label(root, tr("status.wait"), 24, 52, 752, fonts().normal, MUTED);
    button(root, tr("button.language"), 540, 12, 112, 44, languages);
    button(root, tr("button.guide"), 664, 12, 112, 44, guidance);
    lv_obj_t *b;
    auto *left = card(root, 24, 88, 488, 165);
    main_name = label(left, "", 20, 17, 448, fonts().normal, MUTED);
    main_value = label(left, "--", 20, 58, 450, &lv_font_montserrat_48, INK);
    auto *right = card(root, 528, 88, 248, 165);
    sub_name = label(right, "", 18, 17, 220, fonts().normal, MUTED);
    sub_value = label(right, "--", 18, 63, 224, &lv_font_montserrat_32, ACCENT);
    for (int i = 0; i < 6; ++i) {
        int col = i % 3, row = i / 3;
        control[i] = button(root, "--", 24 + col * 256, 270 + row * 57, 240, 49, controls,
                            (void *)(intptr_t)i);
        control_text[i] = lv_obj_get_child(control[i], 0);
    }
    b = button(root, tr("button.hold"), 24, 391, 104, 49, hold);
    hold_label = lv_obj_get_child(b, 0);
    button(root, tr("button.reset"), 140, 391, 120, 49, reset_stats);
    button(root, tr("button.info"), 272, 391, 120, 49, instrument_info);
    sync_button = button(root, tr("button.sync"), 404, 391, 108, 49, synchronize);
    stats = label(root, tr("stats.empty"), 530, 382, 245, fonts().small, MUTED);
    message = label(root, tr("hint.offline"), 24, 453, 752, fonts().small, MUTED);
    lv_label_set_long_mode(message, LV_LABEL_LONG_DOT);
}
void panel_ui_update(const State &s, bool pending) {
    const bool locale_changed = s.locale != i18n::current().id && i18n::set_locale(s.locale);
    if (locale_changed || s.connected != main_page) {
        view = s;
        panel_ui_create(send_action); // Drops open dialogs on disconnect; no main controls remain.
    }
    const std::string signature =
        s.locale + s.identity + s.primary + s.secondary + s.equivalent + s.error + s.error_detail +
        std::to_string((int)s.model) + std::to_string(s.hz) + std::to_string(s.level) +
        std::to_string(s.connected) + std::to_string(s.ready) + std::to_string((int)s.phase) +
        std::to_string(s.hold) + std::to_string(s.poll_ms) + std::to_string(s.sample_sequence) +
        std::to_string(s.stats.count) + std::to_string(pending);
    if (signature == previous)
        return;
    previous = signature;
    view = s;
    auto model = s.model == Model::Unknown ? tr("model.unknown") : profile(s.model).name;
    set_text(title,
             main_page ? i18n::format("app.title", {{"model", model}}) : tr("connection.title"));
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
    set_text(status, tr(key));
    lv_obj_set_style_text_color(status, lv_color_hex(s.ready ? ACCENT : MUTED), 0);
    if (!main_page) {
        const auto details = s.error.empty()
                                 ? std::string(tr("hint.offline"))
                                 : i18n::format(s.error.c_str(), {{"detail", s.error_detail}});
        set_text(message, details);
        lv_obj_set_style_text_color(message, lv_color_hex(s.error.empty() ? MUTED : 0xffbf69), 0);
        return;
    }
    set_text(main_name, fmt("reading.primary", s.primary.empty() ? "--" : s.primary) +
                            (s.hold ? std::string(" / ") + tr("reading.held") : ""));
    set_text(main_value, format_value(s.reading.primary, primary_unit(s.primary)));
    set_text(sub_name,
             fmt("reading.secondary", s.secondary == "NULL" ? tr("reading.none") : s.secondary));
    set_text(sub_value, s.secondary == "NULL"
                            ? "--"
                            : format_value(s.reading.secondary, secondary_unit(s.secondary)));
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
    set_text(control_text[4],
             fmt("control.second", s.secondary == "NULL" ? tr("reading.none") : s.secondary));
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
    if (!s.connected || s.ready)
        lv_obj_add_flag(sync_button, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_clear_flag(sync_button, LV_OBJ_FLAG_HIDDEN);
    if (s.connected && !pending)
        lv_obj_clear_state(sync_button, LV_STATE_DISABLED);
    else
        lv_obj_add_state(sync_button, LV_STATE_DISABLED);
    set_text(hold_label, tr(s.hold ? "button.resume" : "button.hold"));
    if (s.stats.count) {
        std::string u = primary_unit(s.primary);
        set_text(stats, i18n::format("stats.values",
                                     {{"count", std::to_string(s.stats.count)},
                                      {"min", format_value({Value::Valid, s.stats.min}, u)},
                                      {"max", format_value({Value::Valid, s.stats.max}, u)},
                                      {"avg", format_value({Value::Valid, s.stats.mean}, u)}}));
    } else
        set_text(stats, tr("stats.empty"));
    std::string info;
    if (!s.error.empty())
        info = i18n::format(s.error.c_str(), {{"detail", s.error_detail}});
    else if (s.connected)
        info = tr("hint.query_control");
    else
        info = tr("hint.offline");
    set_text(message, info);
    lv_obj_set_style_text_color(message, lv_color_hex(s.error.empty() ? MUTED : 0xffbf69), 0);
}
