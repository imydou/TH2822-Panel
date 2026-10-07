#include "meter.hpp"
#include "traffic.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace th {
static std::string trim(std::string s) {
    auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == s.npos ? "" : s.substr(a, b - a + 1);
}
static std::string upper(std::string s) {
    for (char &c : s)
        c = std::toupper((unsigned char)c);
    return s;
}
const Profile &profile(Model m) {
    static const Profile p[] = {{Model::Unknown, "UNKNOWN", false, false, 0},
                                {Model::A, "TH2822A", false, false, 10000},
                                {Model::C, "TH2822C", false, false, 100000},
                                {Model::D, "TH2822D", true, true, 10000},
                                {Model::E, "TH2822E", true, true, 100000}};
    unsigned i = (unsigned)m;
    return p[i < 5 ? i : 0];
}
Model identify(const std::string &id) {
    const auto s = upper(trim(id.substr(0, id.find(','))));
    for (Model m : {Model::A, Model::C, Model::D, Model::E}) {
        std::string name = profile(m).name;
        if (s == name || s.rfind(name + " ", 0) == 0)
            return m;
    }
    return Model::Unknown;
}
bool valid_frequency(Model m, int hz) {
    return hz <= profile(m).max_hz &&
           (hz == 100 || hz == 120 || hz == 1000 || hz == 10000 || hz == 100000);
}
bool valid_level(Model m, double v) {
    return profile(m).max_hz && (v == 0.6 || (profile(m).selectable_level && (v == 0.3 || v == 1)));
}
bool parse_value(const std::string &raw, Value &v) {
    v = {};
    auto s = trim(raw);
    if (s.empty())
        return false;
    if (s.size() >= 3 && s.find_first_not_of('-') == s.npos) {
        v.status = Value::Overrange;
        return true;
    }
    // Explicit decimal grammar excludes NaN, Inf, hex and partially valid numbers.
    size_t i = 0;
    if (s[i] == '+' || s[i] == '-')
        ++i;
    bool digits = false;
    while (i < s.size() && std::isdigit((unsigned char)s[i])) {
        ++i;
        digits = true;
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && std::isdigit((unsigned char)s[i])) {
            ++i;
            digits = true;
        }
    }
    if (!digits)
        return false;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-'))
            ++i;
        size_t start = i;
        while (i < s.size() && std::isdigit((unsigned char)s[i]))
            ++i;
        if (i == start)
            return false;
    }
    if (i != s.size())
        return false;
    errno = 0;
    char *end = nullptr;
    double n = std::strtod(s.c_str(), &end);
    if (errno || !std::isfinite(n) || *end)
        return false;
    v = {Value::Valid, n};
    return true;
}
static std::vector<std::string> split(const std::string &s) {
    std::vector<std::string> v;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == ',') {
            v.push_back(trim(s.substr(start, i - start)));
            start = i + 1;
        }
    return v;
}
bool parse_fetch(const std::string &s, bool dcr, Reading &r) {
    r = {};
    auto v = split(s);
    if (v.size() != (dcr ? 2 : 3))
        return false;
    Reading t;
    if (!parse_value(v[0], t.primary) || (!dcr && !parse_value(v[1], t.secondary)))
        return false;
    t.bin = upper(v.back()); // Manual NR1; observed D firmware also returns N (tolerance disabled).
    if (t.bin.empty())
        return false;
    if (t.bin != "N") {
        // Manual NR1 accepts an optional sign; no documented BIN range is imposed here.
        size_t i = (t.bin[0] == '+' || t.bin[0] == '-') ? 1 : 0;
        if (i == t.bin.size() || t.bin.size() > 11)
            return false;
        for (; i < t.bin.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(t.bin[i])))
                return false;
    }
    r = t;
    return true;
}
bool parse_tolerance_range(const std::string &raw, int &percent) {
    const auto s = upper(trim(raw));
    if (s == "----") {
        percent = 0;
        return true;
    }
    const int values[] = {1, 5, 10, 20};
    for (int i = 0; i < 4; ++i)
        if (s == "BIN" + std::to_string(i + 1)) {
            percent = values[i];
            return true;
        }
    return false;
}
bool parse_frequency(const std::string &raw, int &hz) {
    auto s = upper(trim(raw));
    double scale = 1;
    if (s.size() >= 3 && s.substr(s.size() - 3) == "KHZ") {
        scale = 1000;
        s.resize(s.size() - 3);
    } else if (s.size() >= 2 && s.substr(s.size() - 2) == "HZ")
        s.resize(s.size() - 2);
    Value v;
    if (!parse_value(s, v) || v.status != Value::Valid)
        return false;
    double n = v.number * scale;
    if (n != 100 && n != 120 && n != 1000 && n != 10000 && n != 100000)
        return false;
    hz = (int)n;
    return true;
}
bool parse_level(const std::string &raw, double &n) {
    auto s = upper(trim(raw));
    if (!s.empty() && s.back() == 'V')
        s.pop_back();
    Value v;
    if (!parse_value(s, v) || v.status != Value::Valid ||
        (v.number != 0.3 && v.number != 0.6 && v.number != 1))
        return false;
    n = v.number;
    return true;
}
std::string format_value(Value v, const std::string &unit) {
    if (v.status == Value::Overrange)
        return "OL";
    if (v.status != Value::Valid)
        return "--";
    double n = v.number;
    const char *prefix = "";
    if (!unit.empty() && unit != "deg" && n != 0) {
        double a = std::abs(n);
        if (a < 1e-9) {
            n *= 1e12;
            prefix = "p";
        } else if (a < 1e-6) {
            n *= 1e9;
            prefix = "n";
        } else if (a < 1e-3) {
            n *= 1e6;
            prefix = "u";
        } else if (a < 1) {
            n *= 1e3;
            prefix = "m";
        } else if (a >= 1e6) {
            n /= 1e6;
            prefix = "M";
        } else if (a >= 1e3) {
            n /= 1e3;
            prefix = "k";
        }
    }
    char b[80];
    std::snprintf(b, sizeof(b), "%.6g%s%s%s", n, unit.empty() ? "" : " ", prefix, unit.c_str());
    return b;
}
std::string primary_unit(const std::string &s) {
    return s == "L" ? "H" : s == "C" ? "F" : (s == "R" || s == "Z" || s == "DCR") ? "ohm" : "";
}
std::string secondary_unit(const std::string &s) {
    return s == "ESR" ? "ohm" : s == "THETA" ? "deg" : "";
}
bool Framer::feed(uint8_t c, std::string &line) {
    if (bad)
        return false;
    if (c == '\r' || c == '\n') {
        if (pending.empty())
            return false;
        line = std::move(pending);
        pending.clear();
        return true;
    }
    if (c < 32 || c > 126 || pending.size() >= 191) {
        bad = true;
        pending.clear();
        return false;
    }
    pending.push_back((char)c);
    return false;
}
bool parse_recording(const std::string &s, Reading &r) {
    r = {};
    const auto fields = split(s);
    Reading next;
    if (fields.size() == 1) {
        Value empty;
        if (!parse_value(fields[0], empty) || empty.status != Value::Overrange)
            return false;
        next.primary = next.secondary = empty;
    } else if (fields.size() != 2 || !parse_value(fields[0], next.primary) ||
               !parse_value(fields[1], next.secondary))
        return false;
    r = next;
    return true;
}
ToleranceResult tolerance_result(const State &s) {
    const auto &t = s.tolerance;
    if (!s.connected || !s.ready || !t.known)
        return ToleranceResult::Unknown;
    if (!t.enabled)
        return ToleranceResult::Inactive;
    if (s.hold)
        return ToleranceResult::Held;
    if (!t.percent)
        return ToleranceResult::NoRange;
    if ((t.percent != 1 && t.percent != 5 && t.percent != 10 && t.percent != 20) ||
        t.nominal.status != Value::Valid || !std::isfinite(t.nominal.number) ||
        t.nominal.number == 0)
        return ToleranceResult::Invalid;
    if (t.deviation.status == Value::Overrange)
        return ToleranceResult::Overrange;
    if (t.deviation.status != Value::Valid || !std::isfinite(t.deviation.number) ||
        s.reading.primary.status != Value::Valid)
        return ToleranceResult::Invalid;
    return std::abs(t.deviation.number) <= t.percent ? ToleranceResult::Within
                                                     : ToleranceResult::Outside;
}
bool Session::fail(const std::string &why, const std::string &detail) {
    verified_context = false;
    traffic_log().add(io.now_ms(), 'E', why + " " + detail);
    state.ready = false;
    state.reading = {};
    state.error = why;
    state.recording.known = false;
    state.recording.value = {};
    state.recording.view = RecordingView::Unknown;
    state.recording.live = false;
    state.recording.read_at_ms = 0;
    ++state.recording.revision;
    state.tolerance.known = false;
    state.tolerance.deviation = {};
    state.error_detail = detail;
    return false;
}
void Session::disconnect(const std::string &why) {
    verified_context = false;
    traffic_log().add(io.now_ms(), 'E', why);
    state.connected = false;
    state.phase = ConnectionPhase::Disconnected;
    state.ready = false;
    state.reading = {};
    state.hold = false;
    state.error = why;
    state.tolerance = {};
    state.recording = {};
    state.sample_sequence = 0;
}
static bool measurement_frame(const std::string &line) {
    Reading value;
    return parse_fetch(line, false, value) || parse_fetch(line, true, value);
}
bool Session::query(const std::string &cmd, std::string &out, bool retry) {
    if (busy)
        return fail("error.reentry");
    busy = true;
    // Both TH2822/A/C and D/E manuals say any command stops Auto Fetch. Send IDN even if a stream
    // already exists; bounded demultiplexing discards preceding measurement frames.
    for (int attempt = 0; attempt < (retry ? 2 : 1); ++attempt) {
        traffic_log().add(io.now_ms(), 'T', cmd);
        if (!io.healthy() || !io.write(cmd + "\n")) {
            busy = false;
            return fail("error.write");
        }
        const auto deadline = io.now_ms() + 1800;
        unsigned lines = 0;
        while (++lines <= 32) {
            const auto now = io.now_ms();
            if (now >= deadline)
                break;
            const unsigned remaining = static_cast<unsigned>(deadline - now);
            if (!io.line(out, remaining))
                break;
            traffic_log().add(io.now_ms(), 'R', out);
            if (cmd != "FETCh?" &&
                !((cmd.rfind("CALC:REC:", 0) == 0 || cmd.rfind("CALCulate:RECording:", 0) == 0) &&
                  split(out).size() <= 2) &&
                measurement_frame(out))
                continue;
            busy = false;
            return true;
        }
        // Only the identical query may retry. Quarantine late replies first.
        if (!retry || !io.healthy() || !io.quiet(1200, 4000))
            break;
    }
    busy = false;
    return fail(retry ? "error.timeout" : "error.rec_timeout", cmd);
}
bool Session::connect() {
    verified_context = false;
    const auto locale = state.locale;
    state = State{};
    state.locale = locale;
    state.phase = ConnectionPhase::Identifying;
    std::string id;
    if (!query("*IDN?", id)) {
        state.phase = ConnectionPhase::SerialReady;
        return false;
    }
    const auto fields = split(id);
    if (id.size() > 160 || fields.size() != 3 || fields[0].empty() || fields[1].empty() ||
        fields[2].empty()) {
        state.phase = ConnectionPhase::SerialReady;
        return fail("error.identity");
    }
    state.identity = id;
    state.model = identify(id);
    if (profile(state.model).max_hz == 0) {
        state.phase = ConnectionPhase::Unsupported;
        const auto name = upper(trim(fields[0]));
        const bool series_only = name == "TH2822" || name.rfind("TH2822 ", 0) == 0;
        return fail(series_only ? "error.model_ambiguous" : "error.unsupported_model", id);
    }
    // Only a valid supported model/firmware/serial response establishes identity.
    state.firmware = fields[1];
    state.serial = fields[2];
    state.connected = true;
    state.phase = ConnectionPhase::Identified;
    // The touch panel owns acquisition: settings resume polling without physical RMT.
    state.poll_ms = 500;
    settle_ms = (state.model == Model::D && fields[1] == "VER4.5.2307") ? 800 : 1200;
    // Fast path is scoped to this E firmware; other variants retain their prior timing.
    fast_readback = state.model == Model::E && fields[1] == "VER4.5.2307";
    if (fast_readback)
        settle_ms = 100;
    return refresh();
}
bool Session::refresh(bool full) {
    std::string s;
    if (!query("FUNC:IMPA?", s))
        return false;
    s = upper(trim(s));
    if (s != "L" && s != "C" && s != "R" && s != "Z" && s != "DCR")
        return fail("error.primary", "FUNC:IMPA? => [" + s + "]");
    if (s == "DCR" && !profile(state.model).dcr)
        return fail("error.dcr_profile");
    const bool read_settings = full || state.primary != s;
    state.primary = s;
    if (s == "DCR") {
        state.secondary = "NULL";
        state.equivalent = "";
        state.hz = 0;
        state.level = 0;
    } else {
        if (!query("FUNC:IMPB?", s))
            return false;
        s = upper(trim(s));
        if (s != "D" && s != "Q" && s != "THETA" && s != "ESR" && s != "NULL")
            return fail("error.secondary", "FUNC:IMPB? => [" + s + "]");
        state.secondary = s;
        if (read_settings) {
            if (!query("FUNC:EQU?", s))
                return false;
            s = upper(trim(s));
            if (s != "SER" && s != "PAL")
                return fail("error.equivalent", "FUNC:EQU? => [" + s + "]");
            state.equivalent = s;
            if (!query("FREQ?", s))
                return false;
            if (!parse_frequency(s, state.hz) || !valid_frequency(state.model, state.hz))
                return fail("error.frequency", "FREQ? => [" + s + "]");
            if (profile(state.model).selectable_level) {
                if (!query("VOLT?", s))
                    return false;
                if (!parse_level(s, state.level))
                    return fail("error.level", "VOLT? => [" + s + "]");
            } else
                state.level =
                    0.6; // fixed hardware level from A/C datasheet, not a queried readback
        }
    }
    if (state.tolerance.monitored && !refresh_tolerance(read_settings))
        return false;
    if (state.recording.monitored && !refresh_recording())
        return false;
    state.ready = true;
    state.error = "";
    state.error_detail.clear();
    return true;
}
bool Session::refresh_tolerance(bool full) {
    state.tolerance.monitored = true;
    Tolerance next = state.tolerance;
    next.monitored = true;
    std::string response;
    if (!query("CALC:TOL:STAT?", response))
        return false;
    const auto status = upper(trim(response));
    if (status != "ON" && status != "OFF")
        return fail("error.tolerance", "STAT? => [" + response + "]");
    next.known = true;
    next.enabled = status == "ON";
    if (next.enabled && (full || !state.tolerance.known || !state.tolerance.enabled)) {
        if (!query("CALC:TOL:RANG?", response))
            return false;
        if (!parse_tolerance_range(response, next.percent))
            return fail("error.tolerance", "RANG? => [" + response + "]");
        if (!query("CALC:TOL:NOM?", response))
            return false;
        if (!parse_value(response, next.nominal))
            return fail("error.tolerance", "NOM? => [" + response + "]");
    }
    if (!next.enabled) {
        next.percent = 0;
        next.nominal = {};
        next.deviation = {};
    }
    const auto &old = state.tolerance;
    if (old.known && old.enabled == next.enabled && old.percent == next.percent &&
        old.nominal.status == next.nominal.status && old.nominal.number == next.nominal.number)
        next.deviation = old.deviation;
    else {
        state.hold = false;
        state.reading = {};
    }
    state.tolerance = next;
    return true;
}
bool Session::refresh_recording() {
    auto &rec = state.recording;
    rec.monitored = true;
    std::string response;
    if (!query("CALC:REC:STAT?", response))
        return false;
    const auto status = upper(trim(response));
    if (status != "ON" && status != "OFF")
        return fail("error.recording", "STAT? => [" + response + "]");
    const bool enabled = status == "ON";
    if (!rec.known || rec.enabled != enabled) {
        rec.view = RecordingView::Unknown;
        rec.value = {};
        rec.live = false;
        rec.read_at_ms = 0;
        ++rec.revision;
        state.reading = {};
        state.hold = false;
    }
    rec.known = true;
    rec.enabled = enabled;
    return true;
}
bool Session::select_recording(RecordingView target, bool force) {
    auto &rec = state.recording;
    if (!rec.known || !rec.enabled || target == RecordingView::Unknown)
        return false;
    if (!force && rec.view == target)
        return true;
    const char *command = target == RecordingView::Present   ? "CALCulate:RECording:PRESent?"
                          : target == RecordingView::Maximum ? "CALCulate:RECording:MAXimum?"
                          : target == RecordingView::Average ? "CALCulate:RECording:AVERage?"
                                                             : "CALCulate:RECording:MINimum?";
    rec.view = RecordingView::Unknown;
    rec.value = {};
    rec.live = false;
    rec.read_at_ms = 0;
    state.reading = {};
    state.hold = false;
    std::string response;
    // These queries switch the instrument display and beep, even when already selected.
    // Never retry automatically, rotate them in the background, or restart REC.
    if (!query(command, response, false))
        return false;
    Reading value;
    if (!parse_recording(response, value))
        return fail("error.recording", std::string(command) + " => [" + response + "]");
    rec.view = target;
    rec.live = target == RecordingView::Present && state.model == Model::E &&
               state.firmware == "VER4.5.2307";
    if (rec.live) {
        // PRES returns ----- on this E, but selects current recording display. The direct
        // test verified FETCH after a 1.2 s gap; retain that gap only when entering Current.
        rec.value = {};
        io.delay(1200);
    } else {
        rec.value = value;
        if (state.primary == "DCR" || state.secondary == "NULL")
            rec.value.secondary = {};
        rec.read_at_ms = io.now_ms();
    }
    ++rec.revision;
    return true;
}
bool Session::set(const std::string &command, const std::string &readback,
                  const std::string &expect) {
    if (readback == "FUNC:IMPA?" || readback == "FUNC:IMPB?" || readback == "FREQ?" ||
        readback == "VOLT?") {
        if (!refresh_tolerance(false))
            return false;
        if (state.tolerance.enabled) {
            state.error = "error.tol_locked";
            state.error_detail.clear();
            traffic_log().add(io.now_ms(), 'E', state.error);
            return false;
        }
    }
    if (state.recording.monitored && command.rfind("CALC:", 0) != 0) {
        if (!refresh_recording())
            return false;
        if (state.recording.enabled) {
            state.error = "error.rec_locked";
            state.error_detail.clear();
            return false;
        }
    }
    state.ready = false;
    state.reading = {};
    state.hold = false;
    state.tolerance.deviation = {};
    traffic_log().add(io.now_ms(), 'T', command);
    if (!io.healthy() || !io.write(command + "\n"))
        return fail("error.setting");
    io.delay(settle_ms);
    std::string got;
    bool match = false;
    // Only readback queries repeat. A setting (especially TOL ON) is never resent.
    // Queries remain serial, so delayed responses cannot spill into a different command.
    for (unsigned attempt = 0; attempt < (fast_readback ? 4u : 1u); ++attempt) {
        if (attempt)
            io.delay(100u << (attempt - 1));
        if (!query(readback, got))
            return false;
        match = upper(trim(got)) == expect;
        if (readback == "FREQ?") {
            int hz = 0;
            match = parse_frequency(got, hz) && std::to_string(hz) == expect;
        }
        if (readback == "VOLT?") {
            double v = 0;
            match =
                parse_level(got, v) && std::abs(v - std::strtod(expect.c_str(), nullptr)) < 1e-6;
        }
        if (readback == "CALC:TOL:RANG?") {
            int percent = 0;
            match = parse_tolerance_range(got, percent) && std::to_string(percent) == expect;
        }
        if (match)
            break;
    }
    if (!match)
        return fail("error.readback", got + " / " + expect);
    // Apply only verified readback values. Do not query unrelated display settings after
    // changing level/equivalent/TOL; retain explicit full sync for externally changed settings.
    if (readback == "FREQ?")
        parse_frequency(got, state.hz);
    if (readback == "VOLT?")
        parse_level(got, state.level);
    if (readback == "FUNC:EQU?")
        state.equivalent = upper(trim(got));
    if (readback == "CALC:TOL:RANG?")
        parse_tolerance_range(got, state.tolerance.percent);
    // Keep the full post-setting verification, but let the immediately following sample
    // reuse it once instead of sending all the same metadata queries again.
    if (!refresh(false))
        return false;
    verified_context = true;
    verified_context_at = io.now_ms();
    return true;
}
bool Session::apply(const Action &a) {
    verified_context = false;
    if (a.type == ActionType::Hold) {
        state.hold = !state.hold;
        return true;
    }
    if (a.type == ActionType::ReservedLocalStats)
        return false;
    if (a.type == ActionType::PollInterval) {
        if (!state.connected || !state.ready)
            return false;
        if (a.value != "250" && a.value != "333" && a.value != "500" && a.value != "667" &&
            a.value != "1000")
            return false;
        state.poll_ms = static_cast<unsigned>(std::strtoul(a.value.c_str(), nullptr, 10));
        return true; // Local scheduling only; no instrument command or statistics reset.
    }
    if (a.type == ActionType::Resync) {
        if (!state.connected)
            return false;
        state.reading = {};
        state.hold = false;
        return refresh();
    }
    if (!state.ready || !profile(state.model).max_hz) {
        state.error = "error.locked";
        return false;
    }
    if (a.type == ActionType::RecordingInspect) {
        if (!refresh_recording())
            return false;
        return !state.recording.enabled || state.recording.view != RecordingView::Unknown ||
               select_recording(RecordingView::Present);
    }
    if (a.type == ActionType::RecordingSelect || a.type == ActionType::RecordingUpdate) {
        if (!refresh_recording())
            return false;
        auto target = a.value == "present" ? RecordingView::Present
                      : a.value == "max"   ? RecordingView::Maximum
                      : a.value == "avg"   ? RecordingView::Average
                      : a.value == "min"   ? RecordingView::Minimum
                                           : RecordingView::Unknown;
        if (a.type == ActionType::RecordingUpdate)
            target = state.recording.view;
        // Live Current already updates with FETCH; an update must not re-send PRES.
        if (a.type == ActionType::RecordingUpdate && state.recording.live)
            return true;
        return select_recording(target, a.type == ActionType::RecordingUpdate);
    }
    if (a.type == ActionType::RecordingEnable || a.type == ActionType::RecordingDisable) {
        if (!refresh_recording())
            return false;
        const bool enabled = a.type == ActionType::RecordingEnable;
        if (state.recording.enabled == enabled)
            return true;
        if (enabled) {
            if (!refresh_tolerance(false))
                return false;
            if (state.tolerance.enabled) {
                state.error = "error.tol_locked";
                state.error_detail.clear();
                return false;
            }
        }
        if (!set(std::string("CALC:REC:STAT ") + (enabled ? "ON" : "OFF"), "CALC:REC:STAT?",
                 enabled ? "ON" : "OFF"))
            return false;
        return !enabled || select_recording(RecordingView::Present);
    }
    if (a.type == ActionType::ToleranceInspect)
        return refresh_tolerance();
    if (a.type == ActionType::ToleranceEnable || a.type == ActionType::ToleranceDisable ||
        a.type == ActionType::ToleranceCapture || a.type == ActionType::ToleranceRange) {
        if (!refresh_tolerance(false))
            return false;
        if (a.type == ActionType::ToleranceRange) {
            if (!state.tolerance.enabled ||
                (a.value != "1" && a.value != "5" && a.value != "10" && a.value != "20"))
                return false;
            if (std::to_string(state.tolerance.percent) == a.value)
                return true;
            return set("CALC:TOL:RANG " + a.value, "CALC:TOL:RANG?", a.value);
        }
        const bool enable = a.type != ActionType::ToleranceDisable;
        if (enable && state.recording.monitored) {
            if (!refresh_recording())
                return false;
            if (state.recording.enabled) {
                state.error = "error.rec_locked";
                state.error_detail.clear();
                return false;
            }
        }
        if (a.type != ActionType::ToleranceCapture && state.tolerance.enabled == enable)
            return true;
        if (enable && (state.hold || state.reading.primary.status != Value::Valid ||
                       state.reading.primary.number == 0)) {
            state.error = "error.tol_reference";
            state.error_detail.clear();
            return false;
        }
        const int previous_range = state.tolerance.percent;
        // A deliberate recapture toggles native TOL. Never automatically repeat an ON write.
        if (enable && state.tolerance.enabled && !set("CALC:TOL:STAT OFF", "CALC:TOL:STAT?", "OFF"))
            return false;
        if (!set(std::string("CALC:TOL:STAT ") + (enable ? "ON" : "OFF"), "CALC:TOL:STAT?",
                 enable ? "ON" : "OFF"))
            return false;
        if (a.type == ActionType::ToleranceCapture && previous_range)
            return set("CALC:TOL:RANG " + std::to_string(previous_range), "CALC:TOL:RANG?",
                       std::to_string(previous_range));
        return true;
    }
    if (a.type == ActionType::Primary) {
        if (a.value != "L" && a.value != "C" && a.value != "R" && a.value != "Z" &&
            (a.value != "DCR" || !profile(state.model).dcr))
            return false;
        return set("FUNC:IMPA " + a.value, "FUNC:IMPA?", a.value);
    }
    if (state.primary == "DCR") {
        state.error = "error.ac_dcr";
        return false;
    }
    if (a.type == ActionType::Secondary) {
        if (a.value != "D" && a.value != "Q" && a.value != "THETA" && a.value != "ESR")
            return false;
        return set("FUNC:IMPB " + a.value, "FUNC:IMPB?", a.value);
    }
    if (a.type == ActionType::Equivalent) {
        if (a.value != "SER" && a.value != "PAL")
            return false;
        return set("FUNC:EQU " + a.value, "FUNC:EQU?", a.value);
    }
    if (a.type == ActionType::Frequency) {
        int hz;
        if (!parse_frequency(a.value, hz) || !valid_frequency(state.model, hz))
            return false;
        return set("FREQ " + std::to_string(hz), "FREQ?", std::to_string(hz));
    }
    if (a.type == ActionType::Level) {
        double v;
        if (!parse_level(a.value, v) || !valid_level(state.model, v) ||
            !profile(state.model).selectable_level)
            return false;
        return set("VOLT " + a.value, "VOLT?", a.value);
    }
    return false;
}
bool Session::poll(bool use_verified_context, bool (*action_waiting)()) {
    if (!state.ready)
        return false;
    if (action_waiting && action_waiting())
        return true;
    const State before = state;
    const bool reuse =
        use_verified_context && verified_context && io.now_ms() - verified_context_at <= 250;
    verified_context = false;
    if (!reuse && !refresh(false))
        return false;
    if (before.primary != state.primary || before.secondary != state.secondary ||
        before.equivalent != state.equivalent || before.hz != state.hz ||
        before.level != state.level) {
        state.reading = {};
        state.hold = false;
        state.tolerance.deviation = {};
    }
    // A queued setting may run before FETCH once metadata replies have all been consumed.
    if (action_waiting && action_waiting())
        return true;
    std::string s;
    Reading r;
    if (state.recording.known && state.recording.enabled) {
        auto &rec = state.recording;
        if (rec.view == RecordingView::Unknown && !select_recording(RecordingView::Present))
            return false;
        if (!rec.live) {
            state.reading = {}; // A statistical snapshot must never masquerade as live data.
            return true;        // Only harmless status queries while viewing a snapshot.
        }
        if (!query("FETCh?", s))
            return false;
        if (!parse_fetch(s, state.primary == "DCR", r))
            return fail("error.measurement", s);
        if (state.secondary == "NULL")
            r.secondary = {};
        rec.value = r;
        rec.read_at_ms = io.now_ms();
        ++rec.revision;
    } else {
        if (!query("FETCh?", s))
            return false;
        if (!parse_fetch(s, state.primary == "DCR", r))
            return fail("error.measurement", s);
    }
    // Do not start another query when a setting arrived during the completed FETCH.
    if (action_waiting && action_waiting())
        return true;
    Value deviation;
    if (state.tolerance.enabled && state.tolerance.known) {
        if (!query("CALC:TOL:VALU?", s))
            return false;
        if (!parse_value(s, deviation))
            return fail("error.tolerance", "VALU? => [" + s + "]");
    }
    if (!state.hold) {
        state.tolerance.deviation = deviation;
        state.reading = r;
        ++state.sample_sequence;
    }
    return true;
}
} // namespace th
