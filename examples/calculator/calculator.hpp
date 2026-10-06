// The calculator example's API: tokens in, one Outcome out. The shell (main.cpp) owns the I/O,
// this file owns the machine and the registers, and the split is deliberate — everything that
// decides anything is reachable from a test without a pipe or a terminal.
#pragma once

#include "fsm_calculator.hpp" // generated at build time from calculator.fsm

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace calc {

// What one input line produced.
struct Outcome {
    enum class Kind {
        None,  // an empty line: nothing to say
        Value, // a result
        Error, // the line was not in the machine's language, or the arithmetic failed
    };

    Kind kind = Kind::None;
    long long value = 0;
    std::string message; // the text after "error: ", empty otherwise
};

// The generated kind enum under the name this example uses for it. `EventKind`, not `Kind`,
// because `Outcome` has a `Kind` of its own. The enumerators are the .fsm's own `kind`
// declarations, so `EventKind::add` is the file's word for kind 2 — nothing here has to be
// kept in step with the machine by hand (NAMED_KINDS.md).
using EventKind = fsmtable_generated::CalculatorKind;
using Token = std::pair<EventKind, int>;

// The numbers. The machine tracks what is legal and which operator is pending; this holds the
// running value and any arithmetic failure, because no row can hold a number.
struct Registers {
    long long value = 0;
    bool has_value = false;
    std::string error; // "division by zero", "overflow", "" when fine
};

// Actions reach the registers through these two. The compiled back end's action type is
// void (*)(const Event&) — there is no context parameter — so a caller-owned pointer is the
// only route (QUESTIONS.md Q11). Shell::run_line installs it for the duration of the line.
void install(Registers* registers);
Registers& registers();

// One line in, one outcome out. Never touches stdin, stdout or argv.
class Shell {
public:
    Shell();

    Outcome run_line(std::string_view line);

    // The machine's current state, which is the mode: which operand is expected, and whether a
    // failure has taken over. Exposed because it is the machine's own invariant and the tests
    // assert on it (for example, that `7 / 0` reached Error).
    std::string_view state_name() const;

private:
    fsmgine::compiled::Machine<fsmtable_generated::CalculatorState,
                               fsmtable_generated::CalculatorEvent>
        machine_;
    Registers registers_;
};

// The tokeniser, exposed because it is what turns text into events and its failures are worth
// testing directly: it writes the line's values into `values` and returns false with a message
// when the text cannot be read at all.
bool tokenise(std::string_view line, std::vector<Token>& tokens, std::string& error);

} // namespace calc
