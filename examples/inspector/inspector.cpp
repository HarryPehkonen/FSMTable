// fsmtable-inspect — read a .fsm, report what its own text cannot show, walk its table over a list
// of events, and optionally write the canonical form back out.
//
// This is the example that uses the LIBRARY without the generator: nothing is generated here, and
// no machine is built at compile time. It is the smallest useful tool you can build on this format
// — a formatter, a checker, a tracer and a diagrammer for .fsm files, in about six hundred lines —
// and it is the only place in this repository where the analyses are used by something other than
// their own tests. Two of them are SPEC.md section 8's; the third is the addition beside them.
//
// Exit codes, the same convention as fsmtable-gen: 0 nothing to report, 1 the file could not be
// read, a state is unreachable, or a traced event matched no row, 2 the command line is wrong. A
// SINK state and a partially covered pair are reported and do not fail the run: ending somewhere
// is usually deliberate, and so is gating a row that has nothing behind it.
//
// `--trace` reads the TABLE, not a driver. It walks the rows the canonical order puts first, tests
// a `when` clause against the event's own value, and reports the exit/action/entry clauses
// ENTRY_EXIT.md composes; it never invents a value, and a clock, a socket or a retry count stays
// the caller's (examples/protocol/README.md). An event the table has no row for stops the walk.
//
// `--graph` is the second projection, beside `--canonical`: it writes the table as a Mermaid
// `stateDiagram-v2` — a node per state with its entry/exit clauses in the description, an edge per
// row labelled with its kind, `when` clause and action, the initial state marked with `[*]`, and
// every sink classed. Like `--canonical` it projects rather than reports, so it exits 0 whatever
// the analyses find. The node ids are positional (`s0`, `s1`, …) because a state may legally be
// named `state` or `end`, which a state diagram reads as syntax.
#include "fsmtable.hpp"

#include <cctype>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void usage(std::ostream& out) {
    out << "fsmtable-inspect — report what a .fsm's own text cannot show\n"
        << "\n"
        << "  fsmtable-inspect <file.fsm>              summarize it, and report findings\n"
        << "  fsmtable-inspect --canonical <file.fsm>  write the canonical form to stdout\n"
        << "  fsmtable-inspect --trace <file.fsm> <event>...\n"
        << "                                           walk the table over those events\n"
        << "  fsmtable-inspect --graph <file.fsm>      draw the machine as a Mermaid state "
           "diagram\n"
        << "  fsmtable-inspect --help\n"
        << "\n"
        << "An event is a kind — a declared kind name, or a number 0..255 — optionally followed\n"
        << "by =<int>, the value a `when` clause is compared against. No = means 0.\n"
        << "\n"
        << "Exit codes: 0 nothing to report, 1 the file could not be read, a state is\n"
        << "unreachable, or a traced event matched no row, 2 the command line is wrong.\n"
        << "--canonical and --graph project the machine instead of reporting on it, so they\n"
        << "exit 0 whatever the analyses find.\n";
}

bool read_file(const std::string& path, std::string& text) {
    std::ifstream in(path);
    if (!in.good())
        return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return true;
}

// A kind the way the generator names one: the declared name when the file declared it, the
// number otherwise, so the report reads the same way the machine's own text does.
std::string kind_label(int kind, const std::vector<fsmtable::KindName>& names) {
    for (const fsmtable::KindName& entry : names) {
        if (entry.kind == kind)
            return entry.name;
    }
    return std::to_string(kind);
}

std::string joined_pairs(const std::vector<fsmtable::PartialPair>& pairs,
                         const std::vector<fsmtable::KindName>& names) {
    std::string out;
    for (const fsmtable::PartialPair& pair : pairs) {
        if (!out.empty())
            out += ", ";
        out += pair.from + " --" + kind_label(pair.kind, names) + "-->";
    }
    return out;
}

std::string joined(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        if (!out.empty())
            out += ", ";
        out += name;
    }
    return out;
}

