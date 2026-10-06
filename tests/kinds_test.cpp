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

// Parses and requires success. A failure is reported with the parser's own line and message,
// which is the part a reader needs, rather than as a bare "no value".
Machine parse_ok(std::string_view text) {
    Error error{0, ""};
    std::vector<KindName> names;
    auto machine = fsmtable::parse(text, error, names);
    if (!machine.has_value()) {
        ADD_FAILURE() << "parse failed at line " << error.line << ": " << error.message;
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
    Error error{0, ""};
    std::vector<KindName> names;
    const auto machine = fsmtable::parse(kNamed, error, names);
    ASSERT_TRUE(machine.has_value()) << error.line << ": " << error.message;

    // Declaration order — the file's order, like `states` — not the numeric order.
    ASSERT_EQ(names.size(), 2U);
    EXPECT_EQ(names[0].name, "warn");
    EXPECT_EQ(names[0].kind, 2);
    EXPECT_EQ(names[1].name, "advance");
    EXPECT_EQ(names[1].kind, 1);

    ASSERT_EQ(machine->transitions.size(), 3U);
    EXPECT_EQ(machine->transitions[0].kind, 1); // Red --advance-->
    EXPECT_EQ(machine->transitions[1].kind, 2); // Green --warn-->
    EXPECT_EQ(machine->transitions[2].kind, 1); // Yellow --advance-->
}

TEST(NamedKinds, ANameAndItsNumberAreTheSameMachine) {
    const Machine named = parse_ok(kNamed);
    const Machine numeric = parse_ok(kNumeric);
    EXPECT_EQ(fsmtable::dump(named), fsmtable::dump(numeric));

    Error error{0, ""};
    std::vector<KindName> names;
    const auto parsed = fsmtable::parse(kNumeric, error, names);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(names.empty()) << "a file that declares no names must report none";
}

TEST(NamedKinds, TheFrozenParseSeesTheSameMachine) {
    Error error{0, ""};
    const auto machine = fsmtable::parse(kNamed, error); // the two-argument form, unchanged
    ASSERT_TRUE(machine.has_value()) << error.line << ": " << error.message;
    EXPECT_EQ(fsmtable::dump(*machine), fsmtable::dump(parse_ok(kNamed)));
}

TEST(NamedKinds, TheFrozenDumpWritesNumbers) {
    const Machine machine = parse_ok(kNamed);
    const std::string dumped = fsmtable::dump(machine);
    EXPECT_EQ(dumped.find("kind "), std::string::npos) << dumped;
    EXPECT_NE(dumped.find("transition Red --1--> Green"), std::string::npos) << dumped;
}

TEST(NamedKinds, DumpWritesTheDeclarationsAndTheNames) {
    Error error{0, ""};
    std::vector<KindName> names;
    const auto machine = fsmtable::parse(kNamed, error, names);
    ASSERT_TRUE(machine.has_value());
    const std::string dumped = fsmtable::dump(*machine, names);

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
    Error error{0, ""};
    std::vector<KindName> names;
    const auto machine = fsmtable::parse(kMixed, error, names);
    ASSERT_TRUE(machine.has_value()) << error.line << ": " << error.message;
    const std::string dumped = fsmtable::dump(*machine, names);
    EXPECT_NE(dumped.find("kind tick = 1\n"), std::string::npos) << dumped;
    EXPECT_NE(dumped.find("transition A --tick--> B\n"), std::string::npos) << dumped;
    EXPECT_NE(dumped.find("transition A --3--> B when ge 2\n"), std::string::npos) << dumped;
}

TEST(NamedKinds, DumpParseDumpIsStableWithNames) {
    Error error{0, ""};
    std::vector<KindName> names;
    const auto once = fsmtable::parse(kNamed, error, names);
    ASSERT_TRUE(once.has_value());
    const std::string first = fsmtable::dump(*once, names);

    std::vector<KindName> again;
    const auto twice = fsmtable::parse(first, error, again);
    ASSERT_TRUE(twice.has_value()) << error.line << ": " << error.message;
    EXPECT_EQ(fsmtable::dump(*twice, again), first);

    // The names survive too. They come back in the dumped order, so compare them as sets:
    // declaration order in, kind order out (which is what makes the dump canonical).
    const std::vector<KindName> before = sorted(names);
    const std::vector<KindName> after = sorted(again);
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
