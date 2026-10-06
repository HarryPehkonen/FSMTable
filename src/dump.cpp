// FSMTable — the writer: Machine in, canonical text out.
//
// Split out of the single 754-line src/fsmtable.cpp on 2026-10-06, which held the format's
// vocabulary, the parser, the writer and the graph analysis. Only the writer moved here: dump_with
// and the three dump overloads, verbatim. dump_with was file-private in the original and still is
// (an anonymous namespace below); the few token helpers this file shares with the parser come from
// fsmtable::detail. No line of logic changed: the bodies below are the same text, moved. The one
// structural change is the namespace: helpers that had internal linkage in anonymous namespaces are
// now named in fsmtable::detail (declared in fsmtable_detail.hpp) so a file boundary can reach
// them. include/ is untouched and no public header changed.

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

// Two of the format's token helpers are used here -- op_text and find_kind_number -- and they live
// in fsmtable::detail so that names as generic as those do not squat in the public namespace.
using namespace detail;
namespace {

std::string dump_with(const Machine& machine, const std::vector<KindName>& kind_names,
                      const std::vector<StateAction>& state_actions) {
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
    if (!kind_names.empty()) {
        // Ascending by kind — canonical, so two files that declare the same names in different
        // orders dump identically, and so the declarations are in the order the rows sort in.
        // A name whose kind no row uses is still written: it was declared, so it is part of the
        // file, and dropping it would make the round trip lossy.
        std::vector<const KindName*> declared;
        declared.reserve(kind_names.size());
        for (const KindName& entry : kind_names) {
            declared.push_back(&entry);
        }
        std::sort(declared.begin(), declared.end(), [](const KindName* a, const KindName* b) {
            if (a->kind != b->kind)
                return a->kind < b->kind;
            return a->name < b->name;
        });
        for (const KindName* entry : declared) {
            out += "kind " + entry->name + " = " + std::to_string(entry->kind) + "\n";
        }
    }
    if (!state_actions.empty()) {
        // Sorted by state name: canonical, and independent of the order the machine's own
        // `states` list happens to be in — a canonical round trip rebuilds that list in its own
        // order (QUESTIONS.md Q7), so using it here would make a decorated dump unstable.
        std::vector<const StateAction*> decorated;
        decorated.reserve(state_actions.size());
        for (const StateAction& entry : state_actions) {
            decorated.push_back(&entry);
        }
        std::sort(decorated.begin(), decorated.end(),
                  [](const StateAction* a, const StateAction* b) { return a->state < b->state; });
        for (const StateAction* entry : decorated) {
            out += "state " + entry->state;
            if (!entry->enter.empty()) {
                out += " entry " + entry->enter;
            }
            if (!entry->exit.empty()) {
                out += " exit " + entry->exit;
            }
            out += "\n";
        }
    }
    for (const Transition* row : rows) {
        const KindName* named = find_kind_number(kind_names, row->kind);
        out += "transition " + row->from + " --";
        out += named == nullptr ? std::to_string(row->kind) : named->name;
        out += "--> " + row->to;
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

} // namespace

std::string dump(const Machine& machine) {
    // The frozen writer: numbers only, byte for byte what it always wrote.
    return dump_with(machine, {}, {});
}

std::string dump(const Machine& machine, const std::vector<KindName>& kind_names) {
    return dump_with(machine, kind_names, {});
}

std::string dump(const Machine& machine, const std::vector<KindName>& kind_names,
                 const std::vector<StateAction>& state_actions) {
    return dump_with(machine, kind_names, state_actions);
}

} // namespace fsmtable
