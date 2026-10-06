// Entry and exit actions per state, the second addition to the reader (ENTRY_EXIT.md).
//
// SPEC.md section 2 is frozen and says states are not declared separately: the first time a name
// appears it is declared. A `state` line therefore does not *declare* anything — it decorates a
// state the machine already has, which is why a decoration for a name no row or `initial` line
// mentions is an error and not a new state (ENTRY_EXIT.md).
#include "fsmtable.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fsmtable::Error;
using fsmtable::KindName;
using fsmtable::Machine;
using fsmtable::StateAction;

constexpr std::string_view kDoor = "version 1\n"
                                   "machine Door\n"
                                   "initial Closed\n"
                                   "kind open = 1\n"
                                   "kind close = 2\n"
                                   "state Closed exit on_exit_closed\n"
                                   "state Open entry on_entry_open exit on_exit_open\n"
                                   "transition Closed --open--> Open action on_opening\n"
                                   "transition Open --close--> Closed\n";

// Parses and requires success: the one place the optionals are unwrapped, visibly guarded,
// because gtest's ASSERT_* teaches clang-tidy's bugprone-unchecked-optional-access nothing.
struct Parsed {
    Machine machine;
    std::vector<KindName> names;
    std::vector<StateAction> states;
};

Parsed parse_ok(std::string_view text) {
    Error error{0, ""};
    std::vector<KindName> names;
    std::vector<StateAction> states;
    auto machine = fsmtable::parse(text, error, names, states);
    if (!machine.has_value()) {
        ADD_FAILURE() << "parse failed at line " << error.line << ": " << error.message;
        return Parsed{Machine{}, {}, {}};
    }
    return Parsed{*machine, names, states};
}

// The frozen two-argument reading, for the test that has to show it still sees the same machine.
Machine parse_frozen(std::string_view text) {
    Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);
    if (!machine.has_value()) {
        ADD_FAILURE() << "parse failed at line " << error.line << ": " << error.message;
        return Machine{};
    }
    return *machine;
}

void expect_rejected(std::string_view text, int line, std::string_view what) {
    Error error{0, ""};
    std::vector<KindName> names;
    std::vector<StateAction> states;
    const auto machine = fsmtable::parse(text, error, names, states);
    EXPECT_FALSE(machine.has_value()) << "a machine came back despite the error";
    EXPECT_EQ(error.line, line);
    EXPECT_NE(error.message.find(what), std::string::npos)
        << "message was '" << error.message << "', expected it to mention '" << std::string(what)
        << "'";
    EXPECT_TRUE(states.empty()) << "a refused file reported state actions";
}

// The four lines that carry a fault, in the place a fault is usually found: after `initial`.
std::string with_line(std::string_view line) {
    return "version 1\nmachine M\ninitial A\n" + std::string(line) + "transition A --1--> A\n";
}

TEST(StateActions, ParsesTheEntryAndExitOfEachState) {
    const Parsed parsed = parse_ok(kDoor);
    ASSERT_EQ(parsed.states.size(), 2U);

    // File order, one entry per state, with the clauses where the file put them.
    EXPECT_EQ(parsed.states[0].state, "Closed");
    EXPECT_EQ(parsed.states[0].enter, "");
    EXPECT_EQ(parsed.states[0].exit, "on_exit_closed");

    EXPECT_EQ(parsed.states[1].state, "Open");
    EXPECT_EQ(parsed.states[1].enter, "on_entry_open");
    EXPECT_EQ(parsed.states[1].exit, "on_exit_open");

    // The machine itself is untouched: a decoration decorates, it does not add anything.
    EXPECT_EQ(fsmtable::dump(parsed.machine), fsmtable::dump(parse_ok(kDoor).machine));
    EXPECT_EQ(parsed.machine.states.size(), 2U);
}

TEST(StateActions, TwoLinesForOneStateMergeIntoOne) {
    constexpr std::string_view kSplit = "version 1\n"
                                        "machine M\n"
                                        "initial A\n"
                                        "state A entry on_enter\n"
                                        "state A exit on_exit\n"
                                        "transition A --1--> B\n";
    const Parsed parsed = parse_ok(kSplit);
    ASSERT_EQ(parsed.states.size(), 1U) << "one entry per state, not one per line";
    EXPECT_EQ(parsed.states[0].state, "A");
    EXPECT_EQ(parsed.states[0].enter, "on_enter");
    EXPECT_EQ(parsed.states[0].exit, "on_exit");
}

