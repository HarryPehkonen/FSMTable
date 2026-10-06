// fsmTable stage C — the differential cross-check of SPEC.md section 8.
//
// Section 8 asks for an identical trace — the state after every event, plus the action log —
// between a machine this library parsed and the same machine driven by FSMgine's compiled back
// end, over a generated event sequence. Two readings were needed to build that, and both are
// recorded in QUESTIONS.md rather than decided silently here:
//
//   Q5 — the second trace. Stages A and B define no execution semantics and the frozen section
//   4 API has no runner, so this file carries a plain reference interpreter: for the current
//   state and the event's kind, the guarded row fires if its comparison passes, otherwise the
//   unguarded row for the same state and kind, otherwise nothing moves. FSMgine is the
//   independent implementation it is checked against — that is what makes this a differential
//   oracle rather than a self-check, and why a disagreement means finding out which side is
//   wrong instead of assuming.
//
//   Q6 — precedence. One (from, kind) may hold one guarded row and one unguarded row (rule 9).
//   Read as the guarded row REFINING the unguarded one, in that order, wherever the file put
//   them: that is the order `dump` emits, and first-match-wins is exactly what
//   fsmgine::compiled::Machine::process() implements — so feeding FSMgine the canonical order
//   is what makes the two implementations comparable at all.
//
// FSMgine is read, never written: only its headers are used, and nothing is built inside its
// tree.
#include "fsmtable.hpp"

#include "FSMgine/compiled/Machine.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fsmtable::Machine;
using fsmtable::Op;
using fsmtable::Transition;

[[noreturn]] void harness_limit(const std::string& what) {
    ADD_FAILURE() << "stage C harness limit: " << what;
    std::abort();
}

// ---------------------------------------------------------------- the compiled oracle

// fsmgine::compiled::Machine reads the event kind from a member named `kind` (its type is the
// machine's EventKind) and refines one int member. The format has exactly one value slot, and
// the compiled back end allows exactly one refined field, so the two line up without a mapping.
struct Event {
    int kind = 0;
    int value = 0;
};

// States are an enum on that back end, while a parsed machine names them as strings. The
// harness therefore maps them by first-appearance index — and refuses to run, loudly, on a
// machine that does not fit rather than truncating one.
enum class State : std::uint8_t {
    S0,
    S1,
    S2,
    S3,
    S4,
    S5,
    S6,
    S7,
    S8,
    S9,
    S10,
    S11,
    S12,
    S13,
    S14,
    S15,
};
constexpr std::size_t kStateSlots = 16;

// Actions on that back end are plain function pointers that cannot capture, so the log and the
// name tables are reached through accessors over function-local statics. A namespace-scope
// mutable would be a lint finding, and two engines sharing mutable globals would make this file
// report on itself instead of on the two implementations.
struct Tables {
    std::vector<std::string> state_names;  // per machine, in state order
    std::vector<std::string> action_names; // per machine, in first-seen row order
    std::vector<std::size_t> log;          // action slots, in the order they ran
};

Tables& tables() {
    static Tables instance;
    return instance;
}

void reset_tables(const Machine& machine) {
    Tables& state = tables();
    state.state_names = machine.states;
    state.action_names.clear();
    state.log.clear();
}

std::size_t slot_for_action(const std::string& name) {
    Tables& state = tables();
    for (std::size_t i = 0; i < state.action_names.size(); ++i) {
        if (state.action_names[i] == name)
            return i;
    }
    state.action_names.push_back(name);
    return state.action_names.size() - 1;
}

std::string_view name_of(State state) {
    const Tables& table = tables();
    const auto index = static_cast<std::size_t>(state);
    return index < table.state_names.size() ? std::string_view(table.state_names[index])
                                            : std::string_view{};
}

template <std::size_t Slot> void record_action(const Event& /*event*/) {
    tables().log.push_back(Slot);
}

using Action = fsmgine::compiled::Action<Event>;
constexpr std::size_t kActionSlots = 8;
constexpr std::array<Action, kActionSlots> kActions = {
    &record_action<0>, &record_action<1>, &record_action<2>, &record_action<3>,
    &record_action<4>, &record_action<5>, &record_action<6>, &record_action<7>,
};

using CompiledMachine = fsmgine::compiled::Machine<State, Event>;

