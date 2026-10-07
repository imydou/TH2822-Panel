#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "meter.hpp"
#include "usb/cdc_acm_host.h"
#include <atomic>
class UsbTransport : public th::Transport {
    cdc_acm_dev_hdl_t device = nullptr;
    QueueHandle_t rx;
    th::Framer framer;
    std::atomic<bool> alive{false}, overflow{false};
    static bool receive(const uint8_t *, size_t, void *);
    static void event(const cdc_acm_host_dev_event_data_t *, void *);

  public:
    UsbTransport();
    static void install();
    static void set_trace(bool);
    bool open();
    void close();
    bool connected() const {
        return alive;
    }
    bool write(const std::string &) override;
    bool line(std::string &, unsigned) override;
    bool quiet(unsigned, unsigned) override;
    void delay(unsigned) override;
    uint64_t now_ms() const override;
    bool healthy() const override {
        return alive && !overflow;
    }
    static th::ConnectionPhase phase();
    static std::string descriptor_status();
    static std::string descriptor_details();
};
