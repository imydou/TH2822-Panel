#pragma once
#include "meter.hpp"
#include <cstdio>
#include <vector>
// Host-test fixture only; excluded from the ESP32 build.
class InstrumentFixture : public th::Transport {
    std::string response, primary = "C", secondary = "D", equivalent = "SER", freq = "1000",
                          level = "0.6";
    unsigned n = 0;
    mutable uint64_t clock = 0;

  public:
    std::string model = "TH2822E", firmware = "TEST-FIXTURE";
    bool recording = false, ignore_rec_writes = false;
    th::RecordingView rec_view = th::RecordingView::Unknown;
    std::string rec_live = "6.8e-6,0.003,N";
    std::string rec_present = "6.8e-6,0.003", rec_min = "6.6e-6,0.001", rec_max = "7.0e-6,0.008",
                rec_avg = "6.9e-6,0.004";
    bool tolerance = false, ignore_tol_writes = false, retain_tol_on_settings = false;
    int tolerance_range = 0;
    double nominal = 6.8e-6, deviation = 0;
    std::string bad_range;
    std::vector<std::string> writes;
    bool write(const std::string &raw) override {
        writes.push_back(raw);
        auto s = raw.substr(0, raw.find_first_of("\r\n"));
        response = "";
        if (s == "*IDN?")
            response = model + " Handheld LCR Meter," + firmware + ",NO-DEVICE";
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
        else if (s == "CALC:REC:STAT?")
            response = recording ? "ON" : "OFF";
        else if (s == "CALCulate:RECording:PRESent?") {
            rec_view = th::RecordingView::Present;
            response = model == "TH2822E" && firmware == "VER4.5.2307" ? "-----" : rec_present;
        } else if (s == "CALCulate:RECording:MINimum?") {
            rec_view = th::RecordingView::Minimum;
            response = rec_min;
        } else if (s == "CALCulate:RECording:MAXimum?") {
            rec_view = th::RecordingView::Maximum;
            response = rec_max;
        } else if (s == "CALCulate:RECording:AVERage?") {
            rec_view = th::RecordingView::Average;
            response = rec_avg;
        } else if (s == "CALC:REC:STAT ON" || s == "CALC:REC:STAT OFF") {
            if (!ignore_rec_writes)
                recording = s == "CALC:REC:STAT ON";
        } else if (s == "CALC:TOL:STAT?")
            response = tolerance ? "ON" : "OFF";
        else if (s == "CALC:TOL:RANG?")
            response = !bad_range.empty()             ? bad_range
                       : tolerance && tolerance_range ? "BIN" + std::to_string(tolerance_range)
                                                      : "----";
        else if (s == "CALC:TOL:NOM?") {
            char b[48];
            std::snprintf(b, sizeof(b), "%.8e", nominal);
            response = b;
        } else if (s == "CALC:TOL:VALU?")
            response = std::to_string(deviation);
        else if (s == "CALC:TOL:STAT ON" || s == "CALC:TOL:STAT OFF") {
            if (!ignore_tol_writes) {
                tolerance = s == "CALC:TOL:STAT ON";
                tolerance_range = 0;
            }
        } else if (s.rfind("CALC:TOL:RANG ", 0) == 0) {
            if (tolerance && !ignore_tol_writes) {
                const auto value = s.substr(14);
                tolerance_range = value == "1"    ? 1
                                  : value == "5"  ? 2
                                  : value == "10" ? 3
                                  : value == "20" ? 4
                                                  : 0;
            }
        } else if (s == "FETCh?") {
            ++n;
            double v = primary == "C" ? 6.8e-6 : primary == "L" ? 0.0047 : 1000;
            char b[100];
            std::snprintf(b, sizeof(b), "%.7e%s,N", v, primary == "DCR" ? "" : ",2.1000e-3");
            response = b;
            if (recording) {
                if (rec_view == th::RecordingView::Present)
                    response = rec_live;
                else if (rec_view == th::RecordingView::Minimum)
                    response = rec_min + ",N";
                else // Measured E: AVG FETCH incorrectly repeats MAX.
                    response = rec_max + ",N";
            }
        } else if (s.rfind("FUNC:IMPA ", 0) == 0) {
            primary = s.substr(10);
            secondary = "NULL";
            if (!retain_tol_on_settings)
                tolerance = false;
        } else if (s.rfind("FUNC:IMPB ", 0) == 0) {
            secondary = s.substr(10);
            if (!retain_tol_on_settings)
                tolerance = false;
        } else if (s.rfind("FUNC:EQU ", 0) == 0)
            equivalent = s.substr(9);
        else if (s.rfind("FREQ ", 0) == 0) {
            freq = s.substr(5);
            if (!retain_tol_on_settings)
                tolerance = false;
        } else if (s.rfind("VOLT ", 0) == 0)
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