TEST(StateActions, AStateMayBeDecoratedBeforeItIsUsed) {
    // The state does not have to exist yet: the check is over the whole file, so a decoration
    // block at the top reads naturally.
    constexpr std::string_view kForward = "version 1\n"
                                          "machine M\n"
                                          "initial A\n"
                                          "state B entry on_enter_b\n"
                                          "transition A --1--> B\n";
    const Parsed parsed = parse_ok(kForward);
    ASSERT_EQ(parsed.states.size(), 1U);
    EXPECT_EQ(parsed.states[0].state, "B");
    EXPECT_EQ(parsed.states[0].enter, "on_enter_b");
}

TEST(StateActions, DumpWritesTheStateLinesAndReparsesToTheSameThing) {
    const Parsed once = parse_ok(kDoor);
    const std::string first = fsmtable::dump(once.machine, once.names, once.states);

    // After the kind declarations, before the rows, sorted by state name: canonical, so two
    // files that decorate in different orders dump identically.
    EXPECT_NE(first.find("kind close = 2\nstate Closed exit on_exit_closed\n"
                         "state Open entry on_entry_open exit on_exit_open\ntransition"),
              std::string::npos)
        << first;

    const Parsed twice = parse_ok(first);
    EXPECT_EQ(fsmtable::dump(twice.machine, twice.names, twice.states), first);
    ASSERT_EQ(twice.states.size(), 2U);
    EXPECT_EQ(twice.states[0].state, "Closed");

    // Sorted by name is not the file's order, so compare the sets: the round trip is what has to
    // hold, and the dump is the canonical form of it.
    std::vector<std::string> before;
    before.reserve(once.states.size());
    for (const StateAction& entry : once.states) {
        before.push_back(entry.state + " " + entry.enter + " " + entry.exit);
    }
    std::vector<std::string> after;
    after.reserve(twice.states.size());
    for (const StateAction& entry : twice.states) {
        after.push_back(entry.state + " " + entry.enter + " " + entry.exit);
    }
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    EXPECT_EQ(after, before);
}

TEST(StateActions, TheNarrowerDumpsWriteWhatTheyWereGiven) {
    const Parsed parsed = parse_ok(kDoor);
    // dump(machine) and dump(machine, names) have nowhere to put the state actions, so they write
    // a machine without them — the same shape as dump(machine) dropping the kind names.
    EXPECT_EQ(fsmtable::dump(parsed.machine).find("state "), std::string::npos);
    EXPECT_EQ(fsmtable::dump(parsed.machine, parsed.names).find("state "), std::string::npos);
}

TEST(StateActions, TheFrozenParseStillSeesTheSameMachine) {
    // The two-argument form, unchanged: it reads the state lines and throws the decorations away.
    EXPECT_EQ(fsmtable::dump(parse_frozen(kDoor)), fsmtable::dump(parse_ok(kDoor).machine));
}

TEST(StateActions, RejectsAStateLineWithNoClause) {
    // A decoration with nothing in it would be dead text the dump could not write back.
    expect_rejected(with_line("state A\n"), 4, "entry or exit");
}

TEST(StateActions, RejectsARepeatedClause) {
    expect_rejected("version 1\nmachine M\ninitial A\nstate A entry one\nstate A entry two\n", 5,
                    "duplicate entry");
    expect_rejected("version 1\nmachine M\ninitial A\nstate A exit one\nstate A exit two\n", 5,
                    "duplicate exit");
}

TEST(StateActions, RejectsTheClausesOutOfOrder) {
    expect_rejected(with_line("state A exit on_exit entry on_entry\n"), 4, "after exit");
}

TEST(StateActions, RejectsAnUnknownStateName) {
    // Rule 7's pattern, and the whole point of decorating rather than declaring: a typo is an
    // error, not a new state with no transitions.
    expect_rejected(with_line("state Nobody entry on_enter\n"), 4, "no transition or initial");
}

TEST(StateActions, RejectsAMalformedClause) {
    expect_rejected(with_line("state A entry 1bad\n"), 4, "malformed action name");
    expect_rejected(with_line("state A enter on_enter\n"), 4, "unknown");
    expect_rejected(with_line("state A entry\n"), 4, "needs an action");
    expect_rejected(with_line("state A entry on_enter junk\n"), 4, "unknown clause");
}

} // namespace