// ---------------------------------------------------------------- the trace

// One event of a trace: the kind that arrives, and the value riding on it.
struct TraceEvent {
    int kind = 0;
    int value = 0;
};

// SPEC.md rule 4 bounds a kind to 0..255 and rule 5 bounds a `when` value to 32 bits; the value a
// `when` clause is tested against is the same number that rides on the event, so an event takes
// that range too. The two limits are the format's, spelled out rather than guessed at.
constexpr long long kKindMax = 255;
constexpr long long kEventValueMin = -2147483648LL;
constexpr long long kEventValueMax = 2147483647LL;

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

// A decimal integer, optionally signed, within [lo, hi]. Index-based, the way the parser's own
// integer scanner is, and bounded while it reads rather than after the multiply has wrapped.
bool parse_int_token(std::string_view token, long long lo, long long hi, long long& out) {
    if (token.empty())
        return false;
    std::size_t i = 0;
    bool negative = false;
    if (token.front() == '-') {
        negative = true;
        i = 1;
    }
    if (i == token.size())
        return false;
    long long value = 0;
    for (; i < token.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(token[i])))
            return false;
        value = value * 10 + (token[i] - '0');
        if (value > 4294967296LL) // past any 32-bit range: further digits cannot bring it back
            return false;
    }
    if (negative)
        value = -value;
    if (value < lo || value > hi)
        return false;
    out = value;
    return true;
}

// A kind token the way a row spells it: the declared name, or the number (NAMED_KINDS.md). Both
// spellings mean the same machine, which is what makes the event list read like the file.
std::optional<int> resolve_kind(std::string_view token,
                                const std::vector<fsmtable::KindName>& names) {
    for (const fsmtable::KindName& entry : names) {
        if (entry.name == token)
            return entry.kind;
    }
    long long number = 0;
    if (parse_int_token(token, 0, kKindMax, number))
        return static_cast<int>(number);
    return std::nullopt;
}

// A `when` clause, tested against the event's own value. The same reading the back ends use
// (QUESTIONS.md Q5); the tool supplies nothing the event did not carry.
bool holds(const fsmtable::Transition& row, int value) {
    switch (row.when_op) {
    case fsmtable::Op::Eq:
        return value == row.when_value;
    case fsmtable::Op::Lt:
        return value < row.when_value;
    case fsmtable::Op::Le:
        return value <= row.when_value;
    case fsmtable::Op::Gt:
        return value > row.when_value;
    case fsmtable::Op::Ge:
        return value >= row.when_value;
    }
    return false;
}

const fsmtable::StateAction* find_state_action(const std::vector<fsmtable::StateAction>& actions,
                                               const std::string& state) {
    for (const fsmtable::StateAction& entry : actions) {
        if (entry.state == state)
            return &entry;
    }
    return nullptr;
}

// The events of a trace, in the order the command line gave them.
//
//     <kind>          a declared kind name, or a number 0..255
//     <kind>=<int>    the same, carrying the value a `when` clause is tested against
//
// A FLAT list: whitespace separates the events, `=` is the only punctuation, and there is no
// operator, no nesting, no variable and nothing to evaluate. A token that names no kind and is no
// number is the caller's mistake, reported with a message rather than guessed at.
bool read_events(std::string_view list, const std::vector<fsmtable::KindName>& names,
                 std::vector<TraceEvent>& events, std::string& message) {
    std::size_t i = 0;
    while (i < list.size()) {
        while (i < list.size() && is_space(list[i]))
            ++i;
        const std::size_t start = i;
        while (i < list.size() && !is_space(list[i]))
            ++i;
        if (i == start)
            break;
        const std::string_view item = list.substr(start, i - start);

        const std::size_t equals = item.find('=');
        const std::optional<int> kind = resolve_kind(item.substr(0, equals), names);
        if (!kind.has_value()) {
            message = "'" + std::string(item)
                      + "' is not an event: an event is a declared kind name or a number 0..255, "
                        "optionally =<int>";
            return false;
        }
        TraceEvent event;
        event.kind = *kind;
        if (equals != std::string_view::npos) {
            long long value = 0;
            if (!parse_int_token(item.substr(equals + 1), kEventValueMin, kEventValueMax, value)) {
                message = "'" + std::string(item) + "' carries no 32-bit integer after '='";
                return false;
            }
            event.value = static_cast<int>(value);
        }
        events.push_back(event);
    }
    return true;
}

