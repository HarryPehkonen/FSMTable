// fsmTable code-generator tests.
//
// The headers under test are WRITTEN AT BUILD TIME by fsmtable-gen from corpus/ and
// tests/fixtures/, and compiled here: a generator that emits something that does not compile, or
// that names actions the file does not name, fails the build rather than a review. Each action
// the generated headers declare is defined below — the definition is the link-time half of the
// contract, so renaming an action in a .fsm breaks this file until it is followed.
//
// The comparison does NOT use stage C's differential harness, on purpose: the harness and the
// generator share the canonical-order rule, so reusing it here would make the two agree by
// construction. This file carries its own plain reading of the semantics (the same reading
// QUESTIONS.md Q5 records) and holds the compiled artifacts to it.
#include "fsm_guarded.hpp"
#include "fsm_minimal.hpp"
#include "fsm_named_kinds.hpp"
#include "fsm_refined_pair.hpp"
#include "fsm_traffic_light.hpp"

#include "fsmtable.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void cannot_read(const std::string& what) {
    ADD_FAILURE() << what;
    std::abort();
}

// The action log, reached through an accessor because the generated actions are plain functions
// (no captures — that is the shape the compiled back end takes) and need somewhere to write.
std::vector<std::string>& log() {
    static std::vector<std::string> instance;
    return instance;
}

// An event in the shape the reference reading works on. Each generated artifact has its own
// Event type, converted at the call site below.
struct Event {
    int kind = 0;
    int value = 0;
};

struct Trace {
    std::vector<std::string> states; // the initial state, then the state after each event
    std::vector<std::string> actions;
    std::size_t no_match = 0;
};

bool holds(const fsmtable::Transition& row, const Event& event) {
    switch (row.when_op) {
    case fsmtable::Op::Eq:
        return event.value == row.when_value;
    case fsmtable::Op::Lt:
        return event.value < row.when_value;
    case fsmtable::Op::Le:
        return event.value <= row.when_value;
    case fsmtable::Op::Gt:
        return event.value > row.when_value;
    case fsmtable::Op::Ge:
        return event.value >= row.when_value;
    }
    return false;
}

// The plain reading: for the current state and this kind, the guarded row fires if its
// comparison passes, otherwise the unguarded row for the same pair, otherwise nothing moves.
Trace reference_trace(const fsmtable::Machine& machine, const std::vector<Event>& events) {
    Trace trace;
    std::string current = machine.initial;
    trace.states.push_back(current);
    for (const Event& event : events) {
        const fsmtable::Transition* plain = nullptr;
        const fsmtable::Transition* refined = nullptr;
        for (const fsmtable::Transition& row : machine.transitions) {
            if (row.from != current || row.kind != event.kind)
                continue;
            if (!row.has_when) {
                plain = &row;
                continue;
            }
            if (holds(row, event)) {
                refined = &row;
                break;
            }
        }
        const fsmtable::Transition* fired = refined != nullptr ? refined : plain;
        if (fired == nullptr) {
            ++trace.no_match;
        } else {
            if (!fired->action.empty())
                trace.actions.push_back(fired->action);
            current = fired->to;
        }
        trace.states.push_back(current);
    }
    return trace;
}

int op_index(fsmtable::Op op) {
    switch (op) {
    case fsmtable::Op::Eq:
        return 0;
    case fsmtable::Op::Lt:
        return 1;
    case fsmtable::Op::Le:
        return 2;
    case fsmtable::Op::Gt:
        return 3;
    case fsmtable::Op::Ge:
        return 4;
    }
    return -1;
}

