// The protocol example's API: one directive in, what the machine did with it out. main.cpp owns the
// terminal; this owns the machine and the small amount of driver-side bookkeeping the format cannot
// hold. Everything that decides anything is reachable from a test without a pipe.
#pragma once

#include "fsm_protocol.hpp" // generated at build time from protocol.fsm

#include <string>
#include <string_view>
#include <vector>

namespace proto {

// The generated types under the names this example uses for them.
using EventKind = fsmtable_generated::ConnectionKind;
using Event = fsmtable_generated::ConnectionEvent;

// What one directive did.
struct Step {
    // false: no row fired, so the directive is not in the machine's language in this state. The
    // state is unchanged and no action ran. Reporting that is the driver's job — the table never
    // explains a refusal (QUESTIONS.md Q10).
    bool matched = false;
    std::string from;                 // the state before the directive
    std::string to;                   // the state after: equal to `from` when nothing matched
    std::vector<std::string> actions; // what ran, in the order the format ran it — the source
                                      // state's exit, the row's own action, the target's entry
};

// The driver: the machine, plus what no row can hold (a retry count). The compiled back end's
// action type is void (*)(const Event&) — no context parameter — so the actions reach this through
// a pointer installed for the duration of one directive (QUESTIONS.md Q11).
class Session {
public:
    Session();

    // Reads one directive and feeds it to the machine:
    //
    //     open   syn_ack   fin   reset   expire     one of the .fsm's kinds, no value
    //     data 64                                   the peer's packet size
    //     tick 1200                                 milliseconds that have passed
    //
    // Returns false and fills `error` when the *directive* cannot be read. That is the driver's
    // mistake rather than the machine's, and it is a different thing from a step that matched
    // nothing.
    bool feed(std::string_view directive, Step& step, std::string& error);

    std::string_view state_name() const;

    // How many `retransmit` actions have run. The machine cannot count them — no row holds a
    // counter and no guard compares two values — so a caller that gives up after N tries counts
    // here and sends `expire` itself. This example's script says `expire` outright.
    int retransmits() const { return retransmits_; }

private:
    fsmgine::compiled::Machine<fsmtable_generated::ConnectionState, Event> machine_;
    std::vector<std::string> log_;
    int retransmits_ = 0;
};

} // namespace proto
