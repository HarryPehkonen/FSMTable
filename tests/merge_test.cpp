// The merge transform: `fsmtable::merge` — two machines composed into one, by name fusion.
//
// This suite exists for the properties the addition has to have: the output is canonical version 1
// text that PARSES, every state of both machines is in it exactly once, a state name the two
// machines share is FUSED (the seam — that is the whole point of composing), the merged machine's
// own identity comes from the first machine, and every way the two inputs can disagree is refused
// with a message that names what disagreed rather than fusing through it silently.
//
// The two properties the byte-level cases of `rename` are held to do not apply here, and the
// difference is the reason `merge` may go through `dump`: the output is a NEW machine with no
// comments of its own to keep, so canonical text is the correct answer rather than a loss
// (TRANSFORM.md). `WritesCanonicalVersionOneTextWithNoComments` is the case that pins it.
#include "fsmtable.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fsmtable::Error;
using fsmtable::KindName;
using fsmtable::Machine;
using fsmtable::MergeResult;
using fsmtable::StateAction;
using fsmtable::Transition;

// The pair the seam cases work on: the first machine ends in `SignedIn` and the second begins
// there, so the merge is a composition and not two machines sharing a file. The first also carries
// a comment, which no merge keeps — that is the second case below, not an oversight.
constexpr std::string_view kLogin = "# the first machine's own comment\n"
                                    "version 1\n"
                                    "machine Login\n"
                                    "initial Anonymous\n"
                                    "kind login = 1\n"
                                    "kind logout = 2\n"
                                    "transition Anonymous --login--> SignedIn\n"
                                    "transition SignedIn --logout--> Anonymous\n";

// The same machines without that comment, so a case can show the comment moved no byte.
constexpr std::string_view kLoginBare = "version 1\n"
                                        "machine Login\n"
                                        "initial Anonymous\n"
                                        "kind login = 1\n"
                                        "kind logout = 2\n"
                                        "transition Anonymous --login--> SignedIn\n"
                                        "transition SignedIn --logout--> Anonymous\n";

constexpr std::string_view kSession = "# the second machine's own comment\n"
                                      "version 1\n"
                                      "machine Session\n"
                                      "initial SignedIn\n"
                                      "kind expire = 3\n"
                                      "transition SignedIn --expire--> Anonymous\n";

// Written out by hand rather than derived from the inputs: a test that computed its expectation
// with the code under test would assert nothing.
constexpr std::string_view kComposed = "version 1\n"
                                       "machine Login\n"
                                       "initial Anonymous\n"
                                       "kind login = 1\n"
                                       "kind logout = 2\n"
                                       "kind expire = 3\n"
                                       "transition Anonymous --login--> SignedIn\n"
                                       "transition SignedIn --logout--> Anonymous\n"
                                       "transition SignedIn --expire--> Anonymous\n";

// The smallest legal machine, used as the second input when a case wants no rows added.
constexpr std::string_view kMinimal = "version 1\nmachine Minimal\ninitial A\n";

// The traffic light (the spec's own example): a cycle the first machine cannot reach.
constexpr std::string_view kTraffic = "version 1\n"
                                      "machine TrafficLight\n"
                                      "initial Red\n"
                                      "transition Red --1--> Green\n"
                                      "transition Green --2--> Yellow\n"
                                      "transition Yellow --3--> Red when ge 30\n";

// ---------------------------------------------------------------- helpers

// Merges and requires success. The unwrap is visible to the checker and to a reader
// (tests/parser_test.cpp says why the tests route through a helper like this).
MergeResult merged_ok(std::string_view a, std::string_view b) {
    Error error{0, ""};
    const auto result = fsmtable::merge(a, b, error);
    if (!result.has_value()) {
        ADD_FAILURE() << "merge refused: line " << error.line << ": " << error.message;
        return MergeResult{};
    }
    return *result;
}

// Merges and requires a refusal; returns what the caller has to assert about it.
struct Refusal {
    int line = -1;
    std::string message;
};