// The canonical order of section 4, mirrored from dump()'s comparator: from, kind,
// guarded-before-unguarded, to, action, then the two `when` fields as the last tiebreak. First
// match wins on the other side, so this order is what carries Q6's precedence across.
std::vector<const Transition*> canonical_order(const Machine& machine) {
    std::vector<const Transition*> rows;
    rows.reserve(machine.transitions.size());
    for (const Transition& row : machine.transitions)
        rows.push_back(&row);
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
    return rows;
}

std::size_t state_slot(const Machine& machine, const std::string& name) {
    const auto found = std::find(machine.states.begin(), machine.states.end(), name);
    if (found == machine.states.end())
        harness_limit("a transition names a state that is not in `states`: " + name);
    return static_cast<std::size_t>(found - machine.states.begin());
}

fsmgine::compiled::Op compiled_op(Op op) {
    switch (op) {
    case Op::Eq:
        return fsmgine::compiled::Op::Eq;
    case Op::Lt:
        return fsmgine::compiled::Op::Lt;
    case Op::Le:
        return fsmgine::compiled::Op::Le;
    case Op::Gt:
        return fsmgine::compiled::Op::Gt;
    case Op::Ge:
        return fsmgine::compiled::Op::Ge;
    }
    return fsmgine::compiled::Op::Eq;
}

// `sabotage_first_row` exists for one test: to prove this comparison can fail. It rewrites the
// first row's target in the emitted table and nothing else.
CompiledMachine build_compiled(const Machine& machine, bool sabotage_first_row) {
    if (machine.states.size() > kStateSlots)
        harness_limit("a machine with more than 16 states: "
                      + std::to_string(machine.states.size()));

    std::vector<fsmgine::compiled::Transition<State, Event>> rows;
    const std::vector<const Transition*> ordered = canonical_order(machine);
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const Transition& row = *ordered[i];
        fsmgine::compiled::Transition<State, Event> compiled{};
        compiled.from = static_cast<State>(state_slot(machine, row.from));
        compiled.event = row.kind;
        compiled.refined = row.has_when;
        compiled.op = compiled_op(row.when_op);
        compiled.value = row.when_value;
        compiled.to = static_cast<State>(state_slot(machine, row.to));
        if (!row.action.empty()) {
            const std::size_t slot = slot_for_action(row.action);
            if (slot >= kActionSlots)
                harness_limit("a machine with more than 8 distinct action names");
            compiled.action = kActions.at(slot);
        }
        if (sabotage_first_row && i == 0)
            compiled.to = compiled.from;
        rows.push_back(compiled);
    }
    return CompiledMachine{&Event::kind, &Event::value, std::move(rows)};
}

// ---------------------------------------------------------------- the reference reading

// Q5's interpreter, and nothing more: one pass over the rows for the current state and kind.
bool holds(const Transition& row, const Event& event) {
    switch (row.when_op) {
    case Op::Eq:
        return event.value == row.when_value;
    case Op::Lt:
        return event.value < row.when_value;
    case Op::Le:
        return event.value <= row.when_value;
    case Op::Gt:
        return event.value > row.when_value;
    case Op::Ge:
        return event.value >= row.when_value;
    }
    return false;
}

struct Trajectory {
    std::vector<std::string> states; // the initial state, then the state after each event
    std::vector<std::size_t> actions;
    std::size_t no_match = 0;      // events that moved nothing
    std::size_t guarded_fires = 0; // reference side only: rows whose `when` clause passed
};

Trajectory run_reference(const Machine& machine, const std::vector<Event>& events) {
    Trajectory trace;
    std::string current = machine.initial;
    trace.states.push_back(current);
    for (const Event& event : events) {
        const Transition* plain = nullptr;
        const Transition* refined = nullptr;
        for (const Transition& row : machine.transitions) {
            if (row.from != current || row.kind != event.kind)
                continue;
            if (!row.has_when) {
                plain = &row; // rule 9 allows at most one of these per (from, kind)
                continue;
            }
            if (holds(row, event)) {
                refined = &row;
                break;
            }
        }
        const Transition* fired = refined != nullptr ? refined : plain;
        if (fired == nullptr) {
            ++trace.no_match;
        } else {
            if (fired->has_when)
                ++trace.guarded_fires;
            if (!fired->action.empty())
                trace.actions.push_back(slot_for_action(fired->action));
            current = fired->to;
        }
        trace.states.push_back(current);
    }
    return trace;
}

