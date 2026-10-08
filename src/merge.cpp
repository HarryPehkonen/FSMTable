// FSMTable — the merge transform: two machines composed into one, by name fusion.
//
// New file (2026-10-08, tier 2b). Everything in it follows from the decision TRANSFORM.md states
// plainly: a merge is a UNION whose shared state names are the seam. The sibling verb moves a name
// out of the way when two names would collide (`rename`); this one relies on two machines naming
// the same state where they are meant to meet.
//
// The output is a NEW machine, which is why this file may go through `dump` where `rename` may not.
// The canonical form has no comments in it, and a merged machine has no comments of its own to
// lose: the caller's two files keep theirs, and `fsmtable-transform` writes the provenance into the
// new file's header instead (TRANSFORM.md).
//
// The one thing this file will not do is write a machine the format cannot read back. The rows of
// both machines are unioned, the union is DUMPED, and the dump is handed to `parse` again before it
// is returned — the same proof `rename` makes, one step further on, because here the union can also
// fail a rule neither input broke on its own (rule 9) and every state has to stay reachable from
// the merged initial. Both are refusals that name what disagreed rather than a file that is subtly
// wrong.

#include "fsmtable.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsmtable {

namespace {

// FNV-1a 64 over the text this file writes — the same algorithm, and therefore the same value, that
// gen/fsmtable-gen.cpp stamps into a generated header as the canonical-form fingerprint. That tool
// keeps its own copy: this card adds a verb, and moving a hash out of a shipped generator is a
// change to that generator rather than to this addition. Two artifacts are comparable because the
// algorithm is written down in both places, not because the two share a symbol.
std::uint64_t fnv1a(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL;
    }
    return hash;
}

// The refusal path of this file, the shape transform.cpp's `refuse` has: a merge error is not tied
// to a line of either input, so `line == 0` and the message carries what was ambiguous.
std::optional<MergeResult> refuse(Error& error, std::string message) {
    error.line = 0;
    error.message = std::move(message);
    return std::nullopt;
}

// A list of names as one readable line, so a report names every state it means rather than the
// first one.
std::string listed(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        if (!out.empty())
            out += ", ";
        out += name;
    }
    return out;
}

} // namespace

