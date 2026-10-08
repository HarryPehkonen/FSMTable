// FSMTable — stage A public API, transcribed from SPEC.md section 4 (frozen).
#pragma once

#include <cstdint>
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

// The pairs a guard has to itself (2026-10-06). Not one of section 8's two — this one adds a
// name rather than changing either, the same way the side tables below add parse overloads.
//
// Rule 9 (frozen) allows at most one guarded row per (from, kind) and fixes the canonical order
// so the guarded row comes first, with the unguarded row behind it as the fallback. A pair with
// no unguarded row therefore has nothing to match when the guard is false: `process()` reports
// the event unhandled, and a driver that ignores that return value drops it. That is a legitimate
// shape — "ignore this event unless the budget allows" is exactly one — which is why this is an
// analysis a caller reports rather than a rule the parser enforces.
//
// `from` and `kind` are together the key: the same kind in two states is two pairs. The result is
// in first-appearance order and deduplicated, like `unreachable` and `sink_states`.
struct PartialPair {
    std::string from;
    int kind = 0;
};

std::vector<PartialPair> partial_pairs(const Machine& m);

// Named event kinds (NAMED_KINDS.md, QUESTIONS.md Q12). Section 2 is frozen and says the arrow
// slot holds a decimal integer, and section 4's block above is frozen too, so this is an
// addition in the shape stage B used rather than a change to either: a file may declare
//
//     kind <name> = <number>
//
// and then use `<name>` where a row would carry the number. The name is resolved while parsing,
// so `Machine` is the same machine either way and a caller that does not care about names keeps
// using the two-argument `parse` above.
//
// `kind_names` comes back in declaration order (the file's order, like `Machine::states`) and is
// empty when the text declares none. `dump(machine, kind_names)` is the matching writer: it
// emits the declarations in ascending kind order and then uses a name wherever one is declared,
// so `dump(parse(text, error, names), names)` parses back to the same machine and the same names.
struct KindName {
    std::string name;
    int kind = 0;
};

std::optional<Machine> parse(std::string_view text, Error& error,
                             std::vector<KindName>& kind_names);
std::string dump(const Machine& m, const std::vector<KindName>& kind_names);

// Entry and exit actions per state (ENTRY_EXIT.md, QUESTIONS.md Q13). Section 2 is frozen and
// says states are declared by first appearance, so a decoration does not declare anything: a
// `state` line for a name no row or `initial` line mentions is an error, not a new state.
//
//     state <name> [entry <action>] [exit <action>]
//
// At least one clause, `entry` before `exit`, each at most once, and two lines for one state
// merge. A row that fires from A to B runs A's `exit`, then the row's own action, then B's
// `entry` — the order statecharts use (Q13). A self-transition is an external one: it runs both.
//
// As with the kind names, this is carried beside the machine rather than inside it, so a caller
// that does not care keeps using the narrower parse. `state_actions` comes back with one entry
// per decorated state, in the order the file first mentions it.
struct StateAction {
    std::string state;
    std::string enter; // empty when the file named none
    std::string exit;  // empty when the file named none
};

std::optional<Machine> parse(std::string_view text, Error& error, std::vector<KindName>& kind_names,
                             std::vector<StateAction>& state_actions);
std::string dump(const Machine& m, const std::vector<KindName>& kind_names,
                 const std::vector<StateAction>& state_actions);

// The text -> text direction (2026-10-08). Not part of the frozen section 4 block above: this is
// an addition in the shape KindName and StateAction took, and it changes neither the row format
// nor the version number — what it writes is version 1 text.
//
// A rename names something and what to call it instead, spelled the way a tool's command line
// spells one:
//
//     machine:TrafficLight=Lamp    state:Red=Green    kind:tick=beat
//
// `parse_rename_spec` reads that spelling — it is also where SPEC.md rule 7's name pattern is
// checked, so a caller does not need a second validator — and `rename` applies a whole list of
// them.
//
// `rename` is a SURGICAL TEXT EDIT, not a dump. `dump` is canonical text with no comments in it,
// and the fleet's .fsm files carry comments that are load-bearing, so the rewrite touches only the
// fields that carry a renamed name — `machine`, `initial`, `state`, `kind`, and a row's `from`,
// `to` and the NAME inside its arrow — and every other byte of the file (every comment, every
// blank line, every directive the rename does not reach, the spacing between the fields) comes
// back exactly as it went in. A row that spells its kind as a NUMBER keeps its number: only a
// name moves, and only where the file wrote the name.
//
// The parse tree is the judge, twice over. `rename` parses `text` itself, so a text that does not
// parse is reported by `parse`'s own error and nothing is written; and it parses its own output
// and compares, so what comes back is proved to be the machine the input was with the requested
// names substituted.
//
// The list applies AT ONCE: `state:A=B state:B=A` is a swap, not a chase. The collision test is
// over the result of the whole list rather than each rename in turn (QUESTIONS.md Q15), and
// renaming a state onto a name a KIND holds is legal — the two are separate namespaces.
//
// Errors, all with `line == 0` (a rename is not tied to a line): an old name no directive uses,
// or a new name another name already holds — never a silent merge, and the message names both
// (the reading QUESTIONS.md Q16 records for a decorated state's line). A refused rename produces
// no text, the way a refused parse produces no machine.
struct Rename {
    // Which namespace the name belongs to. Named the way the spec spells it on a command line.
    enum class Target : std::uint8_t { Machine, State, Kind };

    Target target = Target::State;
    std::string from;
    std::string to;
};

// Reads `machine:Old=New`, `state:Old=New` or `kind:Old=New`. False when `spec` is not one of
// those three shapes or either name breaks rule 7, with `message` naming the fault and no
// half-filled `rename`.
bool parse_rename_spec(std::string_view spec, Rename& rename, std::string& message);

std::optional<std::string> rename(std::string_view text, const std::vector<Rename>& renames,
                                  Error& error);

} // namespace fsmtable
