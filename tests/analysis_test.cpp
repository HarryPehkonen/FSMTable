// FSMTable stage B tests — the analysis functions of SPEC.md section 8, and the addition beside
// them (`partial_pairs`, 2026-10-06).
//
// Written BEFORE the implementations, against stubs that return nothing: every test below
// was watched fail, which is the RED this stage needed. Section 8 fixes the names, the
// signatures and the order ("first-appearance"); everything else pinned here is a reading of
// its wording, and each reading is recorded in QUESTIONS.md (Q3, Q4).
//
// The helper here is deliberately not shared with tests/parser_test.cpp: that file is the
// frozen evidence for stage A, and a shared header would mean editing it. The unwrap is
// guarded for the same reason it is there — clang-tidy's bugprone-unchecked-optional-access
// cannot see what gtest's ASSERT_* macros do, and a guard a reader can see beats a
// suppression.
#include "fsmtable.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fsmtable::Machine;
using fsmtable::Op;
using fsmtable::PartialPair;
using fsmtable::Transition;

[[noreturn]] void no_machine(const std::string& what) {
    ADD_FAILURE() << "expected a machine: " << what;
    std::abort();
}

// Returns by value: a helper that returned a reference would have to hand back a reference
// into the optional it just unwrapped, which dies with it. It takes a string_view because
// parse does — the example text is a compile-time literal, and copying it into a string
// just to hand it over would be work with no reader.
Machine machine_of(std::string_view text, const char* what) {
    fsmtable::Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);
    if (!machine.has_value()) {
        no_machine(std::string(what) + ": " + error.message);
    }
    return *machine;
}

// A machine built by hand, so a test can name a shape the parser would have to be contorted
// to produce: a state nothing points at, an island, a self-loop, a row naming a state that
// is not in `states`.
Machine build(const std::string& initial, std::vector<std::string> states,
              std::vector<Transition> transitions) {
    Machine machine;
    machine.name = "Hand";
    machine.initial = initial;
    machine.states = std::move(states);
    machine.transitions = std::move(transitions);
    return machine;
}

Transition edge(const std::string& from, int kind, const std::string& to) {
    return Transition{from, kind, false, Op::Eq, 0, to, ""};
}

Transition guarded_edge(const std::string& from, Op op, int value, const std::string& to) {
    return Transition{from, 1, true, op, value, to, ""};
}

Transition guarded(const std::string& from, int kind, const std::string& to) {
    return Transition{from, kind, true, Op::Lt, 3, to, ""};
}

std::string joined(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        if (!out.empty())
            out += ",";
        out += name;
    }
    return out;
}

// SPEC.md section 2's example, as text: Red -> Green -> Yellow -> Red. Every state is
// reachable, none is a sink, and `states` is in first-appearance order.
constexpr std::string_view kExample = "version 1\n"
                                      "machine TrafficLight\n"
                                      "initial Red\n"
                                      "transition Red --1--> Green action on_red_green\n"
                                      "transition Green --2--> Yellow\n"
                                      "transition Yellow --3--> Red when ge 30\n";

} // namespace

// ---------------------------------------------------------------- unreachable()

TEST(Unreachable, IsEmptyWhenEveryStateIsReachable) {
    const Machine machine = machine_of("version 1\nmachine M\ninitial A\ntransition A --1--> B\n"
                                       "transition B --2--> C\n",
                                       "a three-state chain");
    EXPECT_EQ(joined(fsmtable::unreachable(machine)), "");
}

TEST(Unreachable, ListsAStateNothingPointsAt) {
    const Machine machine = build("A", {"A", "B", "Orphan"}, {edge("A", 1, "B")});
    EXPECT_EQ(joined(fsmtable::unreachable(machine)), "Orphan");
}

TEST(Unreachable, IsInFirstAppearanceOrderNotAlphabetical) {
    const Machine machine = build("Mid", {"Mid", "Zulu", "Alpha"}, {});
    // First appearance is Mid, Zulu, Alpha; sorted would be Alpha, Zulu.
    EXPECT_EQ(joined(fsmtable::unreachable(machine)), "Zulu,Alpha");
}

TEST(Unreachable, NeverIncludesTheInitialState) {
    const Machine machine = build("Solo", {"Solo"}, {});
    EXPECT_EQ(joined(fsmtable::unreachable(machine)), "");
}

TEST(Unreachable, CountsAGuardedEdgeAsAPath) {
    // Reachability is structural: a `when` clause gates a row's firing, not its existence
    // (QUESTIONS.md Q3).
    const Machine machine = build("A", {"A", "B"}, {guarded_edge("A", Op::Ge, 30, "B")});
    EXPECT_EQ(joined(fsmtable::unreachable(machine)), "");
}