Trajectory run_compiled(CompiledMachine& machine, const Machine& parsed,
                        const std::vector<Event>& events) {
    Trajectory trace;
    machine.setInitialState(static_cast<State>(state_slot(parsed, parsed.initial)));
    machine.withNames(&name_of);
    trace.states.emplace_back(machine.currentStateName());
    tables().log.clear();
    for (const Event& event : events) {
        if (!machine.process(event))
            ++trace.no_match;
        trace.states.emplace_back(machine.currentStateName());
    }
    trace.actions = tables().log;
    return trace;
}

// ------------------------------------------------------------------- the comparison

struct Comparison {
    bool agree = true;
    std::string detail;
    Trajectory reference;
};

std::string action_names_at(const std::vector<std::size_t>& slots) {
    std::string out;
    const Tables& state = tables();
    for (const std::size_t slot : slots) {
        if (!out.empty())
            out += ",";
        out += slot < state.action_names.size() ? state.action_names[slot] : "?";
    }
    return out;
}

Comparison compare(const Machine& machine, const std::vector<Event>& events,
                   bool sabotage_first_row = false) {
    Comparison result;
    reset_tables(machine);
    CompiledMachine compiled = build_compiled(machine, sabotage_first_row);
    result.reference = run_reference(machine, events);
    const Trajectory oracle = run_compiled(compiled, machine, events);

    for (std::size_t i = 0; i < result.reference.states.size(); ++i) {
        if (result.reference.states[i] != oracle.states[i]) {
            result.agree = false;
            result.detail = "state after event " + std::to_string(i) + " (kind "
                            + std::to_string(i == 0 ? -1 : events[i - 1].kind) + ", value "
                            + std::to_string(i == 0 ? -1 : events[i - 1].value) + "): reference '"
                            + result.reference.states[i] + "', FSMgine '" + oracle.states[i] + "'";
            return result;
        }
    }
    if (result.reference.actions != oracle.actions) {
        result.agree = false;
        result.detail = "action log: reference [" + action_names_at(result.reference.actions)
                        + "], FSMgine [" + action_names_at(oracle.actions) + "]";
        return result;
    }
    if (result.reference.no_match != oracle.no_match) {
        result.agree = false;
        result.detail = "events that moved nothing: reference "
                        + std::to_string(result.reference.no_match) + ", FSMgine "
                        + std::to_string(oracle.no_match);
        return result;
    }
    return result;
}

// ------------------------------------------------------------------- the generators

std::vector<Op> all_ops() { return {Op::Eq, Op::Lt, Op::Le, Op::Gt, Op::Ge}; }

std::string op_text(Op op) {
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

struct RowText {
    std::string from;
    int kind = 0;
    std::string to;
    std::string when;   // "ge 30", or empty for an unguarded row
    std::string action; // empty when the row carries no action
};

std::string row_line(const RowText& row) {
    std::string line
        = "transition " + row.from + " --" + std::to_string(row.kind) + "--> " + row.to;
    if (!row.when.empty())
        line += " when " + row.when;
    if (!row.action.empty())
        line += " action " + row.action;
    return line + "\n";
}

// Machines as text, so they travel through parse() like every other input: a generator that
// produced a machine directly would skip the layer this stage is checking.
std::string generate_machine(std::mt19937& rng) {
    const std::size_t state_count = 1 + static_cast<std::size_t>(rng() % 6);
    const std::size_t kind_count = 1 + static_cast<std::size_t>(rng() % 3);
    std::vector<int> kinds;
    while (kinds.size() < kind_count) {
        const int kind = static_cast<int>(rng() % 256);
        if (std::find(kinds.begin(), kinds.end(), kind) == kinds.end())
            kinds.push_back(kind);
    }

    std::ostringstream out;
    out << "version 1\nmachine Generated\ninitial S0\n";
    for (std::size_t from = 0; from < state_count; ++from) {
        for (const int kind : kinds) {
            // 0-2 no row for this pair, 3-5 unguarded, 6-7 guarded, 8-9 both — and when both,
            // whichever order the throw of the dice says, because Q6 says the precedence does
            // not depend on it.
            const unsigned shape = static_cast<unsigned>(rng() % 10);
            const std::string from_name = "S" + std::to_string(from);
            const std::string to_name = "S" + std::to_string(rng() % state_count);
            RowText plain{from_name, kind, to_name, "", ""};
            if (rng() % 5 != 0)
                plain.action = "A" + std::to_string(rng() % 4);

            RowText guarded = plain;
            const std::vector<Op> ops = all_ops();
            guarded.when = op_text(ops[rng() % ops.size()]) + " "
                           + std::to_string(rng() % 8 == 0 ? (rng() % 2 == 0 ? 500 : -500)
                                                           : static_cast<int>(rng() % 81) - 40);

            if (shape <= 2)
                continue;
            if (shape <= 5) {
                out << row_line(plain);
                continue;
            }
            if (shape <= 7) {
                out << row_line(guarded);
                continue;
            }
            if (rng() % 2 == 0) {
                out << row_line(guarded) << row_line(plain);
            } else {
                out << row_line(plain) << row_line(guarded);
            }
        }
    }
    return out.str();
}

// Events aimed where the interesting behaviour is: the kinds the machine knows plus one it does
// not (the no-match path), and values sitting on the guard boundaries rather than in the middle
// of nowhere.
std::vector<Event> generate_events(const Machine& machine, std::mt19937& rng, std::size_t count) {
    std::vector<int> kinds;
    std::vector<int> guards;
    for (const Transition& row : machine.transitions) {
        if (std::find(kinds.begin(), kinds.end(), row.kind) == kinds.end())
            kinds.push_back(row.kind);
        if (row.has_when)
            guards.push_back(row.when_value);
    }
    int unseen = 0;
    while (std::find(kinds.begin(), kinds.end(), unseen) != kinds.end())
        ++unseen;

    std::vector<Event> events;
    events.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        Event event;
        event.kind = kinds.empty() || rng() % 8 == 0 ? unseen : kinds[rng() % kinds.size()];
        if (!guards.empty() && rng() % 2 == 0) {
            const int boundary = guards[rng() % guards.size()];
            event.value = boundary + static_cast<int>(rng() % 3) - 1; // just below, on, just above
        } else {
            event.value = static_cast<int>(rng() % 201) - 100;
        }
        events.push_back(event);
    }
    return events;
}

