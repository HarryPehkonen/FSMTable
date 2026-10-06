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

} // namespace fsmtable