// Where the file wrote a row: its 1-based line, and the line's own text with the comment stripped.
// SPEC.md section 4's Machine is frozen and carries no line numbers, so a diagnostic that points
// at a line finds it in the text again. A machine the parser accepted always places: the fields
// below are matched the way the format reads them, and rule 9 leaves at most one guarded and one
// unguarded row per (from, kind) for the `when` test to tell apart. `line` is 0 when a row cannot
// be placed, which no parsed machine produces — it is a floor, not a case being handled.
struct RowSite {
    int line = 0;
    std::string text;
};

std::vector<std::string_view> fields_of(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && is_space(line[i]))
            ++i;
        const std::size_t start = i;
        while (i < line.size() && !is_space(line[i]))
            ++i;
        if (i > start)
            fields.push_back(line.substr(start, i - start));
    }
    return fields;
}

std::string_view trim_view(std::string_view s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && is_space(s[begin]))
        ++begin;
    while (end > begin && is_space(s[end - 1]))
        --end;
    return s.substr(begin, end - begin);
}

// True when this line is the row the way the row was read: `transition <from> --<kind>--> <to>`
// with the same `when` presence. The arrow token is resolved the same way an event's kind is, so a
// file that wrote the number and one that wrote the name both match.
bool is_the_row(std::string_view line, const fsmtable::Transition& row,
                const std::vector<fsmtable::KindName>& names) {
    const std::vector<std::string_view> fields = fields_of(line);
    if (fields.size() < 4 || fields[0] != "transition" || fields[1] != row.from)
        return false;
    const std::string_view arrow = fields[2];
    if (arrow.size() < 5 || arrow.substr(0, 2) != "--" || arrow.substr(arrow.size() - 3) != "-->")
        return false;
    const std::optional<int> kind = resolve_kind(arrow.substr(2, arrow.size() - 5), names);
    if (!kind.has_value() || *kind != row.kind || fields[3] != row.to)
        return false;
    return (fields.size() > 4 && fields[4] == "when") == row.has_when;
}

RowSite locate_row(std::string_view text, const fsmtable::Transition& row,
                   const std::vector<fsmtable::KindName>& names) {
    int line = 1;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string_view raw = text.substr(start, end - start);
        const std::size_t hash = raw.find('#');
        const std::string_view written
            = trim_view(hash == std::string_view::npos ? raw : raw.substr(0, hash));
        if (is_the_row(written, row, names))
            return RowSite{line, std::string(written)};
        if (end == std::string_view::npos)
            break;
        start = end + 1;
        ++line;
    }
    return RowSite{};
}

// The calls ENTRY_EXIT.md composes for a row that fires, in the order they run: the source state's
// exit, the row's own action, then the target state's entry. Only the clauses a state carries are
// named; a row that runs nothing gets an empty string.
std::string clauses_of(const fsmtable::Transition& row,
                       const std::vector<fsmtable::StateAction>& state_actions) {
    std::string ran;
    const fsmtable::StateAction* leaving = find_state_action(state_actions, row.from);
    if (leaving != nullptr && !leaving->exit.empty())
        ran = "exit " + leaving->exit;
    if (!row.action.empty()) {
        if (!ran.empty())
            ran += ", ";
        ran += "action " + row.action;
    }
    const fsmtable::StateAction* arriving = find_state_action(state_actions, row.to);
    if (arriving != nullptr && !arriving->enter.empty()) {
        if (!ran.empty())
            ran += ", ";
        ran += "entry " + arriving->enter;
    }
    return ran;
}

