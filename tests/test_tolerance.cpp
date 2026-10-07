#include "fixtures/instrument_fixture.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
using namespace th;
static unsigned count(const InstrumentFixture &t, const std::string &cmd) {
    return std::count(t.writes.begin(), t.writes.end(), cmd + "\n");
}
int main() {
    {
        State s;
        s.connected = s.ready = true;
        s.tolerance.known = s.tolerance.enabled = true;
        s.tolerance.nominal = {Value::Valid, 1e-6};
        s.reading.primary = {Value::Valid, 1e-6};
        for (int range : {1, 5, 10, 20}) {
            s.tolerance.percent = range;
            for (int sign : {-1, 1}) {
                s.tolerance.deviation = {Value::Valid, double(sign * range)};
                assert(tolerance_result(s) == ToleranceResult::Within);
                s.tolerance.deviation.number += sign * 0.001;
                assert(tolerance_result(s) == ToleranceResult::Outside);
            }
        }
        s.hold = true;
        assert(tolerance_result(s) == ToleranceResult::Held);
        s.hold = false;
        s.tolerance.percent = 0;
        assert(tolerance_result(s) == ToleranceResult::NoRange);
        s.tolerance.percent = 5;
        s.tolerance.deviation.status = Value::Overrange;
        assert(tolerance_result(s) == ToleranceResult::Overrange);
        s.tolerance.deviation = {Value::Valid, std::numeric_limits<double>::quiet_NaN()};
        assert(tolerance_result(s) == ToleranceResult::Invalid);
        s.tolerance.deviation = {Value::Valid, 0};
        s.tolerance.nominal.number = 0;
        assert(tolerance_result(s) == ToleranceResult::Invalid);
        s.ready = false;
        assert(tolerance_result(s) == ToleranceResult::Unknown);
    }

    int percent;
    for (auto pair : {std::pair{"BIN1", 1}, {"BIN2", 5}, {"BIN3", 10}, {"BIN4", 20}, {"----", 0}}) {
        assert(parse_tolerance_range(pair.first, percent) && percent == pair.second);
    }
    for (auto bad : {"BIN0", "BIN5", "5", "", "BIN2suffix", "nan"})
        assert(!parse_tolerance_range(bad, percent));
    for (auto model : {"TH2822A", "TH2822C", "TH2822D", "TH2822E"}) {
        InstrumentFixture t;
        t.model = model;
        Session s(t);
        assert(s.connect() && s.poll());
        assert(s.apply({ActionType::ToleranceInspect, ""}));
        assert(s.state.tolerance.known && !s.state.tolerance.enabled);
        assert(count(t, "CALC:TOL:STAT ON") == 0); // No implicit enable on connect/inspect.
        assert(!s.apply({ActionType::ToleranceRange, "5"}));
        assert(count(t, "CALC:TOL:RANG 5") == 0);
        assert(s.poll());
        assert(s.apply({ActionType::ToleranceEnable, ""}));
        assert(s.state.tolerance.enabled && s.state.tolerance.nominal.number == t.nominal);
        assert(s.state.tolerance.percent == 0 && count(t, "CALC:TOL:STAT ON") == 1);
        assert(s.apply({ActionType::ToleranceEnable, ""}));
        assert(count(t, "CALC:TOL:STAT ON") == 1); // Current value is not rewritten.
        for (int n : {1, 5, 10, 20}) {
            assert(s.apply({ActionType::ToleranceRange, std::to_string(n)}));
            assert(s.state.tolerance.percent == n);
            assert(s.poll());
        }
        t.deviation = -6.25;
        assert(s.poll() && s.state.tolerance.deviation.number == -6.25);
        assert(s.apply({ActionType::Hold, ""}));
        t.deviation = 4;
        assert(s.poll() && s.state.tolerance.deviation.number == -6.25);
        assert(!s.apply({ActionType::ToleranceCapture, ""})); // No hidden held reference.
        assert(s.apply({ActionType::Hold, ""}) && s.poll());
        assert(s.apply({ActionType::ToleranceCapture, ""}));
        assert(s.state.tolerance.enabled && s.state.tolerance.percent == 20);
        assert(count(t, "CALC:TOL:STAT ON") == 2 && count(t, "CALC:TOL:STAT OFF") == 1);
        const auto commands_before = t.writes.size();
        assert(!s.apply({ActionType::Frequency, "100"}));
        assert(s.state.error == "error.tol_locked" && s.state.ready);
        assert(t.writes.size() == commands_before + 1); // Only fresh STAT?, no setter.
        assert(s.apply({ActionType::ToleranceDisable, ""}));
        assert(s.apply({ActionType::Frequency, "100"}));
        assert(!s.state.tolerance.enabled); // Instrument auto-disables TOL; no re-enable.
        assert(count(t, "CALC:TOL:STAT ON") == 2);
        assert(s.poll());
        assert(s.apply({ActionType::ToleranceEnable, ""}));
        assert(s.apply({ActionType::ToleranceDisable, ""}));
        assert(!s.state.tolerance.enabled && s.state.tolerance.deviation.status == Value::Invalid);
        s.disconnect("test");
        assert(!s.state.tolerance.known && !s.state.tolerance.monitored);
    }
    { // Device ignores setting: visible mismatch, no automatic setter retry.
        InstrumentFixture t;
        t.ignore_tol_writes = true;
        Session s(t);
        assert(s.connect() && s.poll());
        assert(s.apply({ActionType::ToleranceInspect, ""}) && s.poll());
        assert(!s.apply({ActionType::ToleranceEnable, ""}));
        assert(!s.state.ready && !s.state.tolerance.known);
        assert(s.state.error == "error.readback" && count(t, "CALC:TOL:STAT ON") == 1);
        const auto n = t.writes.size();
        assert(!s.poll() && t.writes.size() == n);
    }
    { // TOL already on at connection: do not overwrite the stored reference.
        InstrumentFixture t;
        t.tolerance = true;
        t.tolerance_range = 2;
        Session s(t);
        assert(s.connect());
        assert(s.apply({ActionType::ToleranceInspect, ""}));
        assert(s.state.tolerance.enabled && s.state.tolerance.percent == 5);
        assert(count(t, "CALC:TOL:STAT ON") == 0);
        t.bad_range = "BIN99";
        assert(!s.apply({ActionType::Resync, ""}) && !s.state.ready &&
               s.state.error == "error.tolerance");
    }
    { // Invalid/zero reference must never capture automatically.
        InstrumentFixture t;
        Session s(t);
        assert(s.connect());
        assert(s.apply({ActionType::ToleranceInspect, ""}));
        for (Value v : {Value{}, Value{Value::Overrange, 0}, Value{Value::Valid, 0}}) {
            s.state.reading.primary = v;
            assert(!s.apply({ActionType::ToleranceEnable, ""}));
            assert(s.state.ready && count(t, "CALC:TOL:STAT ON") == 0);
        }
    }
    // Both cached ON and externally enabled TOL must block protected setters.
    for (bool external : {false, true}) {
        for (Action action :
             {Action{ActionType::Primary, "L"}, Action{ActionType::Secondary, "Q"},
              Action{ActionType::Frequency, "100"}, Action{ActionType::Level, "1"}}) {
            InstrumentFixture t;
            Session s(t);
            assert(s.connect() && s.apply({ActionType::ToleranceInspect, ""}) && s.poll());
            if (external) {
                t.tolerance = true;
                t.tolerance_range = 2;
            } else {
                assert(s.apply({ActionType::ToleranceEnable, ""}));
            }
            const auto start = t.writes.size();
            assert(!s.apply(action));
            assert(s.state.ready && s.state.tolerance.enabled &&
                   s.state.error == "error.tol_locked");
            for (size_t i = start; i < t.writes.size(); ++i)
                assert(t.writes[i].find('?') != std::string::npos);
            assert(s.apply({ActionType::ToleranceDisable, ""}));
            assert(s.apply(action));
        }
    }
    { // Failed OFF must not announce Measurement or admit a setting afterwards.
        InstrumentFixture t;
        t.tolerance = true;
        t.tolerance_range = 2;
        Session s(t);
        assert(s.connect() && s.apply({ActionType::ToleranceInspect, ""}) && s.poll());
        t.ignore_tol_writes = true;
        assert(!s.apply({ActionType::ToleranceDisable, ""}));
        assert(!s.state.ready && !s.state.tolerance.known && s.state.tolerance.enabled);
        auto n = t.writes.size();
        assert(!s.apply({ActionType::Level, "1"}) && t.writes.size() == n);
    }
    { // Already enabled before connection: query live deviation without forced recapture.
        InstrumentFixture t;
        t.tolerance = true;
        t.tolerance_range = 2;
        t.deviation = 1.25;
        Session s(t);
        assert(s.connect());
        assert(s.apply({ActionType::ToleranceInspect, ""}));
        const auto begin = t.writes.size();
        assert(s.poll() && s.state.tolerance.deviation.number == 1.25);
        const std::vector<std::string> expected = {
            "FUNC:IMPA?\n", "FUNC:IMPB?\n", "CALC:TOL:STAT?\n", "FETCh?\n", "CALC:TOL:VALU?\n"};
        assert(std::vector<std::string>(t.writes.begin() + begin, t.writes.end()) == expected);
        t.deviation = -2.5;
        assert(s.poll() && s.state.tolerance.deviation.number == -2.5);
        assert(count(t, "CALC:TOL:STAT ON") == 0 && count(t, "CALC:TOL:STAT OFF") == 0);
        assert(t.write("VOLT 1\n"));
        assert(s.poll() && s.state.level == 0.6); // Settings cached until explicit sync.
        assert(s.apply({ActionType::Resync, ""}) && s.state.level == 1);
        t.tolerance = false;
        assert(s.poll() && !s.state.tolerance.enabled);
        assert(s.state.tolerance.deviation.status == Value::Invalid);
    }
    { // Changing level/circuit/range must not trigger an unrelated frequency display query.
        InstrumentFixture t;
        t.tolerance = true;
        t.tolerance_range = 2;
        Session s(t);
        assert(s.connect());
        assert(s.apply({ActionType::ToleranceInspect, ""}) && s.poll());
        for (Action a :
             {Action{ActionType::Equivalent, "PAL"}, Action{ActionType::ToleranceRange, "10"}}) {
            auto f = count(t, "FREQ?"), n = count(t, "CALC:TOL:NOM?");
            assert(s.apply(a) && s.poll());
            assert(count(t, "FREQ?") == f && count(t, "CALC:TOL:NOM?") == n);
            assert(count(t, "CALC:TOL:STAT ON") == 0 && count(t, "CALC:TOL:STAT OFF") == 0);
        }
        assert(s.state.level == 0.6 && s.state.equivalent == "PAL" &&
               s.state.tolerance.percent == 10);
    }
    std::cout << "PASS: native TOL state/range/readback/recapture/hold/disconnect/error gating; "
                 "simulated A/C/D/E only\n";
}