int op_index(fsmgine::compiled::Op op) {
    switch (op) {
    case fsmgine::compiled::Op::Eq:
        return 0;
    case fsmgine::compiled::Op::Lt:
        return 1;
    case fsmgine::compiled::Op::Le:
        return 2;
    case fsmgine::compiled::Op::Gt:
        return 3;
    case fsmgine::compiled::Op::Ge:
        return 4;
    }
    return -1;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in.good())
        cannot_read("cannot read " + path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// The input paths come from CMake, never from the working directory: ctest starts the binary
// wherever it likes, and a relative path would make the tests depend on that.
std::string corpus(const std::string& name) {
    return std::string(FSMTABLE_CORPUS_DIR) + "/" + name;
}

std::string fixture(const std::string& name) {
    return std::string(FSMTABLE_FIXTURES_DIR) + "/" + name;
}

fsmtable::Machine parse_text(const std::string& path) {
    fsmtable::Error error{0, ""};
    const auto machine = fsmtable::parse(read_file(path), error);
    if (!machine.has_value())
        cannot_read(path + ": " + error.message);
    return *machine;
}

// The machine as the canonical form describes it, whose transitions are comparable row by row
// with an emitted table. (A canonical round trip rebuilds `states` in its own order —
// QUESTIONS.md Q7 — which is why only the rows are compared.)
fsmtable::Machine parse_canonical(const std::string& path) {
    const fsmtable::Machine parsed = parse_text(path);
    fsmtable::Error error{0, ""};
    const auto again = fsmtable::parse(fsmtable::dump(parsed), error);
    if (!again.has_value())
        cannot_read(path + ": its canonical form did not parse: " + error.message);
    return *again;
}

// Re-expresses generated events as reference events. Both are (kind, value) pairs; the generated
// one's kind is a scoped enum, which is the point of the conversion.
template <class GeneratedEvent>
std::vector<Event> as_reference_events(const std::vector<GeneratedEvent>& events) {
    std::vector<Event> out;
    out.reserve(events.size());
    for (const GeneratedEvent& event : events)
        out.push_back(Event{static_cast<int>(event.kind), event.value});
    return out;
}

// Events aimed at the interesting places: the kinds the machine knows plus one it does not, and
// values sitting on guard boundaries.
template <class GeneratedEvent, class Kind>
std::vector<GeneratedEvent> make_events(const fsmtable::Machine& machine, std::mt19937& rng,
                                        std::size_t count) {
    std::vector<int> kinds;
    std::vector<int> guards;
    for (const fsmtable::Transition& row : machine.transitions) {
        if (std::find(kinds.begin(), kinds.end(), row.kind) == kinds.end())
            kinds.push_back(row.kind);
        if (row.has_when)
            guards.push_back(row.when_value);
    }
    int unseen = 0;
    while (std::find(kinds.begin(), kinds.end(), unseen) != kinds.end())
        ++unseen;

    std::vector<GeneratedEvent> events;
    events.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        GeneratedEvent event{};
        event.kind = static_cast<Kind>(
            kinds.empty() || rng() % 8 == 0 ? unseen : kinds[rng() % kinds.size()]);
        if (!guards.empty() && rng() % 2 == 0) {
            const int boundary = guards[rng() % guards.size()];
            event.value = boundary + static_cast<int>(rng() % 3) - 1;
        } else {
            event.value = static_cast<int>(rng() % 201) - 100;
        }
        events.push_back(event);
    }
    return events;
}

// Drives a generated machine over generated events, through its own types, keeping only what the
// comparison needs: the state name after every event, and the "moved nothing" count.
template <class State, class GeneratedEvent>
Trace drive(fsmgine::compiled::Machine<State, GeneratedEvent>& machine,
            const std::vector<GeneratedEvent>& events) {
    Trace trace;
    trace.states.emplace_back(machine.currentStateName());
    for (const GeneratedEvent& event : events) {
        if (!machine.process(event))
            ++trace.no_match;
        trace.states.emplace_back(machine.currentStateName());
    }
    return trace;
}

void expect_agrees(const std::string& what, const Trace& reference,
                   const std::vector<std::string>& generated_actions, const Trace& generated) {
    ASSERT_EQ(reference.states.size(), generated.states.size()) << what;
    EXPECT_EQ(reference.states, generated.states) << what << ": the state after some event differs";
    EXPECT_EQ(reference.actions, generated_actions) << what << ": the action log differs";
    EXPECT_EQ(reference.no_match, generated.no_match)
        << what << ": the count of events that moved nothing differs";
    EXPECT_GT(reference.states.size(), 1U) << what << ": no events were compared";
}

} // namespace

// The actions the generated headers declare. Defining them here is the other half of the
// contract: a name a .fsm uses and this file does not define is a link error.
namespace fsmtable_generated {

void on_red_green(const TrafficLightEvent& /*event*/) { log().push_back("on_red_green"); }

void high(const RefinedPairEvent& /*event*/) { log().push_back("high"); }

void on_tick(const NamedEvent& /*event*/) { log().push_back("on_tick"); }

} // namespace fsmtable_generated

// -------------------------------------------------------------------------------- the tests

TEST(Generated, TheEmittedTableIsTheTextInCanonicalOrder) {
    const fsmtable::Machine canonical = parse_canonical(corpus("traffic_light.fsm"));
    const auto& rows = fsmtable_generated::TrafficLightRows;
    ASSERT_EQ(rows.size(), canonical.transitions.size());
    std::size_t with_action = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const fsmtable::Transition& expected = canonical.transitions[i];
        EXPECT_EQ(std::string(fsmtable_generated::TrafficLightNameOf(rows.at(i).from)),
                  expected.from)
            << "row " << i << ": from";
        EXPECT_EQ(static_cast<int>(rows.at(i).event), expected.kind) << "row " << i << ": kind";
        EXPECT_EQ(rows.at(i).refined, expected.has_when) << "row " << i << ": refined";
        EXPECT_EQ(op_index(rows.at(i).op), op_index(expected.when_op)) << "row " << i << ": op";
        EXPECT_EQ(rows.at(i).value, expected.when_value) << "row " << i << ": value";
        EXPECT_EQ(std::string(fsmtable_generated::TrafficLightNameOf(rows.at(i).to)), expected.to)
            << "row " << i << ": to";
        EXPECT_EQ(rows.at(i).action != nullptr, !expected.action.empty())
            << "row " << i << ": action present";
        if (rows.at(i).action != nullptr) {
            ++with_action;
            EXPECT_EQ(rows.at(i).action, &fsmtable_generated::on_red_green)
                << "row " << i << " points at an action the file did not name";
        }
    }
    EXPECT_EQ(with_action, 1U) << "traffic_light.fsm names exactly one action";
}