// Walk the table over the events, one line per event: where it was, the kind that arrived, where
// it went, and what the clauses run on the way. The row for a pair is the guarded one when its
// clause holds and the unguarded one otherwise (rule 9: at most one of each). An event with
// neither stops the walk — the tool reports which row would have matched and returns 1.
int trace_run(const std::string& path, const fsmtable::Machine& machine,
              const std::vector<fsmtable::KindName>& names,
              const std::vector<fsmtable::StateAction>& state_actions,
              const std::vector<TraceEvent>& events, std::string_view text) {
    std::string current = machine.initial;
    std::cout << path << ": " << machine.name << " — trace of " << events.size()
              << " event(s) from " << machine.initial << "\n";
    for (std::size_t index = 0; index < events.size(); ++index) {
        const TraceEvent& event = events[index];
        const fsmtable::Transition* refined = nullptr;
        const fsmtable::Transition* plain = nullptr;
        for (const fsmtable::Transition& row : machine.transitions) {
            if (row.from != current || row.kind != event.kind)
                continue;
            if (row.has_when)
                refined = &row;
            else
                plain = &row;
        }
        const fsmtable::Transition* fired
            = refined != nullptr && holds(*refined, event.value) ? refined : plain;
        const std::string label = kind_label(event.kind, names);

        if (fired == nullptr) {
            std::cout << "  trace stopped at event " << (index + 1) << ": " << current << " --"
                      << label << "--> (value " << event.value << ") matched no row\n";
            if (refined != nullptr) {
                const RowSite site = locate_row(text, *refined, names);
                std::cout << "  the table's row for that pair refused it";
                if (site.line > 0)
                    std::cout << " — " << path << ":" << site.line << ": " << site.text;
                std::cout << "\n";
            } else {
                std::cout << "  the table has no row for " << current << " --" << label << "-->\n";
            }
            return 1;
        }

        std::cout << "  " << current << " --" << label << "--> " << fired->to;
        const std::string ran = clauses_of(*fired, state_actions);
        if (!ran.empty())
            std::cout << "  (" << ran << ")";
        std::cout << "\n";
        current = fired->to;
    }
    return 0;
}

// ---------------------------------------------------------------- the graph

// The `when` operator the way the format spells it (SPEC.md rule 6). The library's own `op_text`
// lives in the internal detail header, which an example that reads the public API does not reach
// for; the five names are the format's, and spelled the way the frozen reader checks them.
std::string op_label(fsmtable::Op op) {
    switch (op) {
    case fsmtable::Op::Eq:
        return "eq";
    case fsmtable::Op::Lt:
        return "lt";
    case fsmtable::Op::Le:
        return "le";
    case fsmtable::Op::Gt:
        return "gt";
    case fsmtable::Op::Ge:
        return "ge";
    }
    return "";
}

// The mermaid id of a state: s0, s1, … in the machine's first-appearance order. The state's NAME
// cannot be the id — `state`, `note`, `direction` and `end` are all legal state names and all
// syntax to a state diagram — so the id is positional and the name is the description, the shape
// KitCI's --graph gives its own nodes.
std::string state_id(std::size_t index) { return "s" + std::to_string(index); }

// Which id a state has: its index in the machine's own order. Every name here comes from `initial`
// or a row's own ends, and the parser declares all three, so a miss cannot happen for a machine
// the parser produced — the 0 is a floor, like locate_row's line 0, not a case being handled.
std::size_t state_index(const std::vector<std::string>& states, const std::string& name) {
    for (std::size_t i = 0; i < states.size(); ++i) {
        if (states[i] == name)
            return i;
    }
    return 0;
}