TEST(Unreachable, FollowsACycleThroughEveryStateInIt) {
    const Machine machine = build("A", {"A", "B", "C", "Island"},
                                  {edge("A", 1, "B"), edge("B", 2, "C"), edge("C", 3, "A")});
    EXPECT_EQ(joined(fsmtable::unreachable(machine)), "Island");
}

TEST(Unreachable, ASelfLoopDoesNotMakeAStateReachable) {
    const Machine machine = build("A", {"A", "Loop"}, {edge("Loop", 1, "Loop")});
    EXPECT_EQ(joined(fsmtable::unreachable(machine)), "Loop");
}

TEST(Unreachable, IgnoresARowToAStateThatIsNotDeclared) {
    // Only a hand-built Machine can do this — the parser declares every name it sees. The
    // reading is recorded as Q4: the result stays a subset of `states`.
    const Machine machine = build("A", {"A", "B"}, {edge("A", 1, "Ghost")});
    EXPECT_EQ(joined(fsmtable::unreachable(machine)), "B");
}

TEST(Unreachable, IsEmptyForTheSpecExample) {
    const Machine machine = machine_of(kExample, "section 2's example");
    EXPECT_EQ(joined(fsmtable::unreachable(machine)), "");
}

// ---------------------------------------------------------------- sink_states()

TEST(SinkStates, ListsStatesWithNoOutgoingTransition) {
    const Machine machine = build("A", {"A", "B", "C"}, {edge("A", 1, "B"), edge("A", 2, "C")});
    EXPECT_EQ(joined(fsmtable::sink_states(machine)), "B,C");
}

TEST(SinkStates, IsInFirstAppearanceOrderNotAlphabetical) {
    const Machine machine = build("Alpha", {"Alpha", "Zulu", "Mid"},
                                  {edge("Alpha", 1, "Zulu"), edge("Zulu", 2, "Alpha")});
    EXPECT_EQ(joined(fsmtable::sink_states(machine)), "Mid");
}

TEST(SinkStates, ASelfLoopIsAnOutgoingTransition) {
    const Machine machine = build("A", {"A", "B"}, {edge("A", 1, "A"), edge("B", 1, "B")});
    EXPECT_EQ(joined(fsmtable::sink_states(machine)), "");
}

TEST(SinkStates, CountsAGuardedOnlyRowAsOutgoing) {
    const Machine machine = build("A", {"A", "B"}, {guarded_edge("A", Op::Lt, 5, "B")});
    EXPECT_EQ(joined(fsmtable::sink_states(machine)), "B");
}

TEST(SinkStates, CanIncludeTheInitialState) {
    const Machine machine = build("Only", {"Only", "Reached"}, {edge("Reached", 1, "Only")});
    EXPECT_EQ(joined(fsmtable::sink_states(machine)), "Only");
}

TEST(SinkStates, IsEmptyForTheSpecExample) {
    const Machine machine = machine_of(kExample, "section 2's example");
    EXPECT_EQ(joined(fsmtable::sink_states(machine)), "");
}

// ------------------------------------------------- the two together, and the corpus

TEST(Analysis, AStateCanBeBothUnreachableAndASink) {
    // The lists are independent: an unreachable island with no way out is in both, and
    // neither function is asked to exclude it.
    const Machine machine = build("A", {"A", "Dead"}, {edge("A", 1, "A")});
    EXPECT_EQ(joined(fsmtable::unreachable(machine)), "Dead");
    EXPECT_EQ(joined(fsmtable::sink_states(machine)), "Dead");
}

