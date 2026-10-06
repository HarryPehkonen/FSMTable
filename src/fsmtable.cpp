// fsmTable — stage A: a total text format, a parser, a canonical dump.
//
// "Total" is the design constraint the whole file is shaped by: every byte string is
// either a valid machine or an error with a line number. There is no input that reaches
// undefined behaviour, no partial machine on failure, and no allocation that depends on
// a value read out of the text before it has been checked.
#include "fsmtable.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fsmtable {
namespace {

constexpr std::size_t kMaxNameLength = 64;
constexpr long long kIntMin = -2147483648LL;
constexpr long long kIntMax = 2147483647LL;

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

bool is_ident_start(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }

bool is_ident_char(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }

bool is_digit(char c) { return c >= '0' && c <= '9'; }

std::string_view trim(std::string_view s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && is_space(s[begin]))
        ++begin;
    while (end > begin && is_space(s[end - 1]))
        --end;
    return s.substr(begin, end - begin);
}

// Fields are separated by one or more whitespace characters. A number split by
// whitespace therefore arrives here as two fields, which is how `-- 1 -->` and
// `when eq 1 2` are rejected rather than leniently re-joined.
std::vector<std::string_view> split_fields(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && is_space(line[i]))
            ++i;
        if (i >= line.size())
            break;
        const std::size_t start = i;
        while (i < line.size() && !is_space(line[i]))
            ++i;
        fields.push_back(line.substr(start, i - start));
    }
    return fields;
}

bool valid_name(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameLength)
        return false;
    if (!is_ident_start(name.front()))
        return false;
    for (const char c : name) {
        if (!is_ident_char(c))
            return false;
    }
    return true;
}

// A decimal integer that is consumed entirely and fits [lo, hi], scanned here rather than
// with std::from_chars so that the rules are this file's own and the whole thing is
// index-based: a leading '-' is the only sign accepted (a leading '+' is an error, as the
// format says), every remaining character must be a digit, and a magnitude that would
// overflow long long is refused instead of wrapping.
bool parse_bounded_int(std::string_view token, long long lo, long long hi, long long& out) {
    if (token.empty())
        return false;

    std::size_t i = 0;
    const bool negative = token[0] == '-';
    if (negative)
        i = 1;
    if (i >= token.size())
        return false;

    constexpr unsigned long long kCeiling = 1ULL << 63; // |LLONG_MIN|
    unsigned long long magnitude = 0;
    for (; i < token.size(); ++i) {
        if (!is_digit(token[i]))
            return false;
        const unsigned digit = static_cast<unsigned>(token[i] - '0');
        if (magnitude > (kCeiling - digit) / 10ULL)
            return false; // would not fit
        magnitude = magnitude * 10ULL + digit;
    }

    long long value = 0;
    if (negative) {
        value = magnitude == kCeiling ? std::numeric_limits<long long>::min()
                                      : -static_cast<long long>(magnitude);
    } else {
        if (magnitude > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
            return false;
        }
        value = static_cast<long long>(magnitude);
    }
    if (value < lo || value > hi)
        return false;

    out = value;
    return true;
}

// `--<digits>-->` as ONE token: `-- 1 -->` is an error, not a lenient parse.
bool parse_arrow_kind(std::string_view token, int& kind) {
    constexpr std::string_view prefix = "--";
    constexpr std::string_view suffix = "-->";
    if (token.size() <= prefix.size() + suffix.size())
        return false;
    if (token.substr(0, prefix.size()) != prefix)
        return false;
    if (token.substr(token.size() - suffix.size()) != suffix)
        return false;
    const std::string_view digits
        = token.substr(prefix.size(), token.size() - prefix.size() - suffix.size());
    for (const char c : digits) {
        if (!is_digit(c))
            return false;
    }
    long long value = 0;
    if (!parse_bounded_int(digits, 0, 255, value))
        return false;
    kind = static_cast<int>(value);
    return true;
}

bool parse_op(std::string_view token, Op& op) {
    if (token == "eq") {
        op = Op::Eq;
        return true;
    }
    if (token == "lt") {
        op = Op::Lt;
        return true;
    }
    if (token == "le") {
        op = Op::Le;
        return true;
    }
    if (token == "gt") {
        op = Op::Gt;
        return true;
    }
    if (token == "ge") {
        op = Op::Ge;
        return true;
    }
    return false;
}

std::string_view op_text(Op op) {
    switch (op) {
    case Op::Eq:
        return "eq";
    case Op::Lt:
        return "lt";
    case Op::Le:
        return "le";
    case Op::Gt:
        return "gt";
    case Op::Ge:
        return "ge";
    }
    return "eq";
}

