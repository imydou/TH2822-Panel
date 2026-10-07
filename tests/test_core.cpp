#include "display_geometry.h"
#include "fixtures/instrument_fixture.hpp"
#include "meter.hpp"
#include "traffic.hpp"
#include <algorithm>
#include <cassert>
#include <deque>
#include <iostream>
using namespace th;
struct Fake : Transport {
    std::deque<std::string> answers;
    std::vector<std::string> writes;
    std::vector<unsigned> delays;
    bool quiet_ok = true, healthy_flag = true;
    uint64_t clock = 0;
    uint64_t now_ms() const override {
        return clock;
    }
    bool healthy() const override {
        return healthy_flag;
    }
    bool write(const std::string &s) override {
        assert(!s.empty() && s.back() == '\n' && s.find('\r') == std::string::npos);
        writes.push_back(s);
        return true;
    }
    bool line(std::string &s, unsigned timeout) override {
        clock += (answers.empty() || answers.front() == "TIMEOUT") ? timeout : 1;
        if (answers.empty())
            return false;
        s = answers.front();
        answers.pop_front();
        if (s == "TIMEOUT")
            return false;
        return true;
    }
    bool quiet(unsigned, unsigned) override {
        return quiet_ok;
    }
    void delay(unsigned t) override {
        delays.push_back(t);
        clock += t;
    }
};
static void connected(Fake &f, Session &s, const std::string &model = "TH2822D",
                      const std::string &version = "VER4.5.2307") {
    f.answers = {model + " Handheld LCR Meter," + version + ",SN123", "C", "D", "SER", "1kHz"};
    if (model == "TH2822D" || model == "TH2822E")
        f.answers.push_back("0.6V");
    assert(s.connect());
    assert(s.state.ready);
    assert(s.state.firmware == version && s.state.serial == "SN123");
}
static void poll_answers(Fake &f, const Session &s, std::initializer_list<std::string> readings) {
    f.answers = {s.state.primary};
    if (s.state.primary != "DCR") {
        f.answers.push_back(s.state.secondary);
    }
    if (s.state.tolerance.monitored)
        f.answers.push_back("OFF");
    for (auto &r : readings)
        f.answers.push_back(r);
}
static unsigned yield_checks = 0, yield_at = 0;
static bool waiting_action() {
    return ++yield_checks >= yield_at;
}
int main() {
    { // Bound RAM history, preserve ordering and neutralize LVGL recolor syntax.
        TrafficLog log;
        for (unsigned i = 0; i < 180; ++i)
            log.add(i, 'T', "FETCh?");
        auto entries = log.snapshot();
        assert(entries.size() == 80 && entries.front().sequence == 101 &&
               entries.back().sequence == 180);
        log.add(181, 'E', std::string(300, '#') + "\nignored");
        entries = log.snapshot();
        assert(entries.back().text[191] == 0 &&
               std::string(entries.back().text.data()).size() == 191);
        assert(std::string(entries.back().text.data()).find('#') == std::string::npos);
    }
    uint16_t pixels[] = {1, 2, 3, 4, 5, 6};
    panel_rotate_180(pixels, 6);
    for (unsigned i = 0; i < 6; ++i)
        assert(pixels[i] == 6 - i);
    assert(panel_mirror_coordinate(0, 800) == 799);
    assert(panel_mirror_coordinate(799, 800) == 0);
    for (auto m : {Model::A, Model::C, Model::D, Model::E}) {
        assert(valid_frequency(m, 100));
        assert(valid_frequency(m, 120));
        assert(valid_frequency(m, 1000));
        assert(valid_frequency(m, 10000));
        assert(!valid_frequency(m, 1001));
        assert(valid_level(m, 0.6));
    }
    assert(!valid_frequency(Model::D, 100000));
    assert(valid_frequency(Model::C, 100000));
    assert(valid_frequency(Model::E, 100000));
    assert(!valid_level(Model::A, 0.3));
    assert(!valid_level(Model::C, 1));
    assert(valid_level(Model::E, 1));
    assert(identify("TH2822E Handheld LCR Meter,v,s") == Model::E);
    assert(identify("TH2822EX,v,s") == Model::Unknown);
    assert(identify("NOT TH2822D,v,s") == Model::Unknown);
    assert(identify("TH2822,v,s") == Model::Unknown);
    Value v;
    for (auto bad : {"nan", "inf", "1e999", "1junk", "0x12", "", "--", "+", "1,2", "1e-"})
        assert(!parse_value(bad, v));
    assert(parse_value(" -1.25E-6 ", v) && v.status == Value::Valid);
    assert(parse_value("-----", v) && v.status == Value::Overrange);
    Reading r;
    assert(parse_fetch("1e-6,0.001,N", false, r));
    assert(parse_fetch("-----,1", true, r) && r.primary.status == Value::Overrange);
    assert(!parse_fetch("1,2,N", true, r));
    assert(!parse_fetch("1,N", false, r));
    assert(!parse_fetch("1,2,N,3", false, r));
    assert(!parse_fetch("nan,2,N", false, r));
    assert(!parse_fetch("1,2,garbage", false, r));
    assert(parse_fetch("1,2,+1", false, r));
    assert(parse_fetch("1,2,-200", false, r));
    assert(!parse_fetch("1,2,+", false, r));
    assert(!parse_fetch("1,2,1.0", false, r));
    assert(!parse_fetch("1,2,", false, r));
    int hz;
    assert(parse_frequency("100kHz", hz) && hz == 100000);
    assert(!parse_frequency("1.2kHz", hz));
    Framer fr;
    std::string out;
    int count = 0;
    for (uint8_t c : std::string("1,2,N\r\n3,4,N\n"))
        if (fr.feed(c, out))
            ++count;
    assert(count == 2);
    for (int i = 0; i < 192; ++i)
        fr.feed('a', out);
    assert(fr.failed());
    fr.reset();
    fr.feed(0, out);
    assert(fr.failed());
    { // First sample after a verified setting skips duplicate context queries, once only.
        InstrumentFixture t;
        t.firmware = "VER4.5.2307";
        Session s(t);
        assert(s.connect() && s.apply({ActionType::ToleranceInspect, ""}) &&
               s.apply({ActionType::RecordingInspect, ""}));
        assert(s.apply({ActionType::Frequency, "100"}));
        auto n = t.writes.size();
        assert(s.poll(true));
        assert(t.writes.size() == n + 1 && t.writes.back() == "FETCh?\n");
        n = t.writes.size();
        assert(s.poll(true));
        assert(t.writes.size() == n + 5); // Ordinary metadata monitoring resumes.
        assert(s.apply({ActionType::Frequency, "1000"}));
        t.delay(251);
        n = t.writes.size();
        assert(s.poll(true) && t.writes.size() == n + 5); // Never reuse old state.
        assert(s.apply({ActionType::Equivalent, "PAL"}));
        yield_checks = 0;
        yield_at = 2;
        n = t.writes.size();
        assert(s.poll(true, waiting_action) && t.writes.size() == n);
        assert(s.apply({ActionType::Secondary, "Q"})); // Still ready, no in-flight response.
        assert(s.poll(true) && s.state.secondary == "Q");
    }
    { // Yield before FETCH or before the extra TOL query, without fabricating a sample.
        InstrumentFixture t;
        Session s(t);
        assert(s.connect() && s.apply({ActionType::ToleranceInspect, ""}) &&
               s.apply({ActionType::RecordingInspect, ""}));
        auto n = t.writes.size();
        yield_checks = 0;
        yield_at = 2;
        assert(s.poll(false, waiting_action));
        assert(t.writes.size() == n + 4 && s.state.sample_sequence == 0 && s.state.ready);
        assert(s.poll() && s.state.sample_sequence == 1);
        assert(s.apply({ActionType::ToleranceEnable, ""}));
        n = t.writes.size();
        yield_checks = 0;
        yield_at = 3;
        assert(s.poll(false, waiting_action));
        assert(t.writes.back() == "FETCh?\n" && s.state.sample_sequence == 1);
        assert(s.poll() && s.state.sample_sequence == 2);
    }
    { // E fast readback succeeds without the old fixed 1200 ms idle.
        Fake f;
        Session s(f);
        connected(f, s, "TH2822E");
        assert(s.state.poll_ms == 500);
        f.answers = {"OFF", "100Hz", "C", "D", "OFF"};
        auto start = f.clock;
        assert(s.apply({ActionType::Frequency, "100"}));
        assert(f.delays == std::vector<unsigned>{100});
        assert(f.clock - start < 150);
    }
    { // Old value then confirmed value: repeat the same query, never the setter.
        Fake f;
        Session s(f);
        connected(f, s, "TH2822E");
        f.answers = {"OFF", "1kHz", "1kHz", "100Hz", "C", "D", "OFF"};
        assert(s.apply({ActionType::Frequency, "100"}));
        assert((f.delays == std::vector<unsigned>{100, 100, 200}));
        assert(std::count(f.writes.begin(), f.writes.end(), "FREQ 100\n") == 1);
    }
    { // Persistent mismatch is bounded and visible; no false success.
        Fake f;
        Session s(f);
        connected(f, s, "TH2822E");
        f.answers = {"OFF", "1kHz", "1kHz", "1kHz", "1kHz"};
        assert(!s.apply({ActionType::Frequency, "100"}));
        assert(s.state.error == "error.readback" && !s.state.ready);
        assert(std::count(f.writes.begin(), f.writes.end(), "FREQ?\n") == 5); // Includes connect.
        assert(std::count(f.writes.begin(), f.writes.end(), "FREQ 100\n") == 1);
    }
    {
        Fake f;
        Session s(f);
        connected(f, s);
        auto n = f.writes.size();
        assert(!s.apply({ActionType::Frequency, "100000"}));
        assert(f.writes.size() == n);
        poll_answers(f, s, {"TIMEOUT", "1e-6,0.01,N"});
        assert(s.poll());
        assert(f.writes[f.writes.size() - 1] == "FETCh?\n" &&
               f.writes[f.writes.size() - 2] == "FETCh?\n");
        assert(s.state.sample_sequence == 1);
        s.apply({ActionType::Hold, ""});
        poll_answers(f, s, {"2e-6,0.02,N"});
        assert(s.poll());
        assert(s.state.reading.primary.number == 1e-6 && s.state.sample_sequence == 1);
    }
    {
        Fake f;
        Session s(f);
        connected(f, s);
        f.answers = {"OFF", "100Hz", "C", "NULL", "OFF"};
        assert(s.apply({ActionType::Frequency, "100"}));
        assert(s.state.secondary == "NULL" && s.state.hz == 100 && f.delays[0] == 800);
    }
    {
        Fake f;
        Session s(f);
        connected(f, s, "TH2822E", "OTHER");
        f.answers = {"OFF", "100kHz", "C", "D", "OFF"};
        assert(s.apply({ActionType::Frequency, "100000"}));
        assert(f.delays[0] == 1200);
    }
    {
        Fake f;
        Session s(f);
        connected(f, s);
        f.answers = {"OFF", "120Hz"};
        assert(!s.apply({ActionType::Frequency, "100"}));
        assert(!s.state.ready && s.state.reading.primary.status == Value::Invalid);
        unsigned setters = 0;
        for (auto &w : f.writes)
            if (w == "FREQ 100\n")
                ++setters;
        assert(setters == 1);
    }
    {
        Fake f;
        Session s(f);
        connected(f, s);
        f.answers = {"OFF", "DCR", "DCR", "OFF"};
        assert(s.apply({ActionType::Primary, "DCR"}));
        assert(s.state.secondary == "NULL");
        auto n = f.writes.size();
        assert(!s.apply({ActionType::Level, "1"}));
        assert(f.writes.size() == n);
        poll_answers(f, s, {"1000,N"});
        assert(s.poll());
    }
    {
        Fake f;
        Session s(f);
        connected(f, s, "TH2822C");
        auto n = f.writes.size();
        assert(!s.apply({ActionType::Primary, "DCR"}));
        assert(!s.apply({ActionType::Level, "0.3"}));
        assert(f.writes.size() == n);
    }
    {
        Fake f;
        Session s(f);
        f.answers = {"TH2822UNKNOWN,v,s"};
        assert(!s.connect());
        assert(!s.state.ready && !s.state.connected);
        assert(!s.apply({ActionType::Primary, "C"}));
        assert(f.writes.size() == 1);
    }
    {
        Fake f;
        Session s(f);
        connected(f, s);
        poll_answers(f, s, {"1,2,N,extra"});
        assert(!s.poll());
        assert(!s.state.ready);
        s.disconnect("gone");
        assert(!s.state.connected && s.state.sample_sequence == 0);
    }
    {
        Fake f;
        Session s(f);
        connected(f, s);
        poll_answers(f, s, {"TIMEOUT", "TIMEOUT"});
        auto n = f.writes.size();
        assert(!s.poll());
        assert(f.writes.size() == n + 4);
    }
    {
        Fake f;
        Session s(f);
        connected(f, s);
        f.healthy_flag = false;
        auto n = f.writes.size();
        assert(!s.poll());
        assert(f.writes.size() == n);
    }
    {
        InstrumentFixture t;
        Session s(t);
        assert(s.connect());
        assert(s.poll());
        assert(s.state.model == Model::E);
        assert(s.apply({ActionType::Primary, "DCR"}));
        assert(s.poll());
    }
    { // A stream already active before connection must not masquerade as IDN.
        Fake f;
        Session s(f);
        f.answers = {"1e-6,0.1,N", "TH2822E Handheld LCR Meter,V1.2,SN", "C", "D", "SER", "1000",
                     "0.6"};
        assert(s.connect() && s.state.ready && s.state.connected);
        poll_answers(f, s, {"2e-6,0.02,N"});
        assert(s.poll() && s.state.reading.primary.number == 2e-6);
    }
    for (const auto &id : {"TH2822E,,SN", "TH2822E,V,", "TH2822E,V,SN,extra", "OTHER,V,SN",
                           "TH2822,V,SN", "TH2822 Handheld LCR Meter,V,SN"}) {
        Fake f;
        Session s(f);
        f.answers = {id};
        assert(!s.connect() && !s.state.connected && !s.state.ready);
        assert(f.writes.size() == 1);
    }
    {
        Fake f;
        Session s(f);
        f.answers = {"TH2822,V1,SN"};
        assert(!s.connect() && !s.state.connected && !s.state.ready);
        assert(s.state.error == "error.model_ambiguous");
        assert(f.writes.size() == 1);
        assert(!s.apply({ActionType::Primary, "C"}));
        assert(f.writes.size() == 1);
    }
    // Default touch-only flow: set/readback and the next poll need no Resync/RMT.
    for (const auto &name : {"TH2822A", "TH2822C", "TH2822D", "TH2822E"}) {
        InstrumentFixture t;
        t.model = name;
        Session s(t);
        assert(s.connect());
        assert(s.poll() && s.state.sample_sequence == 1);
        assert(s.apply({ActionType::Frequency, "100"}));
        assert(s.state.ready && s.state.hz == 100);
        assert(s.state.reading.primary.status == Value::Invalid);
        assert(s.poll() && s.state.reading.primary.status == Value::Valid);
        assert(s.state.sample_sequence == 2);
        for (const auto *interval : {"250", "333", "500", "667", "1000"})
            assert(s.apply({ActionType::PollInterval, interval}) &&
                   s.state.poll_ms == std::stoul(interval));
        assert(!s.apply({ActionType::PollInterval, "auto"}));
        assert(!s.apply({ActionType::PollInterval, "0"}));
        assert(s.state.sample_sequence == 2);
        s.disconnect("test");
        assert(s.connect());
    }
    // Real E incident: truncated response must remain an error, never guessed as 0.6V.
    {
        Fake f;
        Session s(f);
        connected(f, s, "TH2822E");
        f.answers = {"C", "NULL", "SER", "1000", "0."};
        assert(!s.apply({ActionType::Resync, ""}) && !s.state.ready);
        assert(s.state.error == "error.level");
        assert(s.state.error_detail == "VOLT? => [0.]");
        assert(s.state.reading.primary.status == Value::Invalid);
        auto n = f.writes.size();
        assert(!s.poll() && f.writes.size() == n);
    }
    for (const auto &field : {"secondary", "equivalent"}) {
        Fake f;
        Session s(f);
        connected(f, s, "TH2822E");
        f.answers = {"C"};
        if (std::string(field) == "equivalent")
            f.answers.push_back("D");
        f.answers.push_back("TRUNCATED");
        assert(
            !(std::string(field) == "equivalent" ? s.apply({ActionType::Resync, ""}) : s.poll()) &&
            !s.state.ready);
        assert(s.state.error == std::string("error.") + field);
        assert(s.state.error_detail.find("TRUNCATED") != std::string::npos);
    }
    std::cout
        << "PASS: capabilities, strict parsing, framing, overflow, state gating, DCR, readback, "
           "secondary reset, query retry, setter no-retry, hold/native-recording, disconnect, "
           "identity "
           "gating, stale-frame demux, touch-only setting/poll flow\n";
}
