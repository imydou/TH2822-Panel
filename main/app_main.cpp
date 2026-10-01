#include "board.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_rom_uart.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "i18n.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui.hpp"
#include "usb_transport.hpp"
#include <atomic>
#include <cstring>
using namespace th;
struct Request {
    ActionType type;
    char value[32];
};
static QueueHandle_t commands;
static SemaphoreHandle_t state_lock;
static State snapshot;
static std::string preferred_locale = "zh-CN";
static bool preferences_ready = false;
static std::atomic<bool> applying{false};
static std::atomic<unsigned> touch_events{0}, ui_ticks{0};
static void publish(const State &s) {
    xSemaphoreTake(state_lock, portMAX_DELAY);
    snapshot = s;
    snapshot.locale = preferred_locale;
    xSemaphoreGive(state_lock);
}
static void enqueue(Action a) {
    Request r = {};
    r.type = a.type;
    snprintf(r.value, sizeof(r.value), "%s", a.value.c_str());
    if (xQueueSend(commands, &r, 0) != pdTRUE) {
        xSemaphoreTake(state_lock, portMAX_DELAY);
        snapshot.error = "error.queue";
        xSemaphoreGive(state_lock);
    } else {
        ++touch_events;
        ESP_LOGI("ui", "Action=%d value=%s", (int)a.type, r.value);
    }
}
static void worker(void *) {
    UsbTransport usb;
    Session meter(usb);
    bool had_serial = false;
    int64_t next_poll = 0, retry = 0, last_frame = 0;
    while (true) {
        Request req;
        if (xQueueReceive(commands, &req, pdMS_TO_TICKS(10)) == pdTRUE) {
            Action action{req.type, req.value};
            if (action.type == ActionType::Language) {
                bool found = false;
                for (size_t i = 0; i < i18n::locale_count; ++i)
                    if (action.value == i18n::locales[i].id)
                        found = true;
                if (found) {
                    preferred_locale = action.value;
                    nvs_handle_t handle;
                    esp_err_t result = preferences_ready
                                           ? nvs_open("th2822panel", NVS_READWRITE, &handle)
                                           : ESP_FAIL;
                    if (result == ESP_OK) {
                        result = nvs_set_str(handle, "locale", preferred_locale.c_str());
                        if (result == ESP_OK)
                            result = nvs_commit(handle);
                        nvs_close(handle);
                    }
                    if (result != ESP_OK)
                        meter.state.error = "error.locale_save";
                    ESP_LOGI("locale", "selected=%s persist=%s", preferred_locale.c_str(),
                             esp_err_to_name(result));
                }
            } else if (action.type == ActionType::Reconnect) {
                usb.close();
                had_serial = false;
                meter.disconnect("usb.reconnect");
                xQueueReset(commands);
                retry = 0;
            } else {
                const bool remote =
                    action.type != ActionType::Hold && action.type != ActionType::ClearStats;
                applying = remote;
                if (remote) {
                    State pending = meter.state;
                    pending.reading = {};
                    publish(pending);
                }
                meter.apply(action);
                applying = false;
                if (remote) {
                    last_frame = esp_timer_get_time() / 1000;
                    next_poll = 0;
                }
            }
            publish(meter.state);
        }
        const int64_t now = esp_timer_get_time() / 1000;
        if (!usb.connected() && had_serial) {
            had_serial = false;
            meter.disconnect("usb.unplugged");
            xQueueReset(commands);
            publish(meter.state);
            retry = now + 1000;
        }
        if (!usb.connected() && now >= retry) {
#if CONFIG_PANEL_USB_HOST_ENABLED
            if (usb.open()) {
                had_serial = true;
                meter.state.phase = ConnectionPhase::SerialReady;
                publish(meter.state);
                meter.state.phase = ConnectionPhase::Identifying;
                meter.state.error.clear();
                publish(meter.state);
                ESP_LOGI("meter", "Serial ready; querying *IDN? before declaring connection");
                meter.connect();
                ESP_LOGI("meter", "Identity accepted=%d ready=%d model=%s mode=%s",
                         meter.state.connected, meter.state.ready, profile(meter.state.model).name,
                         meter.state.acquisition == Acquisition::AutoFetch ? "AutoFetch" : "Query");
                last_frame = esp_timer_get_time() / 1000;
            } else {
                auto status = UsbTransport::descriptor_status();
                meter.state.phase = UsbTransport::phase();
                meter.state.error = status.rfind("usb.", 0) == 0 ? status : "usb.unsupported";
                meter.state.error_detail = status;
            }
#else
            meter.state.error = "usb.disabled";
#endif
            publish(meter.state);
            retry = now + 3000;
        }
        if (meter.state.ready && meter.state.acquisition == Acquisition::AutoFetch) {
            // No query, heartbeat, quiet drain or recovery command while waiting/receiving.
            if (meter.receive(20))
                last_frame = esp_timer_get_time() / 1000;
            if (!meter.state.ready)
                ESP_LOGW("meter", "Receive stopped: %s / %s", meter.state.error.c_str(),
                         meter.state.error_detail.c_str());
            if (meter.state.streaming && esp_timer_get_time() / 1000 - last_frame > 5000)
                meter.stream_stale();
            publish(meter.state);
        } else if (meter.state.ready && now >= next_poll) {
            meter.poll();
            publish(meter.state);
            next_poll = esp_timer_get_time() / 1000 + meter.state.poll_ms;
        }
        // Failed identity/protocol stays visible; only explicit Retry or hotplug retries identity.
    }
}
static void refresh_ui(lv_timer_t *) {
    ++ui_ticks;
    xSemaphoreTake(state_lock, portMAX_DELAY);
    State s = snapshot;
    xSemaphoreGive(state_lock);
    if (s.phase == ConnectionPhase::Disconnected || s.phase == ConnectionPhase::UsbEnumerated)
        s.phase = UsbTransport::phase();
    panel_ui_update(s, applying);
}
extern "C" void app_main() {
    ESP_LOGI("panel",
             "TH2822 series panel / identity-gated acquisition / no demo or calibration commands");
    commands = xQueueCreate(8, sizeof(Request));
    state_lock = xSemaphoreCreateMutex();
    assert(commands && state_lock);
    // Use a dedicated namespace; never erase an existing NVS partition on failure.
    preferences_ready = nvs_flash_init() == ESP_OK;
    nvs_handle_t preference_handle;
    if (preferences_ready && nvs_open("th2822panel", NVS_READONLY, &preference_handle) == ESP_OK) {
        char locale[32] = {};
        size_t size = sizeof(locale);
        if (nvs_get_str(preference_handle, "locale", locale, &size) == ESP_OK &&
            i18n::set_locale(locale))
            preferred_locale = locale;
        nvs_close(preference_handle);
    }
    i18n::set_locale(preferred_locale);
    snapshot.locale = preferred_locale;
    ESP_LOGI("locale", "boot=%s NVS=%s", preferred_locale.c_str(),
             preferences_ready ? "ready" : "unavailable (not erased)");
    board_init();
#if CONFIG_PANEL_USB_HOST_ENABLED
    UsbTransport::install();
#endif
    if (lvgl_port_lock(1000)) {
        panel_ui_create(enqueue);
        auto *show = lv_timer_create([](lv_timer_t *) { board_show_display(); }, 700, nullptr);
        lv_timer_set_repeat_count(show, 1);
        lv_timer_create(refresh_ui, 100, nullptr);
        lvgl_port_unlock();
    }
    assert(xTaskCreatePinnedToCore(worker, "meter_worker", 8192, nullptr, 4, nullptr, 0) == pdPASS);
    ESP_LOGI("panel", "UART diagnostics: state | locale <id> | pclk 12/16/21; no synthetic data");
    char command[32] = {};
    unsigned used = 0;
    int64_t last_health = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));
        uint8_t c;
        while (esp_rom_output_rx_one_char(&c) == 0) {
            if (c == '\r' || c == '\n') {
                command[used] = 0;
                used = 0;
                if (!strcmp(command, "pclk 12"))
                    board_set_pixel_clock(12000000);
                else if (!strcmp(command, "pclk 16"))
                    board_set_pixel_clock(16000000);
                else if (!strcmp(command, "pclk 21"))
                    board_set_pixel_clock(21000000);
                else if (!strncmp(command, "locale ", 7))
                    enqueue({ActionType::Language, command + 7});
                else if (!strcmp(command, "state")) {
                    xSemaphoreTake(state_lock, portMAX_DELAY);
                    ESP_LOGI(
                        "state",
                        "phase=%d connected=%d ready=%d primary=%s samples=%llu error=%s detail=%s",
                        (int)snapshot.phase, snapshot.connected, snapshot.ready,
                        snapshot.primary.c_str(), (unsigned long long)snapshot.stats.count,
                        snapshot.error.c_str(), snapshot.error_detail.c_str());
                    ESP_LOGI("locale", "current=%s", snapshot.locale.c_str());
                    xSemaphoreGive(state_lock);
                }
            } else if (c >= 32 && c < 127 && used < sizeof(command) - 1)
                command[used++] = c;
        }
        auto now = esp_timer_get_time();
        if (now - last_health >= 10000000) {
            last_health = now;
            ESP_LOGI("health", "uptime=%llds internal=%u psram=%u actions=%u ui_ticks=%u tasks=%u",
                     now / 1000000, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), touch_events.load(),
                     ui_ticks.load(), (unsigned)uxTaskGetNumberOfTasks());
        }
    }
}