Machine parse_or_fail(const std::string& text, const char* what) {
    fsmtable::Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);
    if (!machine.has_value()) {
        ADD_FAILURE() << what << ": " << error.message << "\n" << text;
        std::abort();
    }
    return *machine;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.good()) {
        ADD_FAILURE() << "cannot read " << path;
        std::abort();
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

// ---------------------------------------------------------------------------- the tests

TEST(Differential, FsmgineAgreesWithTheReferenceOnTheCorpus) {
    const std::filesystem::path dir(FSMTABLE_CORPUS_DIR);
    ASSERT_TRUE(std::filesystem::is_directory(dir)) << "no corpus directory at " << dir;
    std::size_t compared = 0;
    std::size_t index = 0;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".fsm")
            continue;
        ++index;
        // The invalid_*.fsm files are section 7's business; this stage compares machines, so a
        // file that does not parse is simply not a machine to compare.
        fsmtable::Error error{0, ""};
        const auto machine = fsmtable::parse(read_file(entry.path()), error);
        if (!machine.has_value())
            continue;
        std::mt19937 events_rng(static_cast<unsigned>(1000 + index));
        const std::vector<Event> events = generate_events(*machine, events_rng, 200);
        const Comparison comparison = compare(*machine, events);
        EXPECT_TRUE(comparison.agree)
            << entry.path().filename().string() << ": " << comparison.detail;
        ++compared;
    }
    EXPECT_GT(compared, 0U) << "no corpus machine was compared";
}

TEST(Differential, FsmgineAgreesWithTheReferenceOnGeneratedMachines) {
    std::mt19937 rng(20261006u);
    std::size_t compared = 0;
    for (std::size_t machine_index = 0; machine_index < 40; ++machine_index) {
        const std::string text = generate_machine(rng);
        const Machine machine = parse_or_fail(text, "a generated machine did not parse");
        for (unsigned seed = 0; seed < 5; ++seed) {
            std::mt19937 events_rng(seed * 7919u + 11u);
            const std::vector<Event> events = generate_events(machine, events_rng, 200);
            const Comparison comparison = compare(machine, events);
            EXPECT_TRUE(comparison.agree) << "machine " << machine_index << ", seed " << seed
                                          << ": " << comparison.detail << "\n"
                                          << text;
            ++compared;
        }
    }
    EXPECT_EQ(compared, 200U);
}

