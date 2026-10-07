#pragma once
#include "meter.hpp"
#include <cstdio>
// Host-test fixture only; excluded from the ESP32 build.
class InstrumentFixture : public th::Transport {
    std::string response, primary = "C", secondary = "D", equivalent = "SER", freq = "1000",
                          level = "0.6";
    unsigned n = 0;
    mutable uint64_t clock = 0;

  public:
    std::string model = "TH2822E";
    bool write(const std::string &raw) override {
        auto s = raw.substr(0, raw.find_first_of("\r\n"));
        response = "";
        if (s == "*IDN?")
            response = model + " Handheld LCR Meter,TEST-FIXTURE,NO-DEVICE";
        else if (s == "FUNC:IMPA?")
            response = primary;
        else if (s == "FUNC:IMPB?")
            response = secondary;
        else if (s == "FUNC:EQU?")
            response = equivalent;
        else if (s == "FREQ?")
            response = freq;
        else if (s == "VOLT?")
            response = level;
        else if (s == "FETCh?") {
            ++n;
            double v = primary == "C" ? 6.8e-6 : primary == "L" ? 0.0047 : 1000;
            char b[100];
            std::snprintf(b, sizeof(b), "%.7e%s,N", v, primary == "DCR" ? "" : ",2.1000e-3");
            response = b;
        } else if (s.rfind("FUNC:IMPA ", 0) == 0) {
            primary = s.substr(10);
            secondary = "NULL";
        } else if (s.rfind("FUNC:IMPB ", 0) == 0)
            secondary = s.substr(10);
        else if (s.rfind("FUNC:EQU ", 0) == 0)
            equivalent = s.substr(9);
        else if (s.rfind("FREQ ", 0) == 0)
            freq = s.substr(5);
        else if (s.rfind("VOLT ", 0) == 0)
            level = s.substr(5);
        else
            return false;
        return true;
    }
    bool line(std::string &out, unsigned timeout) override {
        clock += response.empty() ? timeout : 1;
        if (response.empty())
            return false;
        out = response;
        response.clear();
        return true;
    }
    bool quiet(unsigned, unsigned) override {
        response.clear();
        return true;
    }
    void delay(unsigned ms) override {
        clock += ms;
    }
    uint64_t now_ms() const override {
        return clock;
    }
};