TEST(Analysis, EveryParsedCorpusMachineAnalysesWithinItsOwnStates) {
    // The corpus of SPEC.md section 7, held to the contract both functions share: names come
    // from `states`, in first-appearance order, with no duplicates, and the initial state is
    // never called unreachable.
    const std::filesystem::path dir(FSMTABLE_CORPUS_DIR);
    ASSERT_TRUE(std::filesystem::is_directory(dir)) << "no corpus directory at " << dir;
    std::size_t parsed_files = 0;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        if (entry.path().extension() != ".fsm")
            continue;
        std::ifstream in(entry.path());
        ASSERT_TRUE(in.good()) << "cannot read " << entry.path();
        std::ostringstream text;
        text << in.rdbuf();
        fsmtable::Error error{0, ""};
        const auto machine = fsmtable::parse(text.str(), error);
        if (!machine.has_value())
            continue; // the invalid_*.fsm files; section 7's parser test owns their verdict
        ++parsed_files;
        const Machine& m = *machine;
        // Named locals, not temporaries: taking a temporary's address here would leave the
        // loop reading freed vectors.
        const std::vector<std::string> orphans = fsmtable::unreachable(m);
        const std::vector<std::string> sinks = fsmtable::sink_states(m);
        for (const std::vector<std::string>& list : {orphans, sinks}) {
            for (const std::string& state : list) {
                EXPECT_NE(std::find(m.states.begin(), m.states.end(), state), m.states.end())
                    << name << ": analysed a name that is not a declared state: " << state;
            }
            EXPECT_EQ(std::adjacent_find(list.begin(), list.end()), list.end())
                << name << ": duplicate names in the result";
        }
        EXPECT_EQ(std::find(orphans.begin(), orphans.end(), m.initial), orphans.end())
            << name << ": the initial state was called unreachable";
    }
    EXPECT_GT(parsed_files, 0U) << "the corpus walk parsed no machine at all";
}

// ---------------------------------------------------------------- partial_pairs()

TEST(PartialPairs, IsEmptyWhenNoPairCarriesAGuard) {
    const Machine machine = machine_of("version 1\nmachine M\ninitial A\ntransition A --1--> B\n"
                                       "transition B --1--> A\n",
                                       "a machine with no guards at all");
    EXPECT_TRUE(fsmtable::partial_pairs(machine).empty());
}

TEST(PartialPairs, IsEmptyForAMachineWithNoRows) {
    const Machine machine = build("Solo", {"Solo"}, {});
    EXPECT_TRUE(fsmtable::partial_pairs(machine).empty());
}

TEST(PartialPairs, ListsAPairWhoseOnlyRowIsGuarded) {
    // One guarded row and nothing behind it: a false clause leaves no row to match, so the
    // event comes back unhandled instead of being ignored deliberately.
    const Machine machine = build("A", {"A", "B"}, {guarded("A", 7, "B"), edge("A", 8, "B")});
    const std::vector<PartialPair> pairs = fsmtable::partial_pairs(machine);
    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_EQ(pairs[0].from, "A");
    EXPECT_EQ(pairs[0].kind, 7);
}

TEST(PartialPairs, DoesNotListAPairThatAlsoHasAnUnguardedRow) {
    // Canonical order leaves the unguarded row last in its pair, so it matches whatever the
    // guard let through: the pair can never drop an event, whichever way the guard goes.
    const Machine machine = build("A", {"A", "B"}, {guarded("A", 7, "B"), edge("A", 7, "A")});
    EXPECT_TRUE(fsmtable::partial_pairs(machine).empty());
}

TEST(PartialPairs, TreatsASelfTransitionPairLikeAnyOther) {
    const Machine machine = build("A", {"A"}, {guarded("A", 1, "A")});
    const std::vector<PartialPair> pairs = fsmtable::partial_pairs(machine);
    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_EQ(pairs[0].from, "A");
    EXPECT_EQ(pairs[0].kind, 1);
}

TEST(PartialPairs, ListsAPairOnceEvenWithMoreThanOneGuardedRow) {
    // Rule 9 forbids this shape and the parser refuses it, but the analysis is defined for a
    // machine built by hand too: two guarded rows are still one pair.
    const Machine machine = build("A", {"A", "B"}, {guarded("A", 7, "B"), guarded("A", 7, "A")});
    EXPECT_EQ(fsmtable::partial_pairs(machine).size(), 1U);
}

TEST(PartialPairs, KeysOnStateAndKindTogether) {
    // The same kind in two states is two pairs, and only the one with no unguarded row is partial.
    const Machine machine = build("A", {"A", "B", "C"},
                                  {guarded("A", 7, "B"), guarded("C", 7, "B"), edge("C", 7, "A")});
    const std::vector<PartialPair> pairs = fsmtable::partial_pairs(machine);
    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_EQ(pairs[0].from, "A");
}

TEST(PartialPairs, IsInFirstAppearanceOrderNotKindOrder) {
    const Machine machine
        = build("A", {"A", "B", "C"}, {guarded("A", 9, "B"), guarded("A", 2, "C")});
    const std::vector<PartialPair> pairs = fsmtable::partial_pairs(machine);
    ASSERT_EQ(pairs.size(), 2U);
    EXPECT_EQ(pairs[0].kind, 9);
    EXPECT_EQ(pairs[1].kind, 2);
}
