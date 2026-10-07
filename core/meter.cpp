#include "meter.hpp"
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
void Stats::add(Value v) {
    if (v.status != Value::Valid)
        return;
    if (!count) {
        min = max = mean = v.number;
        count = 1;
        return;
    }
    min = std::min(min, v.number);
    max = std::max(max, v.number);
    ++count;
    mean += (v.number - mean) / (double)count;
}
bool Session::fail(const std::string &why, const std::string &detail) {
    state.ready = false;
    state.reading = {};
    state.error = why;
    state.error_detail = detail;
    return false;
}
void Session::disconnect(const std::string &why) {
    state.connected = false;
    state.phase = ConnectionPhase::Disconnected;
    state.ready = false;
    state.reading = {};
    state.stats = {};
    state.hold = false;
    state.error = why;
}
static bool measurement_frame(const std::string &line) {
    Reading value;
    return parse_fetch(line, false, value) || parse_fetch(line, true, value);
}
bool Session::query(const std::string &cmd, std::string &out) {
    if (busy)
        return fail("error.reentry");
    busy = true;
    // Both TH2822/A/C and D/E manuals say any command stops Auto Fetch. Send IDN even if a stream
    // already exists; bounded demultiplexing discards preceding measurement frames.
    for (int attempt = 0; attempt < 2; ++attempt) {
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
            if (cmd != "FETCh?" && measurement_frame(out))
                continue;
            busy = false;
            return true;
        }
        // Only the identical query may retry. Quarantine late replies first.
        if (!io.healthy() || !io.quiet(1200, 4000))
            break;
    }
    busy = false;
    return fail("error.timeout", cmd);
}
bool Session::connect() {
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
    state.poll_ms = 1000;
    settle_ms = (state.model == Model::D && fields[1] == "VER4.5.2307") ? 800 : 1200;
    return refresh();
}
bool Session::refresh() {
    std::string s;
    if (!query("FUNC:IMPA?", s))
        return false;
    s = upper(trim(s));
    if (s != "L" && s != "C" && s != "R" && s != "Z" && s != "DCR")
        return fail("error.primary", "FUNC:IMPA? => [" + s + "]");
    if (s == "DCR" && !profile(state.model).dcr)
        return fail("error.dcr_profile");
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
            state.level = 0.6; // fixed hardware level from A/C datasheet, not a queried readback
    }
    state.ready = true;
    state.error = "";
    state.error_detail.clear();
    return true;
}
bool Session::set(const std::string &command, const std::string &readback,
                  const std::string &expect) {
    state.ready = false;
    state.reading = {};
    state.stats = {};
    state.hold = false;
    if (!io.healthy() || !io.write(command + "\n"))
        return fail("error.setting");
    io.delay(settle_ms);
    std::string got;
    if (!query(readback, got))
        return false;
    bool match = upper(trim(got)) == expect;
    if (readback == "FREQ?") {
        int hz = 0;
        match = parse_frequency(got, hz) && std::to_string(hz) == expect;
    }
    if (readback == "VOLT?") {
        double v = 0;
        match = parse_level(got, v) && std::abs(v - std::strtod(expect.c_str(), nullptr)) < 1e-6;
    }
    if (!match)
        return fail("error.readback", got + " / " + expect);
    // Re-read entire context, including secondary, after any setter. Never label data using
    // requested settings.
    return refresh();
}
bool Session::apply(const Action &a) {
    if (a.type == ActionType::Hold) {
        state.hold = !state.hold;
        return true;
    }
    if (a.type == ActionType::ClearStats) {
        state.stats = {};
        return true;
    }
    if (a.type == ActionType::PollInterval) {
        if (!state.connected || !state.ready)
            return false;
        if (a.value != "250" && a.value != "500" && a.value != "1000")
            return false;
        state.poll_ms = static_cast<unsigned>(std::strtoul(a.value.c_str(), nullptr, 10));
        return true; // Local scheduling only; no instrument command or statistics reset.
    }
    if (a.type == ActionType::Resync) {
        if (!state.connected)
            return false;
        state.reading = {};
        state.stats = {};
        state.hold = false;
        return refresh();
    }
    if (!state.ready || !profile(state.model).max_hz) {
        state.error = "error.locked";
        return false;
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
bool Session::poll() {
    if (!state.ready)
        return false;
    const State before = state;
    if (!refresh())
        return false;
    if (before.primary != state.primary || before.secondary != state.secondary ||
        before.equivalent != state.equivalent || before.hz != state.hz ||
        before.level != state.level) {
        state.stats = {};
        state.reading = {};
        state.hold = false;
    }
    std::string s;
    if (!query("FETCh?", s))
        return false;
    Reading r;
    if (!parse_fetch(s, state.primary == "DCR", r))
        return fail("error.measurement", s);
    if (!state.hold) {
        state.reading = r;
        state.stats.add(r.primary);
        ++state.sample_sequence;
    }
    return true;
}
} // namespace th
