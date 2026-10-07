#include "fixtures/instrument_fixture.hpp"
#include "i18n.hpp"
#include "lvgl.h"
#include "traffic.hpp"
#include "ui.hpp"
#include <SDL.h>
#include <cassert>
#include <fstream>
#include <string>
#include <vector>
static std::vector<uint16_t> frame(800 * 480);
static bool down = false;
static int mx = 0, my = 0;
static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *p) {
    for (int y = a->y1; y <= a->y2; ++y)
        for (int x = a->x1; x <= a->x2; ++x) {
            if (x >= 0 && x < 800 && y >= 0 && y < 480)
                frame[y * 800 + x] = p->full;
            ++p;
        }
    lv_disp_flush_ready(d);
}
static void input(lv_indev_drv_t *, lv_indev_data_t *d) {
    d->point.x = mx;
    d->point.y = my;
    d->state = down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}
static void screenshot(const std::string &file) {
    std::ofstream f(file, std::ios::binary);
    f << "P6\n800 480\n255\n";
    for (auto c : frame) {
        unsigned char rgb[] = {(unsigned char)(((c >> 11) & 31) * 255 / 31),
                               (unsigned char)(((c >> 5) & 63) * 255 / 63),
                               (unsigned char)((c & 31) * 255 / 31)};
        f.write((char *)rgb, 3);
    }
}
static lv_obj_t *find_button(lv_obj_t *parent, const char *text) {
    for (unsigned i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
        auto *o = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(o, &lv_btn_class) && lv_obj_get_child_cnt(o) &&
            std::string(lv_label_get_text(lv_obj_get_child(o, 0))) == text)
            return o;
        if (auto *found = find_button(o, text))
            return found;
    }
    return nullptr;
}
static bool label_contains(lv_obj_t *parent, const std::string &text) {
    if (lv_obj_check_type(parent, &lv_label_class) &&
        std::string(lv_label_get_text(parent)).find(text) != std::string::npos)
        return true;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(parent); ++i)
        if (label_contains(lv_obj_get_child(parent, i), text))
            return true;
    return false;
}
static lv_obj_t *find_label(lv_obj_t *parent, const std::string &text) {
    if (lv_obj_check_type(parent, &lv_label_class) && text == lv_label_get_text(parent))
        return parent;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(parent); ++i)
        if (auto *found = find_label(lv_obj_get_child(parent, i), text))
            return found;
    return nullptr;
}
static void assert_reading_fits(lv_obj_t *parent, const std::string &text) {
    auto *label = find_label(parent, text);
    assert(label);
    lv_point_t size;
    const auto *font = lv_obj_get_style_text_font(label, 0);
    lv_txt_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    assert(size.x <= lv_obj_get_width(label));
    assert(lv_obj_get_height(label) <= font->line_height);
}
static void click_text(const std::string &text) {
    auto *b = find_button(lv_scr_act(), text.c_str());
    assert(b);
    lv_event_send(b, LV_EVENT_CLICKED, nullptr);
}
static void click(const char *key) {
    click_text(i18n::text(key));
}
static void tap(int x, int y) {
    mx = x;
    my = y;
    for (bool pressed : {true, false}) {
        down = pressed;
        for (int i = 0; i < 8; ++i) {
            lv_tick_inc(10);
            lv_timer_handler();
        }
    }
}