Refusal refused(std::string_view a, std::string_view b) {
    Error error{0, ""};
    const auto result = fsmtable::merge(a, b, error);
    if (result.has_value()) {
        ADD_FAILURE() << "merge wrote text where the case says it must refuse";
    }
    return Refusal{error.line, error.message};
}

Machine parse_ok(std::string_view text) {
    Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);
    if (!machine.has_value()) {
        ADD_FAILURE() << "the merged text does not parse, line " << error.line << ": "
                      << error.message;
        return Machine{};
    }
    return *machine;
}

std::vector<KindName> kinds_of(std::string_view text) {
    Error error{0, ""};
    std::vector<KindName> names;
    const auto machine = fsmtable::parse(text, error, names);
    if (!machine.has_value()) {
        ADD_FAILURE() << "the merged text does not parse, line " << error.line << ": "
                      << error.message;
    }
    return names;
}

std::vector<StateAction> actions_of(std::string_view text) {
    Error error{0, ""};
    std::vector<KindName> names;
    std::vector<StateAction> actions;
    const auto machine = fsmtable::parse(text, error, names, actions);
    if (!machine.has_value()) {
        ADD_FAILURE() << "the merged text does not parse, line " << error.line << ": "
                      << error.message;
    }
    return actions;
}

// The machine as one comparable line, so a failure shows both sides in the test log.
std::string render(const Machine& machine) {
    std::string out = "machine " + machine.name + ", initial " + machine.initial + ", states:";
    for (const std::string& state : machine.states) {
        out += " " + state;
    }
    for (const Transition& row : machine.transitions) {
        out += " | " + row.from + " --" + std::to_string(row.kind) + "--> " + row.to;
        if (row.has_when) {
            out += " when " + std::to_string(row.when_value);
        }
        if (!row.action.empty()) {
            out += " action " + row.action;
        }
    }
    return out;
}

std::string render(const std::vector<KindName>& names) {
    std::string out;
    for (const KindName& entry : names) {
        if (!out.empty()) {
            out += ", ";
        }
        out += entry.name + "=" + std::to_string(entry.kind);
    }
    return out;
}

std::string render(const std::vector<StateAction>& actions) {
    std::string out;
    for (const StateAction& entry : actions) {
        if (!out.empty()) {
            out += ", ";
        }
        out += entry.state + "{entry=" + (entry.enter.empty() ? "-" : entry.enter)
               + " exit=" + (entry.exit.empty() ? "-" : entry.exit) + "}";
    }
    return out;
}

// The reference for the fingerprint case: FNV-1a 64, the algorithm the generator's header names.
// Written out here rather than reused from the library, because a case that compared the library
// with itself would assert nothing.
std::uint64_t fnv1a(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL;
    }
    return hash;
}

// ---------------------------------------------------------------- fusion, and whose identity wins

TEST(MergeTest, FusesTheStateTheTwoMachinesShare) {
    const MergeResult merged = merged_ok(kLogin, kSession);
    EXPECT_EQ(merged.text, std::string(kComposed));
    const Machine machine = parse_ok(merged.text);
    // Two states, not three: `SignedIn` is one state the two machines both name, which is the seam.
    EXPECT_EQ(render(machine),
              "machine Login, initial Anonymous, states: Anonymous SignedIn | "
              "Anonymous --1--> SignedIn | SignedIn --2--> Anonymous | SignedIn --3--> Anonymous");
    EXPECT_TRUE(fsmtable::unreachable(machine).empty());
}

TEST(MergeTest, TakesTheFirstMachinesNameAndInitial) {
    const MergeResult merged = merged_ok(kLogin, kSession);
    const Machine machine = parse_ok(merged.text);
    EXPECT_EQ(machine.name, "Login");
    EXPECT_EQ(machine.initial, "Anonymous");
    // The second machine's initial is an ordinary state now, and it is still there — dropping it
    // would drop the seam.
    EXPECT_NE(std::find(machine.states.begin(), machine.states.end(), std::string("SignedIn")),
              machine.states.end());
}

