// Named event kinds: a row's arrow slot may hold a name that a `kind` directive declared.
//
// SPEC.md section 2 is frozen and says a kind is a decimal integer, and its section 4 API is
// frozen too, so nothing here is a second format: a name is another spelling for the same
// number, resolved while parsing, and the Machine that comes back does not record which
// spelling was used. That is what makes the two tests below that dump a named file and a
// numeric file and compare the results meaningful.
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

// The spec's own example (SPEC.md section 2), spelled both ways. `warn` is declared before
// `advance`, so declaration order and kind order differ — which is the point of asserting both.
constexpr std::string_view kNamed = "version 1\n"
                                    "machine TrafficLight\n"
                                    "initial Red\n"
                                    "kind warn = 2\n"
                                    "kind advance = 1\n"
                                    "transition Red --advance--> Green action on_red_green\n"
                                    "transition Green --warn--> Yellow\n"
                                    "transition Yellow --advance--> Red when ge 30\n";

constexpr std::string_view kNumeric = "version 1\n"
                                      "machine TrafficLight\n"
                                      "initial Red\n"
                                      "transition Red --1--> Green action on_red_green\n"
                                      "transition Green --2--> Yellow\n"
                                      "transition Yellow --1--> Red when ge 30\n";

// Parses and requires success. The unwrap happens here and nowhere else, visibly guarded:
// gtest's ASSERT_* does not teach clang-tidy's bugprone-unchecked-optional-access anything about
// an optional (tests/parser_test.cpp says the same where it unwraps one), so every test in this
// file goes through these two functions instead of touching a `std::optional` itself.
struct Parsed {
    Machine machine;
    std::vector<KindName> names;
};

Parsed parse_ok(std::string_view text) {
    Error error{0, ""};
    std::vector<KindName> names;
    auto machine = fsmtable::parse(text, error, names);
    if (!machine.has_value()) {
        ADD_FAILURE() << "parse failed at line " << error.line << ": " << error.message;
        return Parsed{Machine{}, {}};
    }
    return Parsed{*machine, names};
}

// The frozen two-argument form, for the test that holds it to the same machine.
Machine frozen_parse_ok(std::string_view text) {
    Error error{0, ""};
    auto machine = fsmtable::parse(text, error);
    if (!machine.has_value()) {
        ADD_FAILURE() << "the two-argument parse failed at line " << error.line << ": "
                      << error.message;
        return Machine{};
    }
    return *machine;
}

// Requires the error contract — no machine, this line — and that the message names the fault.
// "unknown kind name" must not pass for "malformed kind token": a caller fixing the file needs
// to know whether to declare the name or to correct the spelling.
void expect_kind_error(std::string_view text, int line, std::string_view what) {
    Error error{0, ""};
    std::vector<KindName> names;
    const auto machine = fsmtable::parse(text, error, names);
    EXPECT_FALSE(machine.has_value()) << "a machine came back despite the error";
    EXPECT_EQ(error.line, line);
    EXPECT_NE(error.message.find(what), std::string::npos)
        << "message was '" << error.message << "', expected it to mention '" << std::string(what)
        << "'";
}

std::vector<KindName> sorted(std::vector<KindName> names) {
    std::sort(names.begin(), names.end(), [](const KindName& a, const KindName& b) {
        return a.kind < b.kind || (a.kind == b.kind && a.name < b.name);
    });
    return names;
}

TEST(NamedKinds, ResolvesADeclaredNameToItsNumber) {
    const Parsed parsed = parse_ok(kNamed);

    // Declaration order — the file's order, like `states` — not the numeric order.
    ASSERT_EQ(parsed.names.size(), 2U);
    EXPECT_EQ(parsed.names[0].name, "warn");
    EXPECT_EQ(parsed.names[0].kind, 2);
    EXPECT_EQ(parsed.names[1].name, "advance");
    EXPECT_EQ(parsed.names[1].kind, 1);

    ASSERT_EQ(parsed.machine.transitions.size(), 3U);
    EXPECT_EQ(parsed.machine.transitions[0].kind, 1); // Red --advance-->
    EXPECT_EQ(parsed.machine.transitions[1].kind, 2); // Green --warn-->
    EXPECT_EQ(parsed.machine.transitions[2].kind, 1); // Yellow --advance-->
}

TEST(NamedKinds, ANameAndItsNumberAreTheSameMachine) {
    EXPECT_EQ(fsmtable::dump(parse_ok(kNamed).machine), fsmtable::dump(parse_ok(kNumeric).machine));
    EXPECT_TRUE(parse_ok(kNumeric).names.empty())
        << "a file that declares no names must report none";
}

TEST(NamedKinds, TheFrozenParseSeesTheSameMachine) {
    EXPECT_EQ(fsmtable::dump(frozen_parse_ok(kNamed)), fsmtable::dump(parse_ok(kNamed).machine));
}

TEST(NamedKinds, TheFrozenDumpWritesNumbers) {
    const std::string dumped = fsmtable::dump(parse_ok(kNamed).machine);
    EXPECT_EQ(dumped.find("kind "), std::string::npos) << dumped;
    EXPECT_NE(dumped.find("transition Red --1--> Green"), std::string::npos) << dumped;
}

