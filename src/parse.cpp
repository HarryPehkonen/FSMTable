// FSMTable — the parser: text in, Machine out, with a diagnostic for every refusal.
//
// Split out of the single 754-line src/fsmtable.cpp on 2026-10-06. That file held four concerns --
// the format's vocabulary, the parser, the writer, the graph analysis -- and a reader looking for
// one had to walk past the other three. No line of logic changed: the bodies below are the same
// text, moved. The one structural change is the namespace: helpers that had internal linkage in
// anonymous namespaces are now named in fsmtable::detail (declared in fsmtable_detail.hpp) so a
// file boundary can reach them. include/ is untouched and no public header changed.

#include "fsmtable_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fsmtable {

// The format's vocabulary (character classes, tokens, bounded integers) lives in detail:: so names
// as generic as `trim` do not squat in the public namespace. The bodies in this file are the ones
// moved out of src/fsmtable.cpp, unchanged.
using namespace detail;

std::optional<Machine> parse(std::string_view text, Error& error) {
    std::vector<KindName> ignored; // the frozen two-argument form has nowhere to put them
    return parse(text, error, ignored);
}

std::optional<Machine> parse(std::string_view text, Error& error,
                             std::vector<KindName>& kind_names) {
    std::vector<StateAction> ignored; // the three-argument form has nowhere to put them
    return parse(text, error, kind_names, ignored);
}