void declare_state(Machine& machine, std::string_view name) {
    const auto known = std::find(machine.states.begin(), machine.states.end(), name);
    if (known == machine.states.end()) {
        machine.states.emplace_back(name);
    }
}

std::optional<Machine> reject(Error& error, int line, std::string message) {
    error.line = line;
    error.message = std::move(message);
    return std::nullopt;
}

std::string quoted(std::string_view token) { return "'" + std::string(token) + "'"; }

} // namespace

std::optional<Machine> parse(std::string_view text, Error& error) {
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
            } else if (directive == "transition") {
                if (fields.size() < 4) {
                    return reject(error, line_number, "transition needs a from, a kind and a to");
                }
                const std::string_view from = fields[1];
                if (!valid_name(from)) {
                    return reject(error, line_number, "malformed state name " + quoted(from));
                }
                int kind = 0;
                if (!parse_arrow_kind(fields[2], kind)) {
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

    if (!version_seen)
        return reject(error, 0, "no version directive");
    if (!machine_seen)
        return reject(error, 0, "no machine directive");
    if (!initial_seen)
        return reject(error, 0, "no initial directive");

    error.line = 0;
    error.message.clear();
    return machine;
}

std::string dump(const Machine& machine) {
    std::vector<const Transition*> rows;
    rows.reserve(machine.transitions.size());
    for (const Transition& row : machine.transitions) {
        rows.push_back(&row);
    }
    // Canonical order: from, kind, guarded-before-unguarded, to, action. The `when` values
    // are the last tiebreak, so a hand-built machine with duplicate rows still dumps
    // deterministically; a parsed machine can never need it (rule 9 rejects the case).
    std::sort(rows.begin(), rows.end(), [](const Transition* a, const Transition* b) {
        if (a->from != b->from)
            return a->from < b->from;
        if (a->kind != b->kind)
            return a->kind < b->kind;
        if (a->has_when != b->has_when)
            return a->has_when;
        if (a->to != b->to)
            return a->to < b->to;
        if (a->action != b->action)
            return a->action < b->action;
        if (a->when_op != b->when_op)
            return a->when_op < b->when_op;
        return a->when_value < b->when_value;
    });

    std::string out;
    out += "version 1\n";
    out += "machine " + machine.name + "\n";
    out += "initial " + machine.initial + "\n";
    for (const Transition* row : rows) {
        out += "transition " + row->from + " --" + std::to_string(row->kind) + "--> " + row->to;
        if (row->has_when) {
            out += " when ";
            out += op_text(row->when_op);
            out += " " + std::to_string(row->when_value);
        }
        if (!row->action.empty()) {
            out += " action " + row->action;
        }
        out += "\n";
    }
    return out;
}

namespace {

// Where `name` sits in `states`, or states.size() when it is not declared there. The
// analyses return a subset of `states`, so a name that is absent leads nowhere rather than
// being invented (QUESTIONS.md Q4).
std::size_t state_index(const std::vector<std::string>& states, const std::string& name) {
    for (std::size_t i = 0; i < states.size(); ++i) {
        if (states[i] == name)
            return i;
    }
    return states.size();
}

} // namespace

std::vector<std::string> unreachable(const Machine& machine) {
    // A worklist, not a recursive walk: a machine is a graph, and a cycle in it must not
    // become a stack overflow. Reachability is structural — a `when` clause gates whether a
    // row fires, not whether the row exists (QUESTIONS.md Q3) — and the initial state is
    // marked before anything else, so it can never come back as unreachable.
    const std::size_t none = machine.states.size();
    std::vector<char> reached(none, 0);
    std::vector<std::size_t> work;

    const auto visit = [&](const std::string& name) {
        const std::size_t index = state_index(machine.states, name);
        if (index != none && reached[index] == 0) {
            reached[index] = 1;
            work.push_back(index);
        }
    };

    visit(machine.initial);
    while (!work.empty()) {
        const std::size_t index = work.back();
        work.pop_back();
        for (const Transition& row : machine.transitions) {
            if (row.from == machine.states[index])
                visit(row.to);
        }
    }

    // Walking `states` in order is what makes the result first-appearance order: the list is
    // never sorted, and sorting it here would silently break the contract.
    std::vector<std::string> result;
    for (std::size_t i = 0; i < machine.states.size(); ++i) {
        if (reached[i] == 0)
            result.push_back(machine.states[i]);
    }
    return result;
}

std::vector<std::string> sink_states(const Machine& machine) {
    std::vector<std::string> result;
    for (const std::string& state : machine.states) {
        const bool has_outgoing
            = std::any_of(machine.transitions.begin(), machine.transitions.end(),
                          [&state](const Transition& row) { return row.from == state; });
        if (!has_outgoing)
            result.push_back(state);
    }
    return result;
}

} // namespace fsmtable