TEST(Generated, TheStateNamesAndTheInitialStateSurvive) {
    const fsmtable::Machine parsed = parse_text(corpus("traffic_light.fsm"));
    ASSERT_EQ(fsmtable_generated::TrafficLightStateNames.size(), parsed.states.size());
    for (std::size_t i = 0; i < parsed.states.size(); ++i)
        EXPECT_EQ(fsmtable_generated::TrafficLightStateNames.at(i), parsed.states[i])
            << "state name " << i;

    auto machine = fsmtable_generated::makeTrafficLight();
    EXPECT_EQ(machine.currentStateName(), parsed.initial) << "the initial state is wrong";
}

TEST(Generated, TheGeneratedMachinesBehaveLikeTheTextTheyCameFrom) {
    // traffic_light: an action and a guard. guarded: every row guarded, no fallback. refined_pair:
    // a fallback with a refinement on top of it.
    {
        const fsmtable::Machine parsed = parse_text(corpus("traffic_light.fsm"));
        std::mt19937 rng(11);
        auto machine = fsmtable_generated::makeTrafficLight();
        const auto events = make_events<fsmtable_generated::TrafficLightEvent,
                                        fsmtable_generated::TrafficLightKind>(parsed, rng, 400);
        log().clear();
        const Trace generated = drive(machine, events);
        const std::vector<std::string> actions = log();
        expect_agrees("corpus/traffic_light.fsm",
                      reference_trace(parsed, as_reference_events(events)), actions, generated);
        EXPECT_FALSE(actions.empty()) << "no action ran: the action log is untested here";
        EXPECT_GT(generated.no_match, 0U)
            << "nothing fell through: the no-match path is untested here";
    }
    {
        const fsmtable::Machine parsed = parse_text(corpus("guarded.fsm"));
        std::mt19937 rng(12);
        auto machine = fsmtable_generated::makeGuarded();
        const auto events
            = make_events<fsmtable_generated::GuardedEvent, fsmtable_generated::GuardedKind>(
                parsed, rng, 400);
        log().clear();
        const Trace generated = drive(machine, events);
        expect_agrees("corpus/guarded.fsm", reference_trace(parsed, as_reference_events(events)),
                      log(), generated);
        EXPECT_GT(generated.no_match, 0U)
            << "no guard ever failed: the guard path is untested here";
    }
    {
        const fsmtable::Machine parsed = parse_text(fixture("refined_pair.fsm"));
        std::mt19937 rng(13);
        auto machine = fsmtable_generated::makeRefinedPair();
        auto events = make_events<fsmtable_generated::RefinedPairEvent,
                                  fsmtable_generated::RefinedPairKind>(parsed, rng, 400);
        // The first event is planted on the guard's boundary. refined_pair's fallback leads to B,
        // which has no outgoing row at all, so a random first kind-1 event that missed the guard
        // would leave the rest of the run with nothing to move and no action to log.
        events.front()
            = fsmtable_generated::RefinedPairEvent{fsmtable_generated::RefinedPairKind::k1, 10};
        log().clear();
        const Trace generated = drive(machine, events);
        const std::vector<std::string> actions = log();
        expect_agrees("tests/fixtures/refined_pair.fsm",
                      reference_trace(parsed, as_reference_events(events)), actions, generated);
        EXPECT_FALSE(actions.empty()) << "the guarded row never ran its action";
    }
}

