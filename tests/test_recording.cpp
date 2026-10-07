#include "fixtures/instrument_fixture.hpp"
#include <algorithm>
#include <cassert>
using namespace th;
static int count(const InstrumentFixture &t, const std::string &s) {
    return std::count(t.writes.begin(), t.writes.end(), s + "\n");
}
static int selectors(const InstrumentFixture &t) {
    return std::count_if(t.writes.begin(), t.writes.end(), [](const std::string &s) {
        return s.rfind("CALCulate:RECording:", 0) == 0;
    });
}
int main() {
    Reading r;
    assert(parse_recording("1e-6,0", r) && r.secondary.number == 0);
    assert(parse_recording("----,2", r) && r.primary.status == Value::Overrange);
    assert(parse_recording("----", r) && r.secondary.status == Value::Overrange);
    for (auto bad : {"1,2,N", "1", "nan,2", "1,inf", "1,", "1,2extra"})
        assert(!parse_recording(bad, r));
    for (auto model : {"TH2822A", "TH2822C", "TH2822D", "TH2822E"}) {
        InstrumentFixture t;
        t.model = model;
        Session s(t);
        assert(s.connect() && s.apply({ActionType::RecordingInspect, ""}));
        assert(s.state.recording.known && !s.state.recording.enabled && selectors(t) == 0);
        assert(s.apply({ActionType::RecordingEnable, ""}));
        assert(s.state.recording.view == RecordingView::Present && !s.state.recording.live);
        assert(s.state.recording.value.primary.number == 6.8e-6);
        const auto n = selectors(t);
        assert(s.poll() && s.poll() && selectors(t) == n && count(t, "FETCh?") == 0);
        assert(s.apply({ActionType::RecordingEnable, ""}));
        assert(count(t, "CALC:REC:STAT ON") == 1); // No restart / loss of native records.
        for (auto a : {Action{ActionType::Frequency, "100"}, Action{ActionType::Equivalent, "PAL"},
                       Action{ActionType::Primary, "L"}, Action{ActionType::Secondary, "Q"},
                       Action{ActionType::Level, "0.3"}})
            assert(!s.apply(a) && s.state.ready && s.state.error == "error.rec_locked");
        assert(!s.apply({ActionType::ToleranceEnable, ""}) && !t.tolerance);
        assert(s.apply({ActionType::RecordingDisable, ""}));
        assert(s.state.recording.view == RecordingView::Unknown);
        assert(s.state.recording.value.primary.status == Value::Invalid);
        assert(s.apply({ActionType::Frequency, "100"}) && s.poll());
        assert(s.apply({ActionType::ToleranceEnable, ""}));
        assert(!s.apply({ActionType::RecordingEnable, ""}) && !t.recording);
        assert(s.apply({ActionType::ToleranceDisable, ""}));
        t.ignore_rec_writes = true;
        assert(!s.apply({ActionType::RecordingEnable, ""}) && !s.state.ready);
        assert(!s.state.recording.known);
    }
    { // Measured firmware: PRES returns hyphens but selects live recording display.
        InstrumentFixture t;
        t.firmware = "VER4.5.2307";
        t.recording = true;
        Session s(t);
        assert(s.connect() && s.apply({ActionType::RecordingInspect, ""}));
        assert(count(t, "CALC:REC:STAT ON") == 0 && count(t, "CALC:REC:STAT OFF") == 0);
        assert(s.state.recording.live && t.rec_view == RecordingView::Present);
        assert(s.poll() && s.state.recording.value.primary.number == 6.8e-6);
        auto stamp = s.state.recording.read_at_ms;
        t.rec_live = "7.1e-6,0.002,N";
        assert(s.poll() && s.state.recording.value.primary.number == 7.1e-6);
        assert(s.state.recording.read_at_ms > stamp);
        assert(s.apply({ActionType::RecordingSelect, "present"}));
        assert(s.apply({ActionType::RecordingInspect, ""}));
        assert(s.apply({ActionType::RecordingUpdate, ""}));
        assert(selectors(t) == 1); // Same tab / reopening / live update don't beep.
        assert(s.apply({ActionType::RecordingSelect, "avg"}));
        assert(t.rec_view == RecordingView::Average && !s.state.recording.live);
        assert(s.state.recording.value.primary.number == 6.9e-6);
        stamp = s.state.recording.read_at_ms;
        const auto fetches = count(t, "FETCh?");
        for (int i = 0; i < 10; ++i)
            assert(s.poll());
        assert(count(t, "FETCh?") == fetches && selectors(t) == 2);
        assert(s.state.recording.value.primary.number == 6.9e-6); // Never replaced with MAX.
        assert(s.state.recording.read_at_ms == stamp);
        assert(s.state.reading.primary.status == Value::Invalid);
        t.rec_avg = "6.95e-6,0";
        assert(s.apply({ActionType::RecordingUpdate, ""}));
        assert(selectors(t) == 3 && s.state.recording.value.primary.number == 6.95e-6);
        for (auto item : {"max", "min", "present"})
            assert(s.apply({ActionType::RecordingSelect, item}));
        assert(s.poll() && s.state.recording.live);
        assert(count(t, "CALC:REC:STAT ON") == 0 && count(t, "CALC:REC:STAT OFF") == 0);
        t.recording = false; // Front-panel stop clears stale snapshots and resumes FETCH.
        assert(s.poll() && !s.state.recording.enabled);
        assert(s.state.recording.view == RecordingView::Unknown);
        t.recording = true;
        assert(s.poll() && s.state.recording.view == RecordingView::Present);
        assert(selectors(t) == 7);
        s.disconnect("test");
        assert(!s.state.recording.known && s.state.recording.read_at_ms == 0);
    }
    for (const auto bad : {"garbage", ""}) {
        InstrumentFixture t;
        t.recording = true;
        Session s(t);
        assert(s.connect() && s.apply({ActionType::RecordingInspect, ""}));
        t.rec_avg = bad;
        assert(!s.apply({ActionType::RecordingSelect, "avg"}));
        assert(!s.state.ready && !s.state.recording.known);
        assert(s.state.recording.value.primary.status == Value::Invalid);
        assert(count(t, "CALCulate:RECording:AVERage?") == 1); // Timeout never retries selector.
        const auto n = t.writes.size();
        assert(!s.poll() && t.writes.size() == n);
    }
}