TEST(MergeTest, MergingIsNotSymmetric) {
    // The order is the caller's: the first machine's identity wins, and the second's initial
    // becomes ordinary. Swapping the two arguments swaps both.
    const MergeResult merged = merged_ok(kSession, kLogin);
    const Machine machine = parse_ok(merged.text);
    EXPECT_EQ(machine.name, "Session");
    EXPECT_EQ(machine.initial, "SignedIn");
    EXPECT_EQ(machine.states.size(), 2U);
    EXPECT_TRUE(fsmtable::unreachable(machine).empty());
}

TEST(MergeTest, WritesCanonicalVersionOneTextWithNoComments) {
    const MergeResult merged = merged_ok(kLogin, kSession);
    EXPECT_EQ(merged.text.compare(0, 10, "version 1\n"), 0);
    EXPECT_EQ(merged.text.back(), '\n');
    // Both inputs carried a comment. Neither is here: the output is a new machine, and `dump` is
    // its writer. A merge that kept them would be a rename-shaped promise this verb does not make.
    EXPECT_EQ(merged.text.find('#'), std::string::npos);
}

TEST(MergeTest, CarriesTheNamedKindsOfBoth) {
    EXPECT_EQ(render(kinds_of(merged_ok(kLogin, kSession).text)), "login=1, logout=2, expire=3");
}

TEST(MergeTest, FusesAKindBothMachinesDeclareTheSameWay) {
    constexpr std::string_view kSiblingLeft
        = "version 1\nmachine Left\ninitial A\nkind step = 1\ntransition A --step--> B\n";
    constexpr std::string_view kSiblingRight
        = "version 1\nmachine Right\ninitial B\nkind step = 1\ntransition B --step--> A\n";
    const MergeResult merged = merged_ok(kSiblingLeft, kSiblingRight);
    EXPECT_EQ(merged.text, "version 1\n"
                           "machine Left\n"
                           "initial A\n"
                           "kind step = 1\n"
                           "transition A --step--> B\n"
                           "transition B --step--> A\n");
    EXPECT_EQ(render(kinds_of(merged.text)), "step=1");
}

TEST(MergeTest, KeepsARowThatWritesItsKindAsANumber) {
    // The number a row writes is not a name: it survives the merge even when no machine declares
    // it.
    constexpr std::string_view kNumbers
        = "version 1\nmachine Numbers\ninitial A\ntransition A --9--> B\n";
    const MergeResult merged = merged_ok(kNumbers, kMinimal);
    EXPECT_EQ(merged.text, "version 1\n"
                           "machine Numbers\n"
                           "initial A\n"
                           "transition A --9--> B\n");
    EXPECT_TRUE(kinds_of(merged.text).empty());
}

// ---------------------------------------------------------------- what the two inputs may not
// disagree about

TEST(MergeTest, RefusesOneKindNumberUnderTwoNames) {
    constexpr std::string_view kStep
        = "version 1\nmachine Left\ninitial A\nkind step = 1\ntransition A --step--> B\n";
    constexpr std::string_view kTick
        = "version 1\nmachine Right\ninitial B\nkind tick = 1\ntransition B --tick--> A\n";
    const Refusal refusal = refused(kStep, kTick);
    EXPECT_EQ(refusal.line, 0);
    // Both declarations are named, and the number they disagree about, so the caller knows which
    // pair of lines to fix — and that the fix is `rename`, not a hand edit.
    EXPECT_NE(refusal.message.find("'step'"), std::string::npos);
    EXPECT_NE(refusal.message.find("'tick'"), std::string::npos);
    EXPECT_NE(refusal.message.find('1'), std::string::npos);
    EXPECT_NE(refusal.message.find("kind"), std::string::npos);
}

TEST(MergeTest, RefusesOneKindNameForTwoNumbers) {
    constexpr std::string_view kOne
        = "version 1\nmachine Left\ninitial A\nkind tick = 1\ntransition A --tick--> B\n";
    constexpr std::string_view kTwo
        = "version 1\nmachine Right\ninitial B\nkind tick = 2\ntransition B --tick--> A\n";
    const Refusal refusal = refused(kOne, kTwo);
    EXPECT_EQ(refusal.line, 0);
    EXPECT_NE(refusal.message.find("'tick'"), std::string::npos);
    EXPECT_NE(refusal.message.find('1'), std::string::npos);
    EXPECT_NE(refusal.message.find('2'), std::string::npos);
}