// The table drawn as a Mermaid `stateDiagram-v2`: the second projection, beside the canonical form,
// and for the same reason — it writes nothing back and reports nothing, so it returns 0 whatever
// the analyses would have found. What it draws is what `parse` read, in the machine's own order:
// states as they first appear, rows as the file wrote them. The one analysis it uses is
// `sink_states`, because a sink is the one finding a picture can carry.
void graph_run(const std::string& path, const fsmtable::Machine& machine,
               const std::vector<fsmtable::KindName>& names,
               const std::vector<fsmtable::StateAction>& state_actions) {
    std::cout << "stateDiagram-v2\n";
    std::cout << "    %% " << path << ": " << machine.name << " — " << machine.states.size()
              << " state(s), " << machine.transitions.size() << " row(s)\n";
    std::cout << "    %% a projection of the table: entry and exit are state descriptions, guards "
                 "and actions are edge labels\n";

    // One node per declared state, so a state no row touches still appears. The entry and exit
    // clauses are the node's description (ENTRY_EXIT.md's clauses belong to the state, not a row);
    // a state that carries neither is just its name, and emits nothing extra.
    for (std::size_t index = 0; index < machine.states.size(); ++index) {
        const std::string& state = machine.states[index];
        std::cout << "    state \"" << state;
        const fsmtable::StateAction* decorated = find_state_action(state_actions, state);
        if (decorated != nullptr) {
            if (!decorated->enter.empty())
                std::cout << "<br/>entry " << decorated->enter;
            if (!decorated->exit.empty())
                std::cout << "<br/>exit " << decorated->exit;
        }
        std::cout << "\" as " << state_id(index) << "\n";
    }

    std::cout << "    [*] --> " << state_id(state_index(machine.states, machine.initial)) << "\n";

    // One edge per row, labelled with the kind and then the row's own clauses, in the order a row
    // states them: the `when` value and the action. Two rows for one (from, kind) — the guarded one
    // and its fallback (rule 9) — are two edges, which is what the table holds.
    for (const fsmtable::Transition& row : machine.transitions) {
        std::cout << "    " << state_id(state_index(machine.states, row.from)) << " --> "
                  << state_id(state_index(machine.states, row.to)) << " : "
                  << kind_label(row.kind, names);
        if (row.has_when)
            std::cout << " when " << op_label(row.when_op) << " " << row.when_value;
        if (!row.action.empty())
            std::cout << " / " << row.action;
        std::cout << "\n";
    }

    // A sink is the one analysis a picture can carry, so it is the one thing drawn that the rows
    // alone do not say. A machine with no sink emits no style at all.
    const std::vector<std::string> sinks = fsmtable::sink_states(machine);
    if (!sinks.empty()) {
        std::cout << "    classDef sink fill:#3a0b0b,stroke:#f87171,color:#e2e8f0\n";
        std::cout << "    class ";
        for (std::size_t i = 0; i < sinks.size(); ++i) {
            if (i > 0)
                std::cout << ", ";
            std::cout << state_id(state_index(machine.states, sinks[i]));
        }
        std::cout << " sink\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    bool canonical = false;
    bool trace = false;
    bool graph = false;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        // A command line arrives as (int argc, char** argv), and reading argv[i] at all is pointer
        // arithmetic by this check's definition — the same reading .ci/tidy-baseline.txt records
        // for gen/fsmtable-gen.cpp. No argument-reading main avoids it, so the honest place to say
        // so is here rather than behind a cast that would hide the array.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const std::string arg = argv[i];
        if (arg == "--canonical") {
            canonical = true;
        } else if (arg == "--trace") {
            trace = true;
        } else if (arg == "--graph") {
            graph = true;
        } else if (arg == "--help" || arg == "-h") {
            usage(std::cout);
            return 0;
        } else if (!arg.empty() && arg.front() == '-') {
            std::cerr << "fsmtable-inspect: unknown option '" << arg << "'\n";
            usage(std::cerr);
            return 2;
        } else {
            positional.push_back(arg);
        }
    }

    // One mode at a time: --canonical writes the machine back out, --trace walks it, --graph draws
    // it, and the default reports on it.
    const int modes = (canonical ? 1 : 0) + (trace ? 1 : 0) + (graph ? 1 : 0);
    if (modes > 1) {
        std::cerr << "fsmtable-inspect: --canonical, --trace and --graph are one mode each\n";
        usage(std::cerr);
        return 2;
    }

    // In trace mode the first operand is the file and every one after it is an event. They arrive
    // as bare words — `--trace f.fsm number add` is four argv entries, and a quoted list is one —
    // so the tail is joined here and split again on whitespace by read_events.
    std::string path;
    std::string event_list;
    if (trace) {
        if (positional.size() < 2) {
            std::cerr << "fsmtable-inspect: --trace takes a file and at least one event\n";
            usage(std::cerr);
            return 2;
        }
        path = positional.front();
        for (std::size_t i = 1; i < positional.size(); ++i) {
            if (!event_list.empty())
                event_list += ' ';
            event_list += positional[i];
        }
    } else {
        if (positional.size() > 1) {
            std::cerr << "fsmtable-inspect: at most one input file\n";
            usage(std::cerr);
            return 2;
        }
        if (positional.empty()) {
            usage(std::cerr);
            return 2;
        }
        path = positional.front();
    }

    std::string text;
    if (!read_file(path, text)) {
        std::cerr << "fsmtable-inspect: cannot read " << path << "\n";
        return 1;
    }

    // The four-argument reading, so the names the file declared and its entry/exit clauses survive
    // into the canonical form (NAMED_KINDS.md, ENTRY_EXIT.md).
    fsmtable::Error error{0, ""};
    std::vector<fsmtable::KindName> kind_names;
    std::vector<fsmtable::StateAction> state_actions;
    const auto parsed = fsmtable::parse(text, error, kind_names, state_actions);
    if (!parsed.has_value()) {
        std::cerr << path << ":" << error.line << ": " << error.message << "\n";
        return 1;
    }
    const fsmtable::Machine& machine = *parsed;

    if (canonical) {
        std::cout << fsmtable::dump(machine, kind_names, state_actions);
        return 0;
    }

    if (graph) {
        // A projection, like --canonical: it draws the machine the table describes and reports
        // nothing of its own, so there is no code to return but 0.
        graph_run(path, machine, kind_names, state_actions);
        return 0;
    }

    if (trace) {
        // The declared names have to be known before an event can name one, which is why this
        // waits for the parse: an event list is read in the machine's own vocabulary.
        std::vector<TraceEvent> events;
        std::string message;
        if (!read_events(event_list, kind_names, events, message)) {
            std::cerr << "fsmtable-inspect: " << message << "\n";
            return 2;
        }
        return trace_run(path, machine, kind_names, state_actions, events, text);
    }

    const std::vector<std::string> unreachable = fsmtable::unreachable(machine);
    const std::vector<std::string> sinks = fsmtable::sink_states(machine);
    const std::vector<fsmtable::PartialPair> partial = fsmtable::partial_pairs(machine);

    std::cout << path << ": " << machine.name << " — " << machine.states.size() << " state(s), "
              << machine.transitions.size() << " row(s), " << kind_names.size()
              << " named kind(s)\n";
    std::cout << "  initial:  " << machine.initial << "\n";
    if (!sinks.empty()) {
        std::cout << "  sinks:    " << joined(sinks)
                  << "  (nothing leaves them; often deliberate)\n";
    }
    if (!partial.empty()) {
        std::cout << "  partial:  " << joined_pairs(partial, kind_names)
                  << "  (a guarded row with no unguarded row: a false guard leaves the event)\n";
    }
    if (!unreachable.empty()) {
        std::cout << "  UNREACHABLE: " << joined(unreachable) << "  (no path from "
                  << machine.initial << ")\n";
        return 1;
    }
    std::cout << "  every state is reachable from " << machine.initial << "\n";
    return 0;
}
