// The protocol example's driver-side half: directives in, machine decisions out, and one definition
// per action the generated header declares. The machine decides what is legal; this file decides
// when to ask it.
#include "protocol.hpp"

#include <charconv>
#include <system_error>
#include <vector>

namespace {

// The actions reach the session's log through this pointer, installed for the duration of one
// directive. The compiled back end's action type is void (*)(const Event&) — no context parameter —
// so a caller-owned pointer is the only route (QUESTIONS.md Q11). The check is right in general and
// wrong here, exactly as it is for the calculator's registers: the alternative to this pointer is
// not a const one, it is not having the feature at all.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::vector<std::string>* g_log = nullptr;

void ran(const char* name) {
    if (g_log != nullptr)
        g_log->emplace_back(name);
}

// The format's values are ints, unsigned: digits only, and the whole word has to be them.
bool parse_value(std::string_view text, int& out) {
    if (text.empty())
        return false;
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return false;
    out = value;
    return true;
}

std::vector<std::string_view> words_of(std::string_view directive) {
    std::vector<std::string_view> words;
    std::size_t i = 0;
    while (i < directive.size()) {
        while (i < directive.size() && (directive[i] == ' ' || directive[i] == '\t'))
            ++i;
        const std::size_t start = i;
        while (i < directive.size() && directive[i] != ' ' && directive[i] != '\t')
            ++i;
        if (i > start)
            words.push_back(directive.substr(start, i - start));
    }
    return words;
}

} // namespace

namespace proto {

Session::Session() : machine_(fsmtable_generated::makeConnection()) { g_log = &log_; }

bool Session::feed(std::string_view directive, Step& step, std::string& error) {
    const std::vector<std::string_view> words = words_of(directive);
    if (words.empty()) {
        error = "empty directive";
        return false;
    }

    const std::string_view verb = words[0];
    EventKind kind = EventKind::tick; // assigned in every branch below
    bool takes_value = false;
    if (verb == "open") {
        kind = EventKind::open;
    } else if (verb == "syn_ack") {
        kind = EventKind::syn_ack;
    } else if (verb == "fin") {
        kind = EventKind::fin;
    } else if (verb == "reset") {
        kind = EventKind::reset;
    } else if (verb == "expire") {
        kind = EventKind::expire;
    } else if (verb == "data" || verb == "tick") {
        kind = verb == "data" ? EventKind::data : EventKind::tick;
        takes_value = true;
    } else {
        error = "unknown directive '" + std::string(verb) + "'";
        return false;
    }

    if (words.size() != (takes_value ? 2U : 1U)) {
        error = takes_value
                    ? "the '" + std::string(verb) + "' directive needs a value"
                    : "unexpected '" + std::string(words.at(1)) + "' after " + std::string(verb);
        return false;
    }
    int value = 0;
    if (takes_value && !parse_value(words.at(1), value)) {
        error = "'" + std::string(words.at(1)) + "' is not a value";
        return false;
    }

    step = Step{};
    step.from = std::string(machine_.currentStateName());
    log_.clear();
    g_log = &log_;
    step.matched = machine_.process(Event{kind, value});
    step.actions = log_;
    step.to = std::string(machine_.currentStateName());
    for (const std::string& action : step.actions) {
        if (action == "retransmit")
            ++retransmits_;
    }
    return true;
}

std::string_view Session::state_name() const { return machine_.currentStateName(); }

} // namespace proto

// The actions the generated header declares — its `entry` and `exit` clauses included, because a
// clause is an action like any other. A name the .fsm uses and this file does not define is a link
// error, so the two cannot drift apart quietly.
namespace fsmtable_generated {

void arm_timer(const ConnectionEvent& /*event*/) { ran("arm_timer"); }

void cancel_timer(const ConnectionEvent& /*event*/) { ran("cancel_timer"); }

void arm_time_wait(const ConnectionEvent& /*event*/) { ran("arm_time_wait"); }

void on_refused(const ConnectionEvent& /*event*/) { ran("on_refused"); }

void on_open(const ConnectionEvent& /*event*/) { ran("on_open"); }

void on_syn_ack(const ConnectionEvent& /*event*/) { ran("on_syn_ack"); }

void retransmit(const ConnectionEvent& /*event*/) { ran("retransmit"); }

void on_give_up(const ConnectionEvent& /*event*/) { ran("on_give_up"); }

void on_refused_while_opening(const ConnectionEvent& /*event*/) { ran("on_refused_while_opening"); }

void on_data(const ConnectionEvent& /*event*/) { ran("on_data"); }

void on_peer_fin(const ConnectionEvent& /*event*/) { ran("on_peer_fin"); }

void on_time_wait_over(const ConnectionEvent& /*event*/) { ran("on_time_wait_over"); }

void on_reopen(const ConnectionEvent& /*event*/) { ran("on_reopen"); }

} // namespace fsmtable_generated
