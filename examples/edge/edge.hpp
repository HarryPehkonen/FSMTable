// The Edge example's API: a stream of `0` and `1` bytes in, one line per reported edge out.
// main.cpp owns the streams; this owns the machine and the one thing the format cannot hold — which
// byte is which kind, and what to do with a byte that is neither.
#pragma once

#include "fsm_edge.hpp" // generated at build time from edge.fsm

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace edge {

// The generated types under the names this example uses for them. `EventKind` and its enumerators
// come straight from edge.fsm's own `kind` declarations — `zero` and `one` — so nothing here has to
// be kept in step with the machine by hand (NAMED_KINDS.md).
using EventKind = fsmtable_generated::EdgeKind;
using Event = fsmtable_generated::EdgeEvent;

// What one run of the detector produced.
struct Result {
    // false: a byte was neither `0` nor `1`. There is no kind for it and so no row to reach; the
    // machine's alphabet is its two kinds and it is total over them (edge.fsm), which leaves the
    // refusal to the driver. The run stops at the first such byte (README.md, "The refusal").
    bool ok = true;

    // The 1-based position of the byte that was refused; 0 when ok.
    std::size_t offset = 0;

    // The driver's message for a refusal; empty when ok.
    std::string error;

    // What the actions printed, in order: `0 --> 1` for a rising edge, `1 --> 0` for a falling one.
    // A bit that reports nothing adds nothing, and here that is most of them.
    std::vector<std::string> lines;
};

// The driver: the machine, and the output its two actions have added to.
class Reader {
public:
    Reader();

    // Reads a whole byte stream. Classifies each byte as `zero`, `one`, or neither; feeds the
    // machine; stops at the first byte that is neither. The classification is the driver's, not the
    // table's (README.md; QUESTIONS.md Q27).
    Result run(std::string_view bytes);

    // The machine's current state, exposed because it is the machine's own invariant and the tests
    // assert on it: a state here is one bit of memory, so it is the last bit seen, and a byte the
    // driver refused left it where it was.
    std::string_view state_name() const;

    // The two actions edge.fsm names, one definition each in edge.cpp. The compiled back end's
    // action type is void (*)(const Event&) — no context parameter — so they reach the run's reader
    // through a pointer installed for its duration (QUESTIONS.md Q11). `rising` is the one row that
    // reports a `one` after a `zero` (`SawZero --one--> SawOne`, printing `0 --> 1`); `falling` is
    // the row that reports a `zero` after a `one` (`SawOne --zero--> SawZero`). The four absorb
    // rows name no action at all, which is why `00` and `11` print nothing.
    void rising();
    void falling();

private:
    fsmgine::compiled::Machine<fsmtable_generated::EdgeState, Event> machine_;
    std::vector<std::string> out_; // what the actions have produced
};

// The run's reader, reached by the actions. `run` installs it; a reader that is not running is
// nobody's (the calculator's registers work the same way).
void install(Reader* reader);
Reader& reader();

} // namespace edge