TEST(Generated, TheGuardedRowIsEmittedFirstAndWins) {
    // The property the canonical order exists for. refined_pair.fsm writes the unguarded fallback
    // FIRST; the emitted table must put the guarded row ahead of it, and the boundary must land
    // where the format says: below 10 the fallback applies, at or above 10 the refinement does.
    const auto& rows = fsmtable_generated::RefinedPairRows;
    ASSERT_GE(rows.size(), 2U);
    EXPECT_TRUE(rows[0].refined)
        << "the guarded row is not first: first-match-wins would make it unreachable";
    EXPECT_FALSE(rows[1].refined) << "the second row of the pair should be the fallback";
    EXPECT_EQ(rows[0].value, 10);

    auto machine = fsmtable_generated::makeRefinedPair();
    log().clear();

    // Each event is fired from A on purpose: the fallback moves the machine to B, where no row
    // exists at all, so a second event fired without going back to A would test nothing.
    machine.setCurrentState(fsmtable_generated::RefinedPairState::A);
    machine.process(
        fsmtable_generated::RefinedPairEvent{fsmtable_generated::RefinedPairKind::k1, 9});
    EXPECT_EQ(machine.currentStateName(), "B") << "9 is below the guard: the fallback applies";
    EXPECT_TRUE(log().empty()) << "the refinement ran below its boundary";

    machine.setCurrentState(fsmtable_generated::RefinedPairState::A);
    machine.process(
        fsmtable_generated::RefinedPairEvent{fsmtable_generated::RefinedPairKind::k1, 10});
    EXPECT_EQ(machine.currentStateName(), "C") << "10 satisfies `ge 10`: the refinement applies";
    ASSERT_EQ(log().size(), 1U);
    EXPECT_EQ(log()[0], "high");
}

TEST(Generated, NamedKindsBecomeNamedEnumeratorsAndAReversibleTable) {
    // NAMED_KINDS.md's payoff. `reset` is declared and no row uses it: it is still an enumerator,
    // because a declaration is part of the file. `k3` is the other case — a kind no directive
    // named, which keeps the spelling the generator has always used.
    EXPECT_EQ(static_cast<int>(fsmtable_generated::NamedKind::tick), 1);
    EXPECT_EQ(static_cast<int>(fsmtable_generated::NamedKind::stop), 2);
    EXPECT_EQ(static_cast<int>(fsmtable_generated::NamedKind::k3), 3);
    EXPECT_EQ(static_cast<int>(fsmtable_generated::NamedKind::reset), 7);

    EXPECT_EQ(fsmtable_generated::NamedKindNameOf(fsmtable_generated::NamedKind::tick), "tick");
    EXPECT_EQ(fsmtable_generated::NamedKindNameOf(fsmtable_generated::NamedKind::reset), "reset");
    EXPECT_TRUE(fsmtable_generated::NamedKindNameOf(fsmtable_generated::NamedKind::k3).empty())
        << "an undeclared kind must not be given an invented name";
    EXPECT_EQ(fsmtable_generated::NamedKindNames.size(), 3U);

    // The round trip that matters: a name the generator hands back has to be a name the format
    // accepts in, or code -> text is a one-way door.
    for (const std::pair<fsmtable_generated::NamedKind, std::string_view>& entry :
         fsmtable_generated::NamedKindNames) {
        const std::string name(entry.second);
        const std::string kind_text = std::to_string(static_cast<int>(entry.first));
        const std::string back = "version 1\nmachine M\ninitial A\nkind " + name + " = " + kind_text
                                 + "\ntransition A --" + name + "--> A\n";
        fsmtable::Error error{0, ""};
        std::vector<fsmtable::KindName> names;
        EXPECT_TRUE(fsmtable::parse(back, error, names).has_value())
            << "'" << name << "', which came out of the generator, was refused at line "
            << error.line << ": " << error.message;
    }
}

TEST(Generated, ANamedMachineRunsThroughItsOwnKindNames) {
    const fsmtable::Machine parsed = parse_text(fixture("named_kinds.fsm"));
    std::mt19937 rng(14);
    auto machine = fsmtable_generated::makeNamed();
    const auto events = make_events<fsmtable_generated::NamedEvent, fsmtable_generated::NamedKind>(
        parsed, rng, 400);
    log().clear();
    const Trace generated = drive(machine, events);
    const std::vector<std::string> actions = log();
    expect_agrees("tests/fixtures/named_kinds.fsm",
                  reference_trace(parsed, as_reference_events(events)), actions, generated);
    EXPECT_FALSE(actions.empty()) << "on_tick never ran: the named row is untested";
}

TEST(Generated, TheEmptyMachineIsGeneratedAndNeverMoves) {
    // One state, no rows, no kinds: the artifact still has to compile, and behave.
    const fsmtable::Machine parsed = parse_text(corpus("minimal.fsm"));
    auto machine = fsmtable_generated::makeMinimal();
    EXPECT_EQ(machine.currentStateName(), parsed.initial);
    unsigned moved = 0;
    for (int kind = 0; kind < 4; ++kind) {
        const bool fired = machine.process(fsmtable_generated::MinimalEvent{
            static_cast<fsmtable_generated::MinimalKind>(kind), kind});
        if (fired)
            ++moved;
    }
    EXPECT_EQ(moved, 0U) << "a machine with no rows moved";
    EXPECT_EQ(machine.currentStateName(), parsed.initial);
}