#include "fixtures/instrument_fixture.hpp"
#include "i18n.hpp"
#include "lvgl.h"
#include "ui.hpp"
#include <SDL.h>
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
    th::Session session(transport);
    th::State offline;
    const std::string locale = argc > 3 ? argv[3] : "zh-CN";
    i18n::set_locale(locale);
    offline.locale = locale;
    session.connect();
    session.state.locale = locale;
    th::State *shown = &offline;
    panel_ui_create([&](th::Action a) {
        if (a.type == th::ActionType::Language) {
            session.state.locale = a.value;
            offline.locale = a.value;
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
        if (ticks % 50 == 0)
            panel_ui_update(*shown);
        static lv_obj_t *test_mark = nullptr;
        if (!test_mark) {
            test_mark = lv_label_create(lv_scr_act());
            lv_label_set_text(test_mark, "TEST INPUT / NO INSTRUMENT");
            lv_obj_set_style_text_font(test_mark, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(test_mark, lv_color_hex(0xffbf69), 0);
            lv_obj_set_pos(test_mark, 260, 22);
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
            if (ticks == 20) {
                screenshot(dest + "-offline.ppm");
                shown = &session.state;
            }
            if (ticks == 100)
                screenshot(dest + "-await-stream.ppm");
            if (ticks == 110) {
                session.state.streaming = true;
                session.state.reading = {{th::Value::Valid, 6.8e-6}, {th::Value::Valid, 0.01}, "N"};
                ++session.state.sample_sequence;
            }
            if (ticks == 180)
                screenshot(dest + "-stream-fixture.ppm");
            if (ticks == 190) {
                shown = &offline;
                offline.error = "usb.unplugged";
            }
            if (ticks == 250) {
                screenshot(dest + "-disconnect.ppm");
            }
            if (ticks == 260) {
                for (unsigned i=0; i<lv_obj_get_child_cnt(lv_scr_act()); ++i) {
                    auto *o=lv_obj_get_child(lv_scr_act(),i);
                    if (lv_obj_check_type(o,&lv_btn_class) && lv_obj_get_child_cnt(o) &&
                        std::string(lv_label_get_text(lv_obj_get_child(o,0))) == i18n::text("button.guide")) {
                        lv_event_send(o,LV_EVENT_CLICKED,nullptr); break;
                    }
                }
            }
            if (ticks == 300) { screenshot(dest + "-guide.ppm"); quit = true; }
        }
        ++ticks;
    }
    if (!headless)
        SDL_Quit();
    return 0;
}