TEST(MergeTest, RefusesTwoUnguardedRowsForOnePair) {
    constexpr std::string_view kLeft
        = "version 1\nmachine Left\ninitial X\ntransition X --1--> P\n";
    constexpr std::string_view kRight
        = "version 1\nmachine Right\ninitial X\ntransition X --1--> Q\n";
    const Refusal refusal = refused(kLeft, kRight);
    EXPECT_EQ(refusal.line, 0);
    EXPECT_NE(refusal.message.find("'X'"), std::string::npos);
    EXPECT_NE(refusal.message.find("rule 9"), std::string::npos);
}

TEST(MergeTest, RefusesTwoGuardedRowsForOnePair) {
    constexpr std::string_view kLeft
        = "version 1\nmachine Left\ninitial X\ntransition X --1--> P when eq 1\n";
    constexpr std::string_view kRight
        = "version 1\nmachine Right\ninitial X\ntransition X --1--> Q when eq 2\n";
    const Refusal refusal = refused(kLeft, kRight);
    EXPECT_EQ(refusal.line, 0);
    EXPECT_NE(refusal.message.find("'X'"), std::string::npos);
}

TEST(MergeTest, AllowsAGuardedRowFromOneMachineAndAnUnguardedRowFromTheOther) {
    // Rule 9 allows one guarded row and one unguarded row per pair, and they may come from
    // different machines: this is the shape a composition actually wants.
    constexpr std::string_view kLeft
        = "version 1\nmachine Left\ninitial X\ntransition X --1--> P\n";
    constexpr std::string_view kRight
        = "version 1\nmachine Right\ninitial X\ntransition X --1--> Q when eq 1\n";
    const MergeResult merged = merged_ok(kLeft, kRight);
    EXPECT_EQ(merged.text, "version 1\n"
                           "machine Left\n"
                           "initial X\n"
                           "transition X --1--> Q when eq 1\n"
                           "transition X --1--> P\n");
    const Machine machine = parse_ok(merged.text);
    ASSERT_EQ(machine.transitions.size(), 2U);
    // Canonical order puts the refinement first, which is the precedence the format reads.
    EXPECT_TRUE(machine.transitions[0].has_when);
    EXPECT_FALSE(machine.transitions[1].has_when);
}

TEST(MergeTest, RefusesToMergeAMachineWithItselfBecauseEveryRowWouldDouble) {
    // Not a bug: fusion is by name, so a machine merged with itself carries two rows for every pair
    // it has, which rule 9 forbids. The message names the first pair that doubled, which is what
    // tells a caller that the two inputs were not two machines at all.
    const Refusal refusal = refused(kLogin, kLogin);
    EXPECT_EQ(refusal.line, 0);
    EXPECT_NE(refusal.message.find("Anonymous"), std::string::npos);
}

// ---------------------------------------------------------------- what the result may not be

TEST(MergeTest, RefusesAResultThatLeavesTheSecondMachinesStatesUnreachable) {
    // The two machines share no name, so nothing leads from the merged initial into the cycle:
    // every state of the second machine is unreachable, and that is a finding, not a file.
    const Refusal refusal = refused(kMinimal, kTraffic);
    EXPECT_EQ(refusal.line, 0);
    EXPECT_NE(refusal.message.find("unreachable"), std::string::npos);
    EXPECT_NE(refusal.message.find("Red"), std::string::npos);
    EXPECT_NE(refusal.message.find("Green"), std::string::npos);
    EXPECT_NE(refusal.message.find("Yellow"), std::string::npos);
}

TEST(MergeTest, RefusesAResultWhereTheFirstMachinesOwnOrphanIsStillUnreachable) {
    constexpr std::string_view kOrphaned = "version 1\n"
                                           "machine Orphaned\n"
                                           "initial Start\n"
                                           "transition Start --1--> Done\n"
                                           "transition Loose --2--> Start\n";
    const Refusal refusal = refused(kOrphaned, kMinimal);
    EXPECT_EQ(refusal.line, 0);
    EXPECT_NE(refusal.message.find("Loose"), std::string::npos);
}