int main(int argc, char **argv) {
    bool headless = argc > 1 && std::string(argv[1]) == "--capture";
    std::string dest = argc > 2 ? argv[2] : "ui";
    SDL_Window *window = nullptr;
    SDL_Renderer *render = nullptr;
    SDL_Texture *texture = nullptr;
    if (!headless) {
        SDL_Init(SDL_INIT_VIDEO);
        window = SDL_CreateWindow("TH2822 UI TEST FIXTURE - no hardware", SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, 800, 480, 0);
        render = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        texture = SDL_CreateTexture(render, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                    800, 480);
    }
    lv_init();
    static lv_color_t draw[800 * 48];
    static lv_disp_draw_buf_t buf;
    lv_disp_draw_buf_init(&buf, draw, nullptr, 800 * 48);
    static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = 800;
    driver.ver_res = 480;
    driver.flush_cb = flush;
    driver.draw_buf = &buf;
    lv_disp_drv_register(&driver);
    static lv_indev_drv_t mouse;
    lv_indev_drv_init(&mouse);
    mouse.type = LV_INDEV_TYPE_POINTER;
    mouse.read_cb = input;
    lv_indev_drv_register(&mouse);
    InstrumentFixture transport;
    transport.firmware = "VER4.5.2307";
    th::Session session(transport);
    th::State offline;
    const std::string locale = argc > 3 ? argv[3] : "zh-CN";
    i18n::set_locale(locale);
    offline.locale = locale;
    session.connect();
    session.apply({th::ActionType::ToleranceInspect, ""});
    session.apply({th::ActionType::RecordingInspect, ""});
    session.state.locale = locale;
    session.state.usb_info = "VID 10C4 / PID EA60 (TEST FIXTURE)";
    th::State *shown = &offline;
    std::vector<th::Action> actions;
    bool last_action_ok = true;
    panel_ui_create([&](th::Action a) {
        actions.push_back(a);
        if (a.type == th::ActionType::Language) {
            session.state.locale = a.value;
            offline.locale = a.value;
        } else {
            last_action_ok = session.apply(a);
        }
    });
    unsigned ticks = 0;
    bool quit = false;
    while (!quit) {
        if (!headless) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT)
                    quit = true;
                if (e.type == SDL_MOUSEMOTION) {
                    mx = e.motion.x;
                    my = e.motion.y;
                }
                if (e.type == SDL_MOUSEBUTTONDOWN)
                    down = true;
                if (e.type == SDL_MOUSEBUTTONUP)
                    down = false;
            }
        }
        if (ticks % 5 == 0)
            panel_ui_update(*shown);
        static lv_obj_t *test_mark = nullptr;
        if (!test_mark) {
            test_mark = lv_label_create(lv_layer_top());
            lv_label_set_text(test_mark, "TEST INPUT / NO INSTRUMENT");
            lv_obj_set_style_text_font(test_mark, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(test_mark, lv_color_hex(0xffbf69), 0);
            lv_obj_set_pos(test_mark, 260, 0);
        }
        lv_tick_inc(10);
        lv_timer_handler();
        if (!headless) {
            SDL_UpdateTexture(texture, nullptr, frame.data(), 800 * 2);
            SDL_RenderCopy(render, texture, nullptr, nullptr);
            SDL_RenderPresent(render);
            SDL_Delay(10);
        }
        if (headless) {
            auto settle = [&]() {
                panel_ui_update(*shown);
                lv_obj_update_layout(lv_scr_act());
                for (int i = 0; i < 5; ++i) {
                    lv_tick_inc(20);
                    lv_timer_handler();
                }
            };
            auto capture = [&](const char *name) {
                settle();
                screenshot(dest + name + ".ppm");
            };
            if (ticks == 20) {
                assert(!find_button(lv_scr_act(), i18n::text("button.hold")));
                offline.error = "usb.waiting";
                capture("-offline");
                assert(label_contains(lv_scr_act(), i18n::text("connection.idle")));
                assert(!label_contains(lv_scr_act(), i18n::text("usb.waiting")));
                offline.phase = th::ConnectionPhase::Identifying;
                capture("-identifying");
                assert(lv_obj_has_state(find_button(lv_scr_act(), i18n::text("connection.retry")),
                                        LV_STATE_DISABLED));
                offline.phase = th::ConnectionPhase::SerialReady;
                offline.error = "error.timeout";
                offline.error_detail = "*IDN?";
                capture("-connection-error");
                assert(!lv_obj_has_state(find_button(lv_scr_act(), i18n::text("connection.retry")),
                                         LV_STATE_DISABLED));
                offline.phase = th::ConnectionPhase::Disconnected;
                offline.error.clear();
                offline.error_detail.clear();
                shown = &session.state;
                session.poll();
            }
            if (ticks == 70) {
                settle();
                capture("-measurement");
                const auto circuit_text =
                    i18n::format("control.circuit", {{"value", i18n::text("value.series")}});
                auto *circuit_label = find_label(lv_scr_act(), circuit_text);
                assert(circuit_label &&
                       lv_obj_get_style_text_align(circuit_label, 0) == LV_TEXT_ALIGN_CENTER);
                const auto normal_state = session.state;
                session.state.primary = "R";
                session.state.secondary = "ESR";
                session.state.reading.primary = {th::Value::Valid, 1.23456e12};
                session.state.reading.secondary = {th::Value::Valid, -1.23456e12};
                ++session.state.sample_sequence;
                capture("-wide-readings");
                assert_reading_fits(lv_scr_act(), "1.23456e+06 Mohm");
                assert_reading_fits(lv_scr_act(), "-1.23456e+06 Mohm");
                session.state.secondary = "THETA";
                ++session.state.sample_sequence;
                settle();
                assert(!label_contains(lv_scr_act(), "THETA"));
                auto *theta =
                    find_label(lv_scr_act(), i18n::format("reading.secondary", {{"value", "θ"}}));
                assert(theta);
                lv_font_glyph_dsc_t glyph;
                assert(
                    lv_font_get_glyph_dsc(lv_obj_get_style_text_font(theta, 0), &glyph, 0x3b8, 0));
                session.state = normal_state;
                settle();
                assert(!label_contains(lv_scr_act(), i18n::text("status.query")));
                auto *info = find_button(lv_scr_act(), i18n::text("button.info"));
                assert(info && lv_obj_get_style_bg_opa(info, 0) == LV_OPA_COVER);
                assert(lv_obj_get_style_radius(info, 0) == 12);
                assert(lv_obj_get_x(info) + lv_obj_get_width(info) == 776);
                auto *hold = find_button(lv_scr_act(), i18n::text("button.hold"));
                auto *stats = find_button(lv_scr_act(), i18n::text("rec.button"));
                assert(lv_obj_get_width(hold) == 240 && lv_obj_get_x(hold) == 536);
                assert(lv_obj_get_width(stats) == 240 && lv_obj_get_x(stats) == 280);
                auto *dot = lv_obj_get_child(lv_scr_act(), 1);
                auto *model_title = lv_obj_get_child(lv_scr_act(), 0);
                auto expect_color = [](lv_obj_t *o, unsigned rgb) {
                    return lv_obj_get_style_bg_color(o, 0).full == lv_color_hex(rgb).full;
                };
                th::traffic_log().add(0, 'T', "FUNC:EQU PAL");
                panel_ui_update(*shown, true);
                assert(expect_color(dot, 0xff5364));
                assert(lv_obj_get_style_text_color(model_title, 0).full ==
                       lv_color_hex(0xecf4ff).full);
                lv_tick_inc(200);
                th::traffic_log().add(0, 'T', "FUNC:EQU?");
                panel_ui_update(*shown, true);
                assert(expect_color(dot, 0x529bff));
                lv_tick_inc(200);
                panel_ui_update(*shown);
                assert(expect_color(dot, 0x35435a));
                lv_point_t text_size;
                lv_txt_get_size(&text_size, lv_label_get_text(model_title),
                                lv_obj_get_style_text_font(model_title, 0), 0, 0, LV_COORD_MAX,
                                LV_TEXT_FLAG_NONE);
                assert(lv_obj_get_x(dot) == 24 + text_size.x + 10);
                auto *m0 = find_button(lv_scr_act(), i18n::text("mode.measure"));
                auto *m1 = find_button(lv_scr_act(), i18n::text("mode.compare"));
                assert(lv_obj_get_x(m0) + lv_obj_get_x(m1) + lv_obj_get_width(m1) == 800);
                click_text(i18n::format("control.source", {{"value", "500 ms"}}));
                click_text("333 ms");
                assert(session.state.poll_ms == 333);
                settle();
                click_text(i18n::format("control.source", {{"value", "333 ms"}}));
                click_text("667 ms");
                assert(session.state.poll_ms == 667);
                settle();
                click("rec.button");
                settle();
                click("rec.start");
                assert(last_action_ok && transport.recording);
                session.poll();
                capture("-recording");
                assert(session.state.recording.live);
                assert(label_contains(lv_scr_act(), "6.8 uF"));
                assert(label_contains(lv_scr_act(), "0.003"));
                click("rec.avg");
                capture("-recording-average");
                assert(label_contains(lv_scr_act(), "6.9 uF"));
                assert(!label_contains(lv_scr_act(), "0.004")); // No unverified secondary stats.
                assert(label_contains(lv_scr_act(), i18n::text("rec.snapshot")));
                const auto readback = session.state.recording.read_at_ms;
                session.poll();
                assert(session.state.recording.read_at_ms == readback);
                const auto sent = actions.size();
                click("rec.avg");
                assert(actions.size() == sent); // Re-tap same item does not reselect/beep.
                transport.rec_avg = "7.2e-6,0";
                click("rec.update");
                settle();
                assert(last_action_ok && label_contains(lv_scr_act(), "7.2 uF"));
                click("button.cancel");
                settle();
                click("rec.button");
                settle();
                assert(session.state.recording.view == th::RecordingView::Average);
                click("rec.present");
                assert(last_action_ok && session.state.recording.live);
                session.poll();
                capture("-recording-current");
                const auto recording_state = session.state;
                session.state.primary = "R";
                session.state.secondary = "ESR";
                session.state.recording.value.primary = {th::Value::Valid, 1.23456e12};
                session.state.recording.value.secondary = {th::Value::Valid, -1.23456e12};
                ++session.state.recording.revision;
                capture("-recording-wide");
                auto *recording_panel =
                    lv_obj_get_child(lv_scr_act(), lv_obj_get_child_cnt(lv_scr_act()) - 1);
                assert_reading_fits(recording_panel, "1.23456e+06 Mohm");
                assert_reading_fits(recording_panel, "-1.23456e+06 Mohm");
                session.state.secondary = "THETA";
                ++session.state.recording.revision;
                settle();
                assert(!label_contains(lv_scr_act(), "THETA"));
                session.state = recording_state;
                settle();
                click("rec.stop");
                settle();
                assert(last_action_ok && !transport.recording);
                click("button.cancel");
                session.poll();
                settle();
                assert(session.apply({th::ActionType::RecordingEnable, ""}));
                settle(); // Native REC becoming enabled automatically opens its page.
                assert(find_button(lv_scr_act(), i18n::text("rec.stop")));
                click("rec.stop");
                settle();
                click("button.cancel");
                session.poll();
                settle();
                assert(find_button(lv_scr_act(), i18n::text("mode.measure")));
                assert(!label_contains(lv_scr_act(), i18n::text("info.transport")));
                const auto initial = actions.size();
                click("mode.compare");
                capture("-reference");
                assert(actions.size() == initial); // Mode click alone never captures.
                click("tol.back");
                assert(actions.size() == initial);
                click("mode.compare");
                session.state.reading.primary = {th::Value::Valid, 8.2e-6};
                ++session.state.sample_sequence;
                settle();
                assert(label_contains(lv_scr_act(), "8.2 uF"));
                click("tol.confirm");
                assert(last_action_ok && session.state.tolerance.enabled);
                session.poll();
                settle();
                capture("-comparison");
                assert(!find_button(lv_scr_act(), i18n::text("tol.capture")));
                auto *refresh_button = find_button(
                    lv_scr_act(), i18n::format("control.source", {{"value", "667 ms"}}).c_str());
                assert(refresh_button && lv_obj_get_x(refresh_button) == 24 &&
                       lv_obj_get_y(refresh_button) == 391);
                assert(lv_obj_get_x(find_button(lv_scr_act(), i18n::text("button.tolerance"))) ==
                       280);
                assert(lv_obj_get_x(find_button(lv_scr_act(), i18n::text("button.hold"))) == 536);
                auto n = actions.size();
                for (auto item :
                     std::vector<std::pair<std::string, std::string>>{{"control.mode", "C"},
                                                                      {"control.freq", "1 kHz"},
                                                                      {"control.level", "0.6 V"},
                                                                      {"control.second", "D"}}) {
                    click_text(i18n::format(item.first.c_str(), {{"value", item.second}}));
                    assert(label_contains(lv_scr_act(), i18n::text("error.tol_locked")));
                    click("button.cancel");
                    assert(actions.size() == n);
                }
                click_text(
                    i18n::format("control.circuit", {{"value", i18n::text("value.series")}}));
                click("value.parallel");
                assert(last_action_ok && session.state.equivalent == "PAL");
                transport.deviation = -3.25;
                session.poll();
                settle();
                assert(label_contains(lv_scr_act(), "-3.25 %"));
                click("button.tolerance");
                settle();
                click_text("5%");
                session.poll();
                settle();
                assert(session.state.tolerance.percent == 5);
                capture("-tolerance-settings");
                assert(find_button(lv_scr_act(), i18n::text("tol.capture")));
                click("button.cancel");
                transport.deviation = 5.001;
                session.poll();
                capture("-tolerance-outside");
                auto *result = find_label(lv_scr_act(), i18n::text("tol.outside"));
                assert(result &&
                       lv_obj_get_style_text_color(result, 0).full == lv_color_hex(0xff6576).full);
                transport.deviation = -5.0;
                session.poll();
                capture("-tolerance-within");
                assert(label_contains(lv_scr_act(), i18n::text("tol.within")));
                click("button.hold");
                settle();
                assert(label_contains(lv_scr_act(), i18n::text("tol.result_held")));
                assert(!label_contains(lv_scr_act(), i18n::text("tol.within")));
                click("button.resume");
                session.poll();
                settle();
                click("button.info");
                capture("-info");
                assert(!label_contains(lv_scr_act(), "CP210x"));
                n = actions.size();
                click("logs.title");
                capture("-log");
                // A real drag away from the latest line pauses follow automatically.
                mx = 400;
                my = 150;
                down = true;
                for (int i = 0; i < 6; ++i) {
                    lv_tick_inc(20);
                    lv_timer_handler();
                }
                for (int y = 160; y <= 320; y += 20) {
                    my = y;
                    lv_tick_inc(20);
                    lv_timer_handler();
                }
                down = false;
                lv_tick_inc(20);
                lv_timer_handler();
                assert(find_button(lv_scr_act(), i18n::text("logs.latest")));
                th::traffic_log().add(123456, 'E', "TEST-PAUSED-ENTRY");
                settle();
                assert(!label_contains(lv_scr_act(), "TEST-PAUSED-ENTRY"));
                click("logs.latest");
                settle();
                assert(label_contains(lv_scr_act(), "TEST-PAUSED-ENTRY"));
                click("logs.clear");
                settle();
                assert(!label_contains(lv_scr_act(), "TEST-PAUSED-ENTRY"));
                assert(actions.size() == n); // Diagnostics never send instrument commands.
                click("logs.back");
                click("button.cancel");
                click("mode.measure");
                settle();
                assert(last_action_ok && !session.state.tolerance.enabled);
                session.poll();
                settle();
                click_text(i18n::format("control.level", {{"value", "0.6 V"}}));
                click_text("1 V");
                assert(last_action_ok && session.state.level == 1);
                session.poll();
                settle();
                click("mode.compare");
                click("tol.confirm");
                session.poll();
                settle();
                transport.ignore_tol_writes = true;
                click("mode.measure");
                settle();
                assert(!last_action_ok && !session.state.ready);
                assert(session.state.tolerance.enabled && !session.state.tolerance.known);
                capture("-mode-error");
                assert(label_contains(lv_scr_act(), i18n::text("connection.error_title")));
                assert(!find_button(lv_scr_act(), i18n::text("button.cancel")));
                assert(find_button(lv_scr_act(), i18n::text("button.sync")));
                transport.ignore_tol_writes = false;
                click("button.sync");
                settle();
                assert(last_action_ok && session.state.ready);
                assert(!label_contains(lv_scr_act(), i18n::text("connection.error_title")));
                click("button.info");
                shown = &offline;
                offline.phase = th::ConnectionPhase::Disconnected;
                offline.error = "usb.unplugged";
                settle();
                assert(!find_button(lv_scr_act(), i18n::text("button.info")));
                capture("-disconnect");
                assert(label_contains(lv_scr_act(), i18n::text("connection.error_title")));
                assert(find_button(lv_scr_act(), i18n::text("button.sync")));
                panel_ui_update(offline, true);
                assert(lv_obj_has_state(find_button(lv_scr_act(), i18n::text("button.sync")),
                                        LV_STATE_DISABLED));
                panel_ui_update(offline);
                assert(!find_button(lv_scr_act(), i18n::text("button.cancel")));
                shown = &session.state;
                settle();
                assert(!label_contains(lv_scr_act(), i18n::text("connection.error_title")));
                shown = &offline;
                settle();
                assert(label_contains(lv_scr_act(), i18n::text("connection.error_title")));
                shown = &session.state;
                settle();
                assert(!label_contains(lv_scr_act(), i18n::text("connection.error_title")));
                quit = true;
            }
        }
        ++ticks;
    }
    if (!headless)
        SDL_Quit();
    return 0;
}