std::optional<MergeResult> merge(std::string_view first, std::string_view second, Error& error) {
    error.line = 0;
    error.message.clear();

    // The four-argument reading of both inputs, so the kind declarations and the entry/exit clauses
    // survive into the union. A text no machine comes out of is reported with the parser's own line
    // and message and no merge happens — the caller is told which input is at fault by the tool,
    // which knows the two file names.
    Error parsed{0, ""};
    std::vector<KindName> a_kinds;
    std::vector<StateAction> a_actions;
    const auto a_machine = parse(first, parsed, a_kinds, a_actions);
    if (!a_machine.has_value()) {
        error = parsed;
        return std::nullopt;
    }
    std::vector<KindName> b_kinds;
    std::vector<StateAction> b_actions;
    const auto b_machine = parse(second, parsed, b_kinds, b_actions);
    if (!b_machine.has_value()) {
        error = parsed;
        return std::nullopt;
    }

    // ---- the kind declarations: one name per number, one number per name, over the union.
    std::vector<KindName> declared = a_kinds;
    declared.insert(declared.end(), b_kinds.begin(), b_kinds.end());
    std::map<int, std::string> by_number;
    std::map<std::string, int> by_name;
    for (const KindName& entry : declared) {
        const auto number = by_number.find(entry.kind);
        if (number != by_number.end()) {
            if (number->second == entry.name)
                continue; // the two declare the same name for the same number: fused
            std::string message = "kind ";
            message += std::to_string(entry.kind);
            message += " is named '";
            message += number->second;
            message += "' by one machine and '";
            message += entry.name;
            message += "' by the other: the two cannot be fused, so rename one kind first";
            return refuse(error, std::move(message));
        }
        const auto name = by_name.find(entry.name);
        if (name != by_name.end()) {
            std::string message = "the name '";
            message += entry.name;
            message += "' stands for kind ";
            message += std::to_string(name->second);
            message += " in one machine and kind ";
            message += std::to_string(entry.kind);
            message += " in the other: one name cannot stand for two numbers, so rename one first";
            return refuse(error, std::move(message));
        }
        by_number.emplace(entry.kind, entry.name);
        by_name.emplace(entry.name, entry.kind);
    }
    // Ascending by kind, which is the order `dump` writes them in; the order is the canonical
    // form's, not either file's.
    std::vector<KindName> kinds;
    kinds.reserve(by_number.size());
    for (const auto& entry : by_number) {
        kinds.push_back(KindName{entry.second, entry.first});
    }

    // ---- the states, fused by name: the first machine's own order, then what only the second has.
    std::vector<std::string> states = a_machine->states;
    for (const std::string& state : b_machine->states) {
        if (std::find(states.begin(), states.end(), state) == states.end()) {
            states.push_back(state);
        }
    }

    // ---- the rows: the first machine's, then the second's. Both ends are names and the names are
    // already fused, so no row is rewritten here.
    std::vector<Transition> rows = a_machine->transitions;
    rows.insert(rows.end(), b_machine->transitions.begin(), b_machine->transitions.end());

    // ---- rule 9, over the union rather than either machine's own rows: at most one guarded and
    // one unguarded row for one (from, kind). A pair both machines give the SAME kind of row to is
    // the one shape a merge cannot represent, and it is refused here rather than written and
    // rejected by the parser below — the message names the pair, which is what tells the caller
    // where to look.
    std::map<std::pair<std::string, int>, unsigned> pair_flags; // bit 1 unguarded, bit 2 guarded
    for (const Transition& row : rows) {
        const unsigned flag = row.has_when ? 2U : 1U;
        const auto key = std::make_pair(row.from, row.kind);
        const auto found = pair_flags.find(key);
        if (found == pair_flags.end()) {
            pair_flags.emplace(key, flag);
            continue;
        }
        if ((found->second & flag) != 0U) {
            std::string message = "the two machines both have a ";
            message += (row.has_when ? "guarded" : "unguarded");
            message += " row for state '";
            message += row.from;
            message += "' and kind ";
            message += std::to_string(row.kind);
            message += ": the merge would write two, and SPEC.md rule 9 allows one guarded row and "
                       "one unguarded row per pair, so rename or retarget one of them";
            return refuse(error, std::move(message));
        }
        found->second |= flag;
    }

    // ---- the entry and exit clauses. The first machine's clause wins where both decorate one
    // state; a clause only the second declares is carried, in first-appearance order.
    std::vector<StateAction> actions;
    std::map<std::string, std::size_t> placed;
    for (const StateAction& entry : a_actions) {
        placed.emplace(entry.state, actions.size());
        actions.push_back(entry);
    }

    std::vector<std::string> warnings;
    for (const StateAction& entry : b_actions) {
        // The second machine's own initial becomes an ordinary state, and a merged machine is
        // CREATED, not entered (ENTRY_EXIT.md): the clause that would have run when that machine
        // was built describes a state it no longer is, so it is dropped and the drop is reported.
        // Both machines beginning in the same state is the one case where nothing is dropped —
        // there the state IS the merged initial, and its clause fuses like any other decoration.
        const bool drops_entry
            = entry.state == b_machine->initial && entry.state != a_machine->initial;
        if (drops_entry && !entry.enter.empty()) {
            std::string message = "the entry action '";
            message += entry.enter;
            message += "' of the second machine's initial state '";
            message += entry.state;
            message += "' is dropped: the merged machine is created, not entered (ENTRY_EXIT.md)";
            warnings.push_back(std::move(message));
        }

        const auto found = placed.find(entry.state);
        if (found == placed.end()) {
            StateAction made;
            made.state = entry.state;
            made.enter = drops_entry ? std::string{} : entry.enter;
            made.exit = entry.exit;
            if (made.enter.empty() && made.exit.empty())
                continue; // the only clause this state had was the one that was dropped
            placed.emplace(entry.state, actions.size());
            actions.push_back(std::move(made));
            continue;
        }
        StateAction& into = actions[found->second];
        if (into.enter.empty() && !drops_entry)
            into.enter = entry.enter;
        if (into.exit.empty())
            into.exit = entry.exit;
    }

    // ---- the union, written by the canonical writer.
    Machine merged;
    merged.name = a_machine->name;
    merged.initial = a_machine->initial;
    merged.states = states;
    merged.transitions = rows;

    std::string text = dump(merged, kinds, actions);

    // ---- the proof, and the two things it can still find. The text is parsed again rather than
    // trusted, and every state of what comes back has to be one a row can reach from the merged
    // initial: a merge that leaves the second machine's states out of reach is the composition
    // failing to meet, which is reported rather than written to a file.
    Error check{0, ""};
    std::vector<KindName> written_kinds;
    std::vector<StateAction> written_actions;
    const auto written = parse(text, check, written_kinds, written_actions);
    if (!written.has_value()) {
        std::string message = "the merge wrote a machine that does not parse (line ";
        message += std::to_string(check.line);
        message += "): ";
        message += check.message;
        return refuse(error, std::move(message));
    }

    const std::vector<std::string> orphans = unreachable(*written);
    if (!orphans.empty()) {
        std::string message = "the merge leaves ";
        message += std::to_string(orphans.size());
        message += " unreachable state(s) — no path from the merged initial '";
        message += merged.initial;
        message += "': ";
        message += listed(orphans);
        message
            += " — the two machines do not meet there; give the second machine a state name the "
               "first already reaches, or a row into it";
        return refuse(error, std::move(message));
    }

    MergeResult result;
    result.text = std::move(text);
    result.fingerprint = fnv1a(result.text);
    result.warnings = std::move(warnings);
    return result;
}

} // namespace fsmtable