TEST(MergeTest, RefusesAFirstMachineThatDoesNotParse) {
    constexpr std::string_view kJunk = "version 1\nmachine M\ninitial A\nbogus A --1--> B\n";
    const Refusal refusal = refused(kJunk, kMinimal);
    EXPECT_EQ(refusal.line, 4);
    EXPECT_NE(refusal.message.find("bogus"), std::string::npos);
}

TEST(MergeTest, RefusesASecondMachineThatDoesNotParse) {
    constexpr std::string_view kJunk = "version 1\nmachine M\ninitial A\nbogus A --1--> B\n";
    const Refusal refusal = refused(kMinimal, kJunk);
    EXPECT_EQ(refusal.line, 4);
    EXPECT_NE(refusal.message.find("bogus"), std::string::npos);
}

TEST(MergeTest, MergesTwoMachinesThatAreTheSameAndHaveNoRows) {
    const MergeResult merged = merged_ok(kMinimal, kMinimal);
    EXPECT_EQ(merged.text, std::string(kMinimal));
    EXPECT_TRUE(merged.warnings.empty());
}

// ---------------------------------------------------------------- entry and exit clauses

TEST(MergeTest, CarriesBothMachinesClausesAndFusesTheSharedState) {
    constexpr std::string_view kLeft = "version 1\n"
                                       "machine Left\n"
                                       "initial A\n"
                                       "state A entry on_entry_a\n"
                                       "transition A --1--> B\n";
    constexpr std::string_view kRight = "version 1\n"
                                        "machine Right\n"
                                        "initial B\n"
                                        "state A exit on_exit_a\n"
                                        "state B entry on_entry_b exit on_exit_b\n"
                                        "transition B --2--> A\n";
    const MergeResult merged = merged_ok(kLeft, kRight);
    // The two decorations of `A` fuse clause by clause. `B` is the second machine's own initial, so
    // it keeps its exit clause and loses its entry clause — the rule the next case spells out.
    EXPECT_EQ(render(actions_of(merged.text)),
              "A{entry=on_entry_a exit=on_exit_a}, B{entry=- exit=on_exit_b}");
    ASSERT_EQ(merged.warnings.size(), 1U);
    EXPECT_NE(merged.warnings[0].find("on_entry_b"), std::string::npos);
}

TEST(MergeTest, DropsTheSecondMachinesInitialEntryClauseAndWarns) {
    constexpr std::string_view kDevice = "version 1\nmachine Device\ninitial Off\ntransition Off "
                                         "--1--> On\ntransition On --2--> Off\n";
    constexpr std::string_view kWatch = "version 1\nmachine Watch\ninitial On\nstate On entry "
                                        "on_entry_watch\ntransition On --3--> Off\n";
    const MergeResult merged = merged_ok(kDevice, kWatch);
    ASSERT_EQ(merged.warnings.size(), 1U);
    EXPECT_NE(merged.warnings[0].find("on_entry_watch"), std::string::npos);
    EXPECT_NE(merged.warnings[0].find("On"), std::string::npos);
    // Dropped rather than renamed: creation is not entering, and the merged machine is created.
    EXPECT_EQ(merged.text.find("on_entry_watch"), std::string::npos);
    EXPECT_EQ(merged.text.find("state "), std::string::npos);
    EXPECT_EQ(merged.text, "version 1\n"
                           "machine Device\n"
                           "initial Off\n"
                           "transition Off --1--> On\n"
                           "transition On --2--> Off\n"
                           "transition On --3--> Off\n");
}