std::optional<Machine> parse(std::string_view text, Error& error, std::vector<KindName>& kind_names,
                             std::vector<StateAction>& state_actions) {
    // Cleared first, so a failure reports no names and no decorations at all: the contract is
    // that a refused file produces no machine and nothing partial to go with it.
    kind_names.clear();
    state_actions.clear();
    std::vector<KindName> declared;       // declaration order, as written
    std::vector<StateAction> decorations; // one per decorated state, first mention first
    std::vector<int> decoration_lines;    // the line each decoration started on, for the check
    Machine machine;
    bool version_seen = false;
    bool machine_seen = false;
    bool initial_seen = false;
    bool any_directive_seen = false;

    // Rule 9: for one `from` plus `kind`, at most one row without a `when` clause and at
    // most one with. Two sets of keys, so the two counts are independent.
    std::set<std::pair<std::string, int>> unguarded_keys;
    std::set<std::pair<std::string, int>> guarded_keys;

    int line_number = 0;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        std::size_t end = text.find('\n', begin);
        const bool last = (end == std::string_view::npos);
        if (last)
            end = text.size();
        ++line_number;
        const std::string_view line = trim(text.substr(begin, end - begin));
        begin = end + 1;

        if (!line.empty() && line.front() != '#') {
            const std::vector<std::string_view> fields = split_fields(line);
            const std::string_view directive = fields[0];

            if (directive == "version") {
                if (any_directive_seen) {
                    return reject(error, line_number, "version is not the first directive");
                }
                if (fields.size() < 2)
                    return reject(error, line_number, "version needs a value");
                if (fields.size() > 2) {
                    return reject(error, line_number,
                                  "unexpected field " + quoted(fields[2]) + " after version");
                }
                if (fields[1] != "1") {
                    return reject(error, line_number, "unsupported version " + quoted(fields[1]));
                }
                version_seen = true;
            } else if (directive == "machine") {
                if (machine_seen)
                    return reject(error, line_number, "duplicate machine directive");
                if (fields.size() < 2)
                    return reject(error, line_number, "machine needs a name");
                if (fields.size() > 2) {
                    return reject(error, line_number,
                                  "unexpected field " + quoted(fields[2]) + " after machine");
                }
                if (!valid_name(fields[1])) {
                    return reject(error, line_number,
                                  "malformed machine name " + quoted(fields[1]));
                }
                machine.name = std::string(fields[1]);
                machine_seen = true;
            } else if (directive == "initial") {
                if (initial_seen)
                    return reject(error, line_number, "duplicate initial directive");
                if (fields.size() < 2)
                    return reject(error, line_number, "initial needs a state");
                if (fields.size() > 2) {
                    return reject(error, line_number,
                                  "unexpected field " + quoted(fields[2]) + " after initial");
                }
                if (!valid_name(fields[1])) {
                    return reject(error, line_number, "malformed state name " + quoted(fields[1]));
                }
                machine.initial = std::string(fields[1]);
                declare_state(machine, fields[1]);
                initial_seen = true;
            } else if (directive == "kind") {
                if (fields.size() < 3) {
                    return reject(error, line_number, "kind needs a name and a number");
                }
                if (!valid_name(fields[1])) {
                    return reject(error, line_number, "malformed kind name " + quoted(fields[1]));
                }
                if (fields[2] != "=") {
                    return reject(error, line_number,
                                  "expected '=' after the kind name, found " + quoted(fields[2]));
                }
                if (fields.size() < 4) {
                    return reject(error, line_number, "kind needs a number after '='");
                }
                if (fields.size() > 4) {
                    return reject(error, line_number,
                                  "unexpected field " + quoted(fields[4]) + " after kind");
                }
                long long value = 0;
                if (!parse_bounded_int(fields[3], 0, 255, value)) {
                    return reject(error, line_number,
                                  "kind value must be a number in 0..255: " + quoted(fields[3]));
                }
                if (find_kind_name(declared, fields[1]) != nullptr) {
                    return reject(error, line_number, "duplicate kind name " + quoted(fields[1]));
                }
                if (find_kind_number(declared, static_cast<int>(value)) != nullptr) {
                    return reject(error, line_number,
                                  "duplicate kind number " + std::to_string(value));
                }
                declared.push_back(KindName{std::string(fields[1]), static_cast<int>(value)});
            } else if (directive == "state") {
                if (fields.size() < 3) {
                    return reject(error, line_number, "state needs an entry or exit clause");
                }
                const std::string_view state_name = fields[1];
                if (!valid_name(state_name)) {
                    return reject(error, line_number, "malformed state name " + quoted(state_name));
                }
                // Two lines for one state merge: the clauses are independent, so
                // `state A entry x` and `state A exit y` are one decoration written twice.
                StateAction* decorated = nullptr;
                for (StateAction& entry : decorations) {
                    if (entry.state == state_name)
                        decorated = &entry;
                }
                if (decorated == nullptr) {
                    decorations.push_back(StateAction{std::string(state_name), "", ""});
                    decorated = &decorations.back();
                    decoration_lines.push_back(line_number);
                }

                std::size_t i = 2;
                bool had_exit = false;
                while (i < fields.size()) {
                    const std::string_view clause = fields[i];
                    if (clause != "entry" && clause != "exit") {
                        return reject(error, line_number,
                                      "unknown clause " + quoted(clause) + " on a state line");
                    }
                    const bool is_entry = clause == "entry";
                    if (is_entry && had_exit) {
                        return reject(error, line_number, "entry clause after exit clause");
                    }
                    if (is_entry && !decorated->enter.empty()) {
                        return reject(error, line_number, "duplicate entry clause for this state");
                    }
                    if (!is_entry && !decorated->exit.empty()) {
                        return reject(error, line_number, "duplicate exit clause for this state");
                    }
                    if (i + 1 >= fields.size()) {
                        return reject(error, line_number, std::string(clause) + " needs an action");
                    }
                    if (!valid_name(fields[i + 1])) {
                        return reject(error, line_number,
                                      "malformed action name " + quoted(fields[i + 1]));
                    }
                    if (is_entry) {
                        decorated->enter = std::string(fields[i + 1]);
                    } else {
                        decorated->exit = std::string(fields[i + 1]);
                        had_exit = true;
                    }
                    i += 2;
                }
            } else if (directive == "transition") {
                if (fields.size() < 4) {
                    return reject(error, line_number, "transition needs a from, a kind and a to");
                }
                const std::string_view from = fields[1];
                if (!valid_name(from)) {
                    return reject(error, line_number, "malformed state name " + quoted(from));
                }
                const ArrowKind arrow = read_arrow_kind(fields[2]);
                int kind = 0;
                if (arrow.what == KindSlot::Number) {
                    kind = arrow.number;
                } else if (arrow.what == KindSlot::Name) {
                    const KindName* declared_kind = find_kind_name(declared, arrow.name);
                    if (declared_kind == nullptr) {
                        return reject(error, line_number,
                                      "unknown kind name " + quoted(arrow.name));
                    }
                    kind = declared_kind->kind;
                } else {
                    return reject(error, line_number, "malformed kind token " + quoted(fields[2]));
                }
                const std::string_view to = fields[3];
                if (!valid_name(to)) {
                    return reject(error, line_number, "malformed state name " + quoted(to));
                }

                Transition row{};
                row.from = std::string(from);
                row.kind = kind;
                row.to = std::string(to);

                std::size_t i = 4;
                if (i < fields.size() && fields[i] == "when") {
                    if (i + 2 >= fields.size()) {
                        return reject(error, line_number, "when needs an operator and a value");
                    }
                    Op op = Op::Eq;
                    if (!parse_op(fields[i + 1], op)) {
                        return reject(error, line_number,
                                      "unknown operator " + quoted(fields[i + 1]));
                    }
                    long long value = 0;
                    if (!parse_bounded_int(fields[i + 2], kIntMin, kIntMax, value)) {
                        return reject(error, line_number,
                                      "when value out of range: " + quoted(fields[i + 2]));
                    }
                    row.has_when = true;
                    row.when_op = op;
                    row.when_value = static_cast<int>(value);
                    i += 3;
                }

                bool had_action = false;
                if (i < fields.size() && fields[i] == "action") {
                    if (i + 1 >= fields.size()) {
                        return reject(error, line_number, "action needs a name");
                    }
                    if (!valid_name(fields[i + 1])) {
                        return reject(error, line_number,
                                      "malformed action name " + quoted(fields[i + 1]));
                    }
                    row.action = std::string(fields[i + 1]);
                    had_action = true;
                    i += 2;
                }

                if (i != fields.size()) {
                    const std::string_view extra = fields[i];
                    if (extra == "when") {
                        return reject(error, line_number,
                                      had_action ? "when clause after action clause"
                                                 : "more than one when clause");
                    }
                    return reject(error, line_number,
                                  "unexpected field " + quoted(extra) + " after the clauses");
                }

                const std::pair<std::string, int> key{row.from, row.kind};
                auto& used = row.has_when ? guarded_keys : unguarded_keys;
                if (!used.insert(key).second) {
                    return reject(
                        error, line_number,
                        row.has_when
                            ? "ambiguous row: a second guarded row for this from and kind"
                            : "ambiguous row: a second unguarded row for this from and kind");
                }

                declare_state(machine, from);
                declare_state(machine, to);
                machine.transitions.push_back(std::move(row));
            } else {
                return reject(error, line_number, "unknown directive " + quoted(directive));
            }
            any_directive_seen = true;
        }

        if (last)
            break;
    }

    // A decoration for a state the machine does not have is a misspelling, and rule 7's pattern
    // cannot catch it: the name is well formed, it just names nothing.
    for (std::size_t i = 0; i < decorations.size(); ++i) {
        const bool known
            = std::find(machine.states.begin(), machine.states.end(), decorations[i].state)
              != machine.states.end();
        if (!known) {
            return reject(error, decoration_lines[i],
                          "state '" + decorations[i].state
                              + "' is decorated but no transition or initial line mentions it");
        }
    }

    if (!version_seen)
        return reject(error, 0, "no version directive");
    if (!machine_seen)
        return reject(error, 0, "no machine directive");
    if (!initial_seen)
        return reject(error, 0, "no initial directive");

    error.line = 0;
    error.message.clear();
    kind_names = declared;
    state_actions = decorations;
    return machine;
}

} // namespace fsmtable
