// FSMTable — the text -> text direction: a list of renames, applied to a .fsm, surgically.
//
// New file (2026-10-08). Everything in it follows from one decision: the fleet's .fsm files
// carry comments that are load-bearing, and `dump` is canonical text with no comments in it, so
// a rename may not go through `dump`. `edit_line` therefore copies the bytes BETWEEN the fields
// and substitutes only the field the parse tree says carries a renamed name, and `rename` proves
// its own output by parsing it back and comparing it against the machine the input was, with the
// names substituted.
//
// The rename spelling a command line reads (`state:Old=New`) lives here too, beside the Rename
// it fills in, so SPEC.md rule 7's name pattern is checked in one place.

#include "fsmtable_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsmtable {

using namespace detail;

namespace {

// One rename list, split by the namespace each name belongs to. `machine` holds at most one entry
// (only the machine's own name can move in that namespace).
struct Plan {
    std::map<std::string, std::string> machine;
    std::map<std::string, std::string> state;
    std::map<std::string, std::string> kind;
};

std::string_view target_word(Rename::Target target) {
    switch (target) {
    case Rename::Target::Machine:
        return "machine";
    case Rename::Target::State:
        return "state";
    case Rename::Target::Kind:
        return "kind";
    }
    return "";
}

// What `name` is called after the rename: the new name when the list renames it, the name itself
// when it does not.
std::string_view mapped(const std::map<std::string, std::string>& table, std::string_view name) {
    const auto found = table.find(std::string(name));
    if (found == table.end())
        return name;
    return found->second;
}

// Where one field sits inside the trimmed line: the two offsets the splice needs. `split_fields`
// gives the field text without offsets, and a reader that has only the tokens cannot rebuild the
// line's own spacing.
struct Span {
    std::size_t begin;
    std::size_t end;
};

std::vector<Span> field_spans(std::string_view content) {
    std::vector<Span> spans;
    std::size_t i = 0;
    while (i < content.size()) {
        while (i < content.size() && is_space(content[i]))
            ++i;
        if (i >= content.size())
            break;
        const std::size_t begin = i;
        while (i < content.size() && !is_space(content[i]))
            ++i;
        spans.push_back(Span{begin, i});
    }
    return spans;
}

// The arrow slot of a row, `--<kind>-->`, with the name inside it moved. A row that spells its
// kind as a NUMBER keeps the number: a number is not a name, and the parse tree is what says
// which arrows carry a name at all (NAMED_KINDS.md). The token comes from a line the parser
// accepted, so the arrow shape is a fact about the input; the checks below are a floor rather
// than a case being handled.
std::string renamed_arrow(const std::map<std::string, std::string>& kinds, std::string_view token) {
    constexpr std::string_view kPrefix = "--";
    constexpr std::string_view kSuffix = "-->";
    if (token.size() <= kPrefix.size() + kSuffix.size())
        return std::string(token);
    if (token.substr(0, kPrefix.size()) != kPrefix
        || token.substr(token.size() - kSuffix.size()) != kSuffix) {
        return std::string(token);
    }
    const std::string_view inner
        = token.substr(kPrefix.size(), token.size() - kPrefix.size() - kSuffix.size());
    const std::string_view moved = mapped(kinds, inner);
    if (moved == inner)
        return std::string(token);
    return std::string(kPrefix) + std::string(moved) + std::string(kSuffix);
}

// One line of the file, rewritten in place: the leading whitespace, the gaps between the fields
// and everything after the last field are copied, and only the fields the list reaches are
// substituted. A comment line and a blank line are copied whole, which is what keeps a file's
// prose (and its blank-line layout) byte for byte.
std::string edit_line(std::string_view raw, const Plan& plan) {
    const std::string_view content = trim(raw);
    if (content.empty() || content.front() == '#')
        return std::string(raw);

    const std::size_t lead = static_cast<std::size_t>(content.data() - raw.data());
    const std::vector<std::string_view> fields = split_fields(content);
    const std::vector<Span> spans = field_spans(content);
    const std::string_view directive = fields.front();

    std::string out(raw.substr(0, lead));
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        std::string replacement(fields[i]);
        if (directive == "machine") {
            if (i == 1)
                replacement = std::string(mapped(plan.machine, fields[i]));
        } else if (directive == "initial" || directive == "state") {
            // `initial` declares the state it names and a `state` line decorates one, so both
            // carry a state name in their first field. The `state` line is a rename site the
            // card's list of sites did not name, and leaving it out would write a file that does
            // not parse — which rule 1 forbids (QUESTIONS.md Q16). The entry and exit action
            // names further along the same line are NOT renamed: an action is not a state.
            if (i == 1)
                replacement = std::string(mapped(plan.state, fields[i]));
        } else if (directive == "kind") {
            if (i == 1)
                replacement = std::string(mapped(plan.kind, fields[i]));
        } else if (directive == "transition") {
            if (i == 1 || i == 3)
                replacement = std::string(mapped(plan.state, fields[i]));
            else if (i == 2)
                replacement = renamed_arrow(plan.kind, fields[i]);
        }
        out.append(content.substr(cursor, spans[i].begin - cursor));
        out.append(replacement);
        cursor = spans[i].end;
    }
    out.append(content.substr(cursor));
    out.append(raw.substr(lead + content.size()));
    return out;
}