TEST(MergeTest, KeepsTheSecondMachinesEntryClauseWhenItIsNotTheMergedInitial) {
    constexpr std::string_view kLeft
        = "version 1\nmachine Left\ninitial A\ntransition A --1--> B\n";
    constexpr std::string_view kRight = "version 1\n"
                                        "machine Right\n"
                                        "initial B\n"
                                        "state A entry on_entry_a\n"
                                        "transition B --2--> A\n";
    const MergeResult merged = merged_ok(kLeft, kRight);
    EXPECT_TRUE(merged.warnings.empty());
    EXPECT_EQ(render(actions_of(merged.text)), "A{entry=on_entry_a exit=-}");
}

TEST(MergeTest, DoesNotDropTheSecondEntryClauseWhenTheTwoInitialsAreTheSameState) {
    // Both machines begin in `X`, so `X` is the merged initial and not an ordinary state: the
    // clause is fused like any other decoration, and nothing is dropped.
    constexpr std::string_view kLeft
        = "version 1\nmachine Left\ninitial X\ntransition X --1--> Y\n";
    constexpr std::string_view kRight = "version 1\n"
                                        "machine Right\n"
                                        "initial X\n"
                                        "state X entry on_entry_x\n"
                                        "transition X --2--> Y\n";
    const MergeResult merged = merged_ok(kLeft, kRight);
    EXPECT_TRUE(merged.warnings.empty());
    EXPECT_EQ(merged.text, "version 1\n"
                           "machine Left\n"
                           "initial X\n"
                           "state X entry on_entry_x\n"
                           "transition X --1--> Y\n"
                           "transition X --2--> Y\n");
}

TEST(MergeTest, KeepsTheFirstMachinesClauseWhenBothDecorateTheSameState) {
    constexpr std::string_view kLeft
        = "version 1\nmachine Left\ninitial A\nstate A entry on_left\ntransition A --1--> B\n";
    constexpr std::string_view kRight
        = "version 1\nmachine Right\ninitial B\nstate A entry on_right\ntransition B --2--> A\n";
    const MergeResult merged = merged_ok(kLeft, kRight);
    EXPECT_TRUE(merged.warnings.empty());
    EXPECT_EQ(render(actions_of(merged.text)), "A{entry=on_left exit=-}");
}

TEST(MergeTest, FusesAStateNameTheTwoMachinesUseWithDifferentSemantics) {
    // The contract, recorded rather than detected: fusion is by NAME, and a caller who fuses two
    // states meant to. Nothing here inspects what each machine did with the name.
    constexpr std::string_view kLeft
        = "version 1\nmachine Left\ninitial Same\ntransition Same --1--> Other\n";
    constexpr std::string_view kRight
        = "version 1\nmachine Right\ninitial Other\ntransition Same --2--> Other\n";
    const MergeResult merged = merged_ok(kLeft, kRight);
    const Machine machine = parse_ok(merged.text);
    EXPECT_EQ(machine.states.size(), 2U);
    EXPECT_EQ(machine.transitions.size(), 2U);
}

// ---------------------------------------------------------------- the fingerprint

TEST(MergeTest, TheFingerprintIsTheFnv1aOfTheTextItReturns) {
    const MergeResult merged = merged_ok(kLogin, kSession);
    EXPECT_EQ(merged.fingerprint, fnv1a(merged.text));
}

TEST(MergeTest, TheFingerprintIgnoresTheCommentsAndBlankLinesOfTheInputs) {
    // The fingerprint covers the merged CANONICAL form, so the first machine's comment cannot move
    // it — which is what lets a merged file's fingerprint be compared with an artifact generated
    // from the same file (`fsmtable-gen` strips the same comments).
    const MergeResult commented = merged_ok(kLogin, kSession);
    const MergeResult bare = merged_ok(kLoginBare, kSession);
    EXPECT_EQ(commented.text, bare.text);
    EXPECT_EQ(commented.fingerprint, bare.fingerprint);
}

TEST(MergeTest, TheFingerprintMovesWhenTheMergedMachineMoves) {
    constexpr std::string_view kOther
        = "version 1\nmachine Minimal\ninitial A\ntransition A --1--> B\n";
    EXPECT_NE(merged_ok(kMinimal, kMinimal).fingerprint, merged_ok(kOther, kMinimal).fingerprint);
}

} // namespace