TEST(Differential, FsmgineAgreesAfterACanonicalDumpRoundTrip) {
    std::mt19937 rng(4242u);
    std::size_t checked = 0;
    for (std::size_t i = 0; i < 20; ++i) {
        const std::string text = generate_machine(rng);
        const Machine first = parse_or_fail(text, "a generated machine did not parse");
        const std::string canonical = fsmtable::dump(first);
        const Machine second = parse_or_fail(canonical, "the canonical dump did not parse");
        // The canonical form declares no states: `states` is rebuilt from the order the names
        // appear in it (initial first, then the sorted rows). So a round trip preserves the
        // machine's meaning and its set of states, not the file's original first-appearance
        // order — section 5's oracle asks for dump/parse/dump STABILITY, not for the vector to
        // survive, and QUESTIONS.md Q7 records the distinction. What must survive is the
        // canonical text and the behaviour, and both are checked below.
        EXPECT_EQ(fsmtable::dump(second), canonical) << "the canonical form is not a fixed point";
        std::vector<std::string> first_names = first.states;
        std::vector<std::string> second_names = second.states;
        std::sort(first_names.begin(), first_names.end());
        std::sort(second_names.begin(), second_names.end());
        EXPECT_EQ(second_names, first_names) << "the round trip changed which states exist";
        EXPECT_EQ(second.initial, first.initial) << "the round trip changed the initial state";

        std::mt19937 events_rng(static_cast<unsigned>(i + 9));
        const std::vector<Event> events = generate_events(first, events_rng, 150);
        const Comparison from_text = compare(first, events);
        const Comparison from_dump = compare(second, events);
        EXPECT_TRUE(from_text.agree) << "as parsed: " << from_text.detail;
        EXPECT_TRUE(from_dump.agree) << "from the dump: " << from_dump.detail;
        EXPECT_EQ(from_text.reference.states, from_dump.reference.states)
            << "the canonical dump changed the trace";
        EXPECT_EQ(from_text.reference.actions, from_dump.reference.actions)
            << "the canonical dump changed the action log";
        ++checked;
    }
    EXPECT_EQ(checked, 20U);
}

TEST(Differential, TheHarnessDetectsADeliberateDisagreement) {
    // Without this, "the traces agree" could be a comparison that cannot fail. The machine's
    // first row must fire on the first event, and the sabotage points it back where it came
    // from: the reference says A -> B, the sabotaged table says A -> A.
    const Machine machine = parse_or_fail("version 1\nmachine Sabotage\ninitial A\n"
                                          "transition A --1--> B action go\n"
                                          "transition B --2--> A\n",
                                          "the sabotage machine did not parse");
    const std::vector<Event> events = {Event{1, 0}, Event{2, 0}, Event{1, 0}};

    const Comparison honest = compare(machine, events);
    EXPECT_TRUE(honest.agree) << honest.detail;
    const std::vector<std::string> expected = {"A", "B", "A", "B"};
    EXPECT_EQ(honest.reference.states, expected);
    EXPECT_EQ(honest.reference.actions.size(), 2U) << "both rows carrying `go` should have fired";

    const Comparison sabotaged = compare(machine, events, /*sabotage_first_row=*/true);
    EXPECT_FALSE(sabotaged.agree) << "the comparison accepted a machine whose first row leads "
                                     "somewhere else: it is not comparing what it claims to";
    EXPECT_FALSE(sabotaged.detail.empty());
}

TEST(Differential, TheHarnessExercisesGuardsActionsAndEventsThatMatchNothing) {
    // The teeth of the two tests above, in the same spirit as the kit's probes: a sweep that
    // never fires a guard, never runs an action or never misses would compare two empty traces
    // and report success.
    std::mt19937 rng(31337u);
    std::size_t guarded_fires = 0;
    std::size_t actions = 0;
    std::size_t no_match = 0;
    std::size_t events_total = 0;
    for (std::size_t i = 0; i < 40; ++i) {
        const std::string text = generate_machine(rng);
        const Machine machine = parse_or_fail(text, "a generated machine did not parse");
        std::mt19937 events_rng(static_cast<unsigned>(i + 3));
        const std::vector<Event> events = generate_events(machine, events_rng, 200);
        const Comparison comparison = compare(machine, events);
        ASSERT_TRUE(comparison.agree) << comparison.detail << "\n" << text;
        guarded_fires += comparison.reference.guarded_fires;
        actions += comparison.reference.actions.size();
        no_match += comparison.reference.no_match;
        events_total += events.size();
    }
    EXPECT_GT(events_total, 0U);
    EXPECT_GT(guarded_fires, 0U) << "no guarded row ever fired: the guard comparison is untested";
    EXPECT_GT(actions, 0U) << "no action ever ran: the action log is untested";
    EXPECT_GT(no_match, 0U) << "every event found a row: the no-match path is untested";
}