// The two machines a `rename` compares: the expected one is built here, the other comes from
// re-parsing the rewrite. `when_op`/`when_value` are compared only where a `when` clause exists,
// which is the same reading the dumper and the back ends use.
bool same_transition(const Transition& a, const Transition& b) {
    if (a.from != b.from || a.kind != b.kind || a.has_when != b.has_when)
        return false;
    if (a.has_when && (a.when_op != b.when_op || a.when_value != b.when_value))
        return false;
    return a.to == b.to && a.action == b.action;
}

bool same_machine(const Machine& a, const Machine& b) {
    if (a.name != b.name || a.initial != b.initial || a.states != b.states)
        return false;
    if (a.transitions.size() != b.transitions.size())
        return false;
    for (std::size_t i = 0; i < a.transitions.size(); ++i) {
        if (!same_transition(a.transitions[i], b.transitions[i]))
            return false;
    }
    return true;
}

bool same_kind_names(const std::vector<KindName>& a, const std::vector<KindName>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].name != b[i].name || a[i].kind != b[i].kind)
            return false;
    }
    return true;
}

// The refusal path of this file: the parser's `reject` contract (no result, a message, a line
// number) for the text a rename produces rather than for a machine.
std::optional<std::string> refuse(Error& error, std::string message) {
    error.line = 0;
    error.message = std::move(message);
    return std::nullopt;
}

// A message built from parts, without the temporary strings a `+` chain makes
// (performance-inefficient-string-concatenation says so, and it is right).
std::string joined(std::initializer_list<std::string_view> parts) {
    std::string out;
    for (std::string_view part : parts)
        out += part;
    return out;
}

// The collision test, over the names the WHOLE list produces rather than over each rename in
// turn: two names may not end up the same, and the message names both. That is what makes
// `A -> B` with `B -> A` a swap instead of a refusal (QUESTIONS.md Q15). `names` is the
// namespace's own list in the file's order — for a kind, the declared names in declaration order.
bool collision_free(const std::vector<std::string>& names,
                    const std::map<std::string, std::string>& table, std::string_view word,
                    Error& error) {
    std::map<std::string, std::string> holder; // a resulting name -> the name that claimed it
    for (const std::string& name : names) {
        const std::string result(mapped(table, name));
        const auto found = holder.find(result);
        if (found == holder.end()) {
            holder.emplace(result, name);
            continue;
        }
        error.line = 0;
        const bool mine_moves = table.find(name) != table.end();
        if (mine_moves) {
            error.message = joined({"renaming the ", word, " '", name, "' to '", result, "'"});
            error.message += joined({" collides with the ", word, " '", found->second, "'"});
            if (table.find(found->second) != table.end())
                error.message += joined({", also renamed to '", result, "'"});
        } else {
            error.message = joined({"renaming the ", word, " '", found->second, "' to '", result,
                                    "' collides with the ", word, " '", name, "'"});
        }
        return false;
    }
    return true;
}

} // namespace

bool parse_rename_spec(std::string_view spec, Rename& rename, std::string& message) {
    const std::size_t colon = spec.find(':');
    if (colon == std::string_view::npos) {
        message = quoted(spec) + " is not a rename: the shape is <target>:<old>=<new>";
        return false;
    }
    const std::string_view target = spec.substr(0, colon);
    Rename::Target what = Rename::Target::State;
    if (target == "machine") {
        what = Rename::Target::Machine;
    } else if (target == "state") {
        what = Rename::Target::State;
    } else if (target == "kind") {
        what = Rename::Target::Kind;
    } else {
        message = "unknown rename target " + quoted(target) + ": it is machine, state or kind";
        return false;
    }

    const std::string_view rest = spec.substr(colon + 1);
    const std::size_t equals = rest.find('=');
    if (equals == std::string_view::npos) {
        message = quoted(spec) + " has no '=': a rename is <target>:<old>=<new>";
        return false;
    }
    const std::string_view from = rest.substr(0, equals);
    const std::string_view to = rest.substr(equals + 1);
    if (!valid_name(from) || !valid_name(to)) {
        message = quoted(spec) + " does not name two identifiers (SPEC.md rule 7)";
        return false;
    }

    rename.target = what;
    rename.from = std::string(from);
    rename.to = std::string(to);
    return true;
}

