#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace th {
enum class Model { Unknown, A, C, D, E };
struct Profile {
    Model model;
    const char *name;
    bool dcr, selectable_level;
    int max_hz;
};
const Profile &profile(Model);
Model identify(const std::string &);
bool valid_frequency(Model, int);
bool valid_level(Model, double);
struct Value {
    enum Status { Invalid, Valid, Overrange } status = Invalid;
    double number = 0;
};
struct Reading {
    Value primary, secondary;
    std::string bin;
};
bool parse_value(const std::string &, Value &);
bool parse_fetch(const std::string &, bool dcr, Reading &);
bool parse_frequency(const std::string &, int &);
bool parse_level(const std::string &, double &);
std::string format_value(Value, const std::string &unit);
std::string primary_unit(const std::string &);
std::string secondary_unit(const std::string &);
// Bounded CR/LF decoder. Overflow poisons the session instead of accepting a suffix.
class Framer {
    std::string pending;
    bool bad = false;

  public:
    bool feed(uint8_t, std::string &line);
    bool failed() const {
        return bad;
    }
    void reset() {
        pending.clear();
        bad = false;
    }
};
// Native REC data only: no panel-side accumulation or fabricated sample count.
enum class RecordingView { Unknown, Present, Maximum, Average, Minimum };
struct Recording {
    bool monitored = false, known = false, enabled = false;
    RecordingView view = RecordingView::Unknown;
    Reading value;
    bool live = false; // FETCH current values verified only on the measured E firmware.
    uint64_t read_at_ms = 0;
    uint64_t revision = 0;
};
bool parse_recording(const std::string &, Reading &);
enum class ConnectionPhase {
    Disconnected,
    UsbEnumerated,
    SerialReady,
    Identifying,
    Identified,
    Unsupported
};
struct Tolerance {
    bool monitored = false, known = false, enabled = false;
    int percent = 0; // 0 = no sorting range; BIN1..4 map to 1/5/10/20%.
    Value nominal, deviation;
};
bool parse_tolerance_range(const std::string &, int &);
struct State {
    Model model = Model::Unknown;
    std::string locale = "zh-CN", error_detail;
    std::string firmware, serial, usb_info;
    std::string identity = "", primary = "", secondary = "NULL", equivalent = "", error;
    int hz = 0;
    double level = 0;
    bool connected = false, ready = false, hold = false;
    ConnectionPhase phase = ConnectionPhase::Disconnected;
    unsigned poll_ms = 500;
    Reading reading;
    Tolerance tolerance;
    Recording recording;
    uint64_t sample_sequence = 0;
};
enum class ToleranceResult {
    Inactive,
    Unknown,
    NoRange,
    Held,
    Invalid,
    Overrange,
    Within,
    Outside
};
// Visual classification of the instrument-reported percentage; never recalculates deviation.
ToleranceResult tolerance_result(const State &);
enum class ActionType {
    Primary,
    Secondary,
    Frequency,
    Level,
    Equivalent,
    PollInterval,
    Hold,
    ReservedLocalStats, // Retired action 7; never sends an instrument command.
    Resync,
    Reconnect,
    Language,
    ToleranceInspect,
    ToleranceEnable,
    ToleranceDisable,
    ToleranceCapture,
    ToleranceRange,
    RecordingInspect,
    RecordingEnable,
    RecordingDisable,
    RecordingSelect,
    RecordingUpdate
};
struct Action {
    ActionType type;
    std::string value;
};
class Transport {
  public:
    virtual ~Transport() = default;
    virtual bool write(const std::string &) = 0;
    virtual bool line(std::string &, unsigned timeout_ms) = 0;
    // Discard late/unsolicited bytes, then establish a bounded quiet interval.
    virtual bool quiet(unsigned quiet_ms, unsigned max_ms) = 0;
    virtual void delay(unsigned) = 0;
    virtual uint64_t now_ms() const = 0;
    virtual bool healthy() const {
        return true;
    }
};
class Session {
    Transport &io;
    bool busy = false;
    unsigned settle_ms = 1200;
    bool fast_readback = false;
    bool verified_context = false;
    uint64_t verified_context_at = 0;
    bool query(const std::string &, std::string &, bool retry = true);
    bool fail(const std::string &, const std::string &detail = "");
    bool refresh(bool full = true);
    bool refresh_tolerance(bool full = true);
    bool refresh_recording();
    bool select_recording(RecordingView, bool force = false);
    bool set(const std::string &, const std::string &, const std::string &);

  public:
    State state;
    explicit Session(Transport &t) : io(t) {}
    bool connect();
    // Reuse only the immediately preceding successful setting readback (max 250 ms).
    // Yield only between completed query transactions, never with a response outstanding.
    bool poll(bool use_verified_context = false, bool (*action_waiting)() = nullptr);
    bool apply(const Action &);
    void disconnect(const std::string &);
};
} // namespace th
