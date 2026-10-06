// fsmTable — stage A public API, transcribed from SPEC.md section 4 (frozen).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsmtable {

enum class Op { Eq, Lt, Le, Gt, Ge };

struct Error {
    int line;            // 1-based; 0 when the error is not tied to a line
    std::string message; // short, lowercase-ish, names the fault
};

struct Transition {
    std::string from;
    int kind;
    bool has_when;  // true when the row carried a `when` clause
    Op when_op;     // meaningful only when has_when
    int when_value; // meaningful only when has_when
    std::string to;
    std::string action; // empty when the row carried no `action` clause
};

struct Machine {
    std::string name;
    std::string initial;
    std::vector<std::string> states;     // first-appearance order, unique
    std::vector<Transition> transitions; // file order
};

std::optional<Machine> parse(std::string_view text, Error& error);
std::string dump(const Machine& m);

// Stage B (SPEC.md section 8). Not part of the frozen section 4 block above: section 8
// fixes these signatures, section 4 never mentioned them.
//
// Both return a subset of `m.states`, in first-appearance order, deduplicated. Neither
// mutates the machine, and neither can fail — a machine the parser produced is always
// analysable.

// States with no path from `m.initial`. The initial state is reachable by definition, so it
// is never in the result.
std::vector<std::string> unreachable(const Machine& m);

// States with no outgoing transition. A row with a `when` clause counts as outgoing: the
// clause gates whether the row fires, not whether it exists (QUESTIONS.md Q3).
std::vector<std::string> sink_states(const Machine& m);

} // namespace fsmtable