std::optional<std::string> rename(std::string_view text, const std::vector<Rename>& renames,
                                  Error& error) {
    error.line = 0;
    error.message.clear();

    // The parse products of the input: the same call `parse` makes, so a text no machine comes
    // out of is refused with the parser's own line and message and no rename happens.
    std::vector<KindName> kind_names;
    const auto machine = parse(text, error, kind_names);
    if (!machine.has_value())
        return std::nullopt;

    Plan plan;
    for (const Rename& entry : renames) {
        // A caller that builds a Rename by hand, rather than through parse_rename_spec, gets the
        // same refusal a malformed command line would: two identifiers, or nothing.
        if (!valid_name(entry.from) || !valid_name(entry.to)) {
            return refuse(error, "a rename needs two identifiers, not " + quoted(entry.from)
                                     + " and " + quoted(entry.to));
        }
        std::map<std::string, std::string>* table = nullptr;
        switch (entry.target) {
        case Rename::Target::Machine:
            table = &plan.machine;
            break;
        case Rename::Target::State:
            table = &plan.state;
            break;
        case Rename::Target::Kind:
            table = &plan.kind;
            break;
        }
        if (table->find(entry.from) != table->end()) {
            return refuse(error, "two renames for the " + std::string(target_word(entry.target))
                                     + " " + quoted(entry.from));
        }
        table->emplace(entry.from, entry.to);
    }

    // Every old name has to be one the machine actually has. The machine's own name is checked
    // against the name the file wrote, so a misspelling is a refusal rather than a rename of
    // nothing.
    for (const Rename& entry : renames) {
        switch (entry.target) {
        case Rename::Target::Machine:
            if (machine->name != entry.from) {
                return refuse(error, "the machine is named " + quoted(machine->name) + ", not "
                                         + quoted(entry.from));
            }
            break;
        case Rename::Target::State:
            if (std::find(machine->states.begin(), machine->states.end(), entry.from)
                == machine->states.end()) {
                return refuse(error, "no state named " + quoted(entry.from));
            }
            break;
        case Rename::Target::Kind:
            if (find_kind_name(kind_names, entry.from) == nullptr) {
                return refuse(error, "no kind named " + quoted(entry.from));
            }
            break;
        }
    }

    if (!collision_free(machine->states, plan.state, "state", error))
        return std::nullopt;
    std::vector<std::string> declared;
    declared.reserve(kind_names.size());
    for (const KindName& entry : kind_names)
        declared.push_back(entry.name);
    if (!collision_free(declared, plan.kind, "kind", error))
        return std::nullopt;
    // The machine namespace holds one name, and two renames onto one new name were refused
    // above, so no machine collision can reach here.

    // The rewrite: line by line, byte for byte outside the fields the list reaches.
    std::string out;
    out.reserve(text.size() + 16);
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find('\n', begin);
        if (end == std::string_view::npos) {
            out += edit_line(text.substr(begin), plan);
            break;
        }
        out += edit_line(text.substr(begin, end - begin), plan);
        out += '\n';
        begin = end + 1;
    }

    // The proof. The parse tree is the judge: the output has to parse, and to the machine the
    // input was with the names substituted — kind names included, so a name that moved in a
    // comment-free way still has to have moved in the declaration. A rewrite that reached the
    // wrong field lands here rather than in a file.
    Machine expected = *machine;
    expected.name = std::string(mapped(plan.machine, expected.name));
    expected.initial = std::string(mapped(plan.state, expected.initial));
    for (std::string& state : expected.states)
        state = std::string(mapped(plan.state, state));
    for (Transition& row : expected.transitions) {
        row.from = std::string(mapped(plan.state, row.from));
        row.to = std::string(mapped(plan.state, row.to));
    }
    std::vector<KindName> expected_names;
    expected_names.reserve(kind_names.size());
    for (const KindName& entry : kind_names)
        expected_names.push_back(KindName{std::string(mapped(plan.kind, entry.name)), entry.kind});

    Error check{0, ""};
    std::vector<KindName> written_names;
    const auto written = parse(out, check, written_names);
    if (!written.has_value() || !same_machine(*written, expected)
        || !same_kind_names(written_names, expected_names)) {
        return refuse(error, "the rewrite did not reproduce the renamed machine");
    }
    return out;
}

} // namespace fsmtable
