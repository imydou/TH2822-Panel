#include "usb_transport.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "usb/usb_host.h"
#include "usb/vcp_cp210x.h"
#include <cstdio>
static std::atomic<uint32_t> descriptor{0};
static std::atomic<th::ConnectionPhase> usb_phase{th::ConnectionPhase::Disconnected};
static void new_device(usb_device_handle_t dev) {
    const usb_device_desc_t *d = nullptr;
    if (usb_host_get_device_descriptor(dev, &d) == ESP_OK) {
        descriptor = ((uint32_t)d->idVendor << 16) | d->idProduct;
        usb_phase = th::ConnectionPhase::UsbEnumerated;
        ESP_LOGI("usb", "Enumerated VID=%04x PID=%04x class=%02x", d->idVendor, d->idProduct,
                 d->bDeviceClass);
        // The only supported bridge is the documented reference CP2102 identity.
        // No generic CDC fallback or guessing vendor control requests.
    }
}
static void host_task(void *) {
    while (true) {
        uint32_t flags;
        esp_err_t e = usb_host_lib_handle_events(pdMS_TO_TICKS(1000), &flags);
        if (e != ESP_OK && e != ESP_ERR_TIMEOUT)
            ESP_LOGE("usb", "Host event error %s", esp_err_to_name(e));
    }
}
UsbTransport::UsbTransport() {
    rx = xQueueCreate(1024, sizeof(uint8_t));
    assert(rx);
}
void UsbTransport::install() {
    usb_host_config_t host = {};
    host.intr_flags = ESP_INTR_FLAG_LEVEL1;
    ESP_ERROR_CHECK(usb_host_install(&host));
    assert(xTaskCreatePinnedToCore(host_task, "usb_events", 4096, nullptr, 5, nullptr, 0) ==
           pdPASS);
    cdc_acm_host_driver_config_t cfg = {};
    cfg.driver_task_stack_size = 4096;
    cfg.driver_task_priority = 5;
    cfg.xCoreID = 0;
    cfg.new_dev_cb = new_device;
    ESP_ERROR_CHECK(cdc_acm_host_install(&cfg));
    ESP_LOGI("usb", "Host installed; waiting for CP210x 10c4:ea60, interface 0");
}
std::string UsbTransport::descriptor_status() {
    auto d = descriptor.load();
    if (!d)
        return "usb.waiting";
    if (d == 0x10c4ea60)
        return "usb.cp210x";
    char b[100];
    std::snprintf(b, sizeof(b), "USB %04x:%04x%s", (unsigned)(d >> 16), (unsigned)(d & 65535),
                  d == 0x10c4ea60 ? " / CP210x" : "");
    return b;
}
std::string UsbTransport::descriptor_details() {
    const auto d = descriptor.load();
    if (!d)
        return "--";
    char text[100];
    std::snprintf(text, sizeof(text), "VID %04X / PID %04X", (unsigned)(d >> 16),
                  (unsigned)(d & 65535));
    return text;
}
bool UsbTransport::receive(const uint8_t *data, size_t n, void *arg) {
    auto &s = *(UsbTransport *)arg;
    for (size_t i = 0; i < n; ++i)
        if (xQueueSend(s.rx, data + i, 0) != pdTRUE)
            s.overflow = true;
    return true;
}
void UsbTransport::event(const cdc_acm_host_dev_event_data_t *e, void *arg) {
    auto &s = *(UsbTransport *)arg;
    if (e->type == CDC_ACM_HOST_DEVICE_DISCONNECTED || e->type == CDC_ACM_HOST_ERROR) {
        s.alive = false;
        descriptor = 0;
        usb_phase = th::ConnectionPhase::Disconnected;
    }
}
bool UsbTransport::open() {
    close();
    overflow = false;
    framer.reset();
    xQueueReset(rx);
    cdc_acm_host_device_config_t cfg = {};
    cfg.connection_timeout_ms = 700;
    cfg.out_buffer_size = 256;
    cfg.in_buffer_size = 256;
    cfg.event_cb = event;
    cfg.data_cb = receive;
    cfg.user_arg = this;
    // cp210x_vcp_open filters VID/PID and inspects descriptor endpoints through the host driver.
    if (cp210x_vcp_open(CP210X_PID, 0, &cfg, &device) != ESP_OK)
        return false;
    alive = true;
    cdc_acm_host_desc_print(device);
    cdc_acm_line_coding_t coding = {};
    coding.dwDTERate = 9600;
    coding.bDataBits = 8;
    coding.bParityType = 0;
    coding.bCharFormat = 0;
    if (cdc_acm_host_line_coding_set(device, &coding) != ESP_OK ||
        cdc_acm_host_set_control_line_state(device, true, true) != ESP_OK) {
        close();
        return false;
    }
    // Silicon Labs AN571 rev0.4 tables 9-11: no CTS/DSR/DCD/XON/XOFF handshake,
    // DTR held active (1), RTS held active (0x40), reserved bits zero.
    uint8_t flow[16] = {1, 0, 0, 0, 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (cdc_acm_host_send_custom_request(device, 0x41, 0x13, 0, 0, sizeof(flow), flow) != ESP_OK) {
        close();
        return false;
    }
    ESP_LOGI("usb", "CP210x 9600/8N1; persistent DTR/RTS session; no instrument settings sent");
    // Do not wait for silence: a meter may already be sending Auto Fetch.
    usb_phase = th::ConnectionPhase::SerialReady;
    return true;
}
void UsbTransport::close() {
    alive = false;
    usb_phase = descriptor ? th::ConnectionPhase::UsbEnumerated : th::ConnectionPhase::Disconnected;
    if (device) {
        cdc_acm_host_close(device);
        device = nullptr;
    }
    framer.reset();
}
void UsbTransport::delay(unsigned ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}
bool UsbTransport::write(const std::string &s) {
    return alive && !overflow && device &&
           cdc_acm_host_data_tx_blocking(device, (const uint8_t *)s.data(), s.size(), 1000) ==
               ESP_OK;
}
bool UsbTransport::line(std::string &s, unsigned ms) {
    auto until = esp_timer_get_time() + (int64_t)ms * 1000;
    uint8_t c;
    while (alive && !overflow && esp_timer_get_time() < until) {
        if (xQueueReceive(rx, &c, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (framer.feed(c, s))
                return true;
            if (framer.failed()) {
                overflow = true;
                return false;
            }
        }
    }
    return false;
}
bool UsbTransport::quiet(unsigned ms, unsigned max_ms) {
    if (!alive || overflow)
        return false;
    auto start = esp_timer_get_time(), last = start;
    uint8_t c;
    while (alive && !overflow) {
        auto now = esp_timer_get_time();
        if (now - start > (int64_t)max_ms * 1000)
            return false;
        if (now - last >= (int64_t)ms * 1000) {
            framer.reset();
            return true;
        }
        if (xQueueReceive(rx, &c, pdMS_TO_TICKS(10)) == pdTRUE)
            last = esp_timer_get_time();
    }
    return false;
}

uint64_t UsbTransport::now_ms() const {
    return esp_timer_get_time() / 1000;
}
th::ConnectionPhase UsbTransport::phase() {
    return usb_phase.load();
}
