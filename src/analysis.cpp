// FSMTable — the graph analysis: reachability, sinks, partial pairs.
//
// Split out of the single 754-line src/fsmtable.cpp on 2026-10-06, which held the format's
// vocabulary, the parser, the writer and the graph analysis. Only the analysis moved here:
// state_index and the three public analyses, verbatim. state_index was file-private in the original
// and still is (the anonymous namespace below), and this file shares nothing with the other three
// -- the public header is enough, so it does not include fsmtable_detail.hpp.

#include "fsmtable.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fsmtable {
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

std::vector<PartialPair> partial_pairs(const Machine& machine) {
    // Rule 9 (frozen) allows one guard per (from, kind) and fixes the canonical order so the
    // guarded row comes first and any unguarded row stands behind it as the fallback. A pair with
    // no unguarded row is therefore the only shape where a false guard leaves nothing to match:
    // `process()` reports the event unhandled and the row's action never runs. This is a reading
    // of rule 9, not a new rule — the shape is legal, which is why the parser accepts it and a
    // caller reports it.
    //
    // The map is the lookup, the vectors are the order: a pair is appended the first time a row
    // names it, so the result comes back in first-appearance order like the two analyses above,
    // and `flags` records what that pair's rows are. Written this way rather than as a nested
    // scan because a machine's row count is the caller's to choose, and the analyses run on
    // whatever the parser was handed.
    std::map<std::pair<std::string, int>, std::size_t> seen;
    std::vector<PartialPair> pairs;
    std::vector<char> flags; // 1 = an unguarded row, 2 = a guarded one, both once a pair has both

    for (const Transition& row : machine.transitions) {
        const auto key = std::make_pair(row.from, row.kind);
        const auto found = seen.find(key);
        std::size_t index = 0;
        if (found == seen.end()) {
            index = pairs.size();
            seen.emplace(key, index);
            pairs.push_back(PartialPair{row.from, row.kind});
            flags.push_back(0);
        } else {
            index = found->second;
        }
        flags[index] = static_cast<char>(flags[index] | (row.has_when ? 2 : 1));
    }

    std::vector<PartialPair> result;
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (flags[i] == 2) // guarded, and never any unguarded row behind it
            result.push_back(pairs[i]);
    }
    return result;
}

} // namespace fsmtable