TEST(NamedKinds, DumpWritesTheDeclarationsAndTheNames) {
    const Parsed parsed = parse_ok(kNamed);
    const std::string dumped = fsmtable::dump(parsed.machine, parsed.names);

    // Declarations first, ascending by number: a canonical order, so that two files declaring the
    // same names in different orders dump identically.
    EXPECT_NE(dumped.find("initial Red\nkind advance = 1\nkind warn = 2\n"), std::string::npos)
        << dumped;
    EXPECT_NE(dumped.find("transition Red --advance--> Green action on_red_green\n"),
              std::string::npos)
        << dumped;
    EXPECT_NE(dumped.find("transition Yellow --advance--> Red when ge 30\n"), std::string::npos)
        << dumped;
}

TEST(NamedKinds, AnUndeclaredNumberStillDumpsAsANumber) {
    constexpr std::string_view kMixed = "version 1\n"
                                        "machine M\n"
                                        "initial A\n"
                                        "kind tick = 1\n"
                                        "transition A --tick--> B\n"
                                        "transition A --3--> B when ge 2\n";
    const Parsed parsed = parse_ok(kMixed);
    const std::string dumped = fsmtable::dump(parsed.machine, parsed.names);
    EXPECT_NE(dumped.find("kind tick = 1\n"), std::string::npos) << dumped;
    EXPECT_NE(dumped.find("transition A --tick--> B\n"), std::string::npos) << dumped;
    EXPECT_NE(dumped.find("transition A --3--> B when ge 2\n"), std::string::npos) << dumped;
}

TEST(NamedKinds, DumpParseDumpIsStableWithNames) {
    const Parsed once = parse_ok(kNamed);
    const std::string first = fsmtable::dump(once.machine, once.names);

    const Parsed twice = parse_ok(first);
    EXPECT_EQ(fsmtable::dump(twice.machine, twice.names), first);

    // The names survive too. They come back in the dumped order, so compare them as sets:
    // declaration order in, kind order out (which is what makes the dump canonical).
    const std::vector<KindName> before = sorted(once.names);
    const std::vector<KindName> after = sorted(twice.names);
    ASSERT_EQ(after.size(), before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(after[i].name, before[i].name);
        EXPECT_EQ(after[i].kind, before[i].kind);
    }
}

TEST(NamedKinds, RejectsAnUndeclaredName) {
    expect_kind_error("version 1\nmachine M\ninitial A\ntransition A --tick--> B\n", 4,
                      "unknown kind name");
}

TEST(NamedKinds, RejectsANameUsedBeforeItIsDeclared) {
    // The declaration has to come first: a single pass, so that the row that is wrong is the row
    // that is reported (QUESTIONS.md Q12).
    expect_kind_error("version 1\nmachine M\ninitial A\ntransition A --tick--> B\nkind tick = 1\n",
                      4, "unknown kind name");
}

TEST(NamedKinds, RejectsADuplicateKindName) {
    expect_kind_error("version 1\nmachine M\ninitial A\nkind tick = 1\nkind tick = 2\n", 5,
                      "duplicate kind name");
}

TEST(NamedKinds, RejectsTwoNamesForOneNumber) {
    // One name per number keeps `name -> number` and `number -> name` both answerable, which is
    // what the dump needs.
    expect_kind_error("version 1\nmachine M\ninitial A\nkind tick = 1\nkind tock = 1\n", 5,
                      "duplicate kind number");
}

TEST(NamedKinds, RejectsAMalformedKindDeclaration) {
    const std::vector<std::string_view> bad{
        "kind tick\n",          // no number
        "kind tick =\n",        // no number
        "kind tick 1\n",        // no '='
        "kind tick = one\n",    // not a number
        "kind tick = 256\n",    // out of range
        "kind tick = -1\n",     // out of range
        "kind tick = 0x1\n",    // no other spelling of a number
        "kind tick = 1 tick\n", // trailing field
        "kind 1 = 1\n",         // a name cannot be a number
    };
    for (const std::string_view line : bad) {
        const std::string text = "version 1\nmachine M\ninitial A\n" + std::string(line);
        expect_kind_error(text, 4, "kind");
    }
}

TEST(NamedKinds, KeepRuleFourForEverythingElse) {
    // Rule 4 is not relaxed: a name has to be a name, so the spaced arrow and a token that is
    // neither a number nor an identifier stay malformed.
    expect_kind_error("version 1\nmachine M\ninitial A\nkind tick = 1\ntransition A -- 1 --> B\n",
                      5, "malformed kind token");
    expect_kind_error("version 1\nmachine M\ninitial A\ntransition A --1x--> B\n", 4,
                      "malformed kind token");
    expect_kind_error("version 1\nmachine M\ninitial A\ntransition A --+1--> B\n", 4,
                      "malformed kind token");
}

TEST(NamedKinds, ADeclarationAndANumericRowCannotMakeAnAmbiguousRow) {
    // Rule 9 works on the resolved numbers, so a name and a number that meet in the same
    // from+kind are the same pair as far as the rule is concerned.
    expect_kind_error("version 1\nmachine M\ninitial A\nkind tick = 1\n"
                      "transition A --tick--> B\ntransition A --1--> C\n",
                      6, "ambiguous row");
}

} // namespace
