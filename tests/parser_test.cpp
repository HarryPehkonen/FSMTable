// FSMTable stage A tests.
//
// The test names and their meanings are frozen by SPEC.md section 5; the corpus rules
// come from section 7. Every error test asserts BOTH that the line number is right AND
// that no machine came back — a parser that reports an error and hands over a partial
// machine anyway would otherwise pass.
#include "fsmtable.hpp"

#include "version.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fsmtable::Error;
using fsmtable::Machine;
using fsmtable::Op;

// SPEC.md section 2, verbatim.
constexpr std::string_view kExample = "version 1\n"
                                      "machine TrafficLight\n"
                                      "initial Red\n"
                                      "transition Red --1--> Green action on_red_green\n"
                                      "transition Green --2--> Yellow\n"
                                      "transition Yellow --3--> Red when ge 30\n";

// Parses `text` and requires the error contract: no machine, this line, a message.
void expect_rejected(std::string_view text, int line, const char* what) {
    Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);
    EXPECT_FALSE(machine.has_value()) << what << ": a machine came back despite the error";
    EXPECT_EQ(error.line, line) << what << ": wrong line number";
    EXPECT_FALSE(error.message.empty()) << what << ": empty message";
}

// SPEC.md's parse returns an optional, and gtest's ASSERT_* does not teach clang-tidy's
// bugprone-unchecked-optional-access anything about it. This is the one place the tests
// unwrap one: the guard is visible to the checker and to a reader, and the failure path
// is loud instead of a dereference of an empty optional.
[[noreturn]] void no_machine(const std::string& what) {
    ADD_FAILURE() << "expected a machine: " << what;
    std::abort();
}

const Machine& parsed(const std::optional<Machine>& machine, const char* what) {
    if (!machine.has_value()) {
        no_machine(what);
    }
    return *machine;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

std::vector<std::filesystem::path> corpus_files() {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(FSMTABLE_CORPUS_DIR)) {
        if (entry.path().extension() == ".fsm") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// ---------------------------------------------------------------- parsing

TEST(FsmTableParser, ParsesTheExampleFromTheSpec) {
    Error error{0, ""};
    const auto machine = fsmtable::parse(kExample, error);
    ASSERT_TRUE(machine.has_value())
        << "the spec's own example must parse (line " << error.line << ": " << error.message << ")";
    const Machine& machine_ref = parsed(machine, "see the assertion above");

    EXPECT_EQ(machine_ref.name, "TrafficLight");
    EXPECT_EQ(machine_ref.initial, "Red");

    const std::vector<std::string> expected_states{"Red", "Green", "Yellow"};
    EXPECT_EQ(machine_ref.states, expected_states);

    ASSERT_EQ(machine_ref.transitions.size(), 3U);
    const auto& first = machine_ref.transitions[0];
    EXPECT_EQ(first.from, "Red");
    EXPECT_EQ(first.kind, 1);
    EXPECT_FALSE(first.has_when);
    EXPECT_EQ(first.to, "Green");
    EXPECT_EQ(first.action, "on_red_green");

    const auto& second = machine_ref.transitions[1];
    EXPECT_EQ(second.from, "Green");
    EXPECT_EQ(second.kind, 2);
    EXPECT_EQ(second.to, "Yellow");
    EXPECT_TRUE(second.action.empty());

    const auto& third = machine_ref.transitions[2];
    EXPECT_EQ(third.from, "Yellow");
    EXPECT_EQ(third.kind, 3);
    ASSERT_TRUE(third.has_when);
    EXPECT_EQ(third.when_op, Op::Ge);
    EXPECT_EQ(third.when_value, 30);
    EXPECT_EQ(third.to, "Red");
}

TEST(FsmTableParser, IgnoresCommentsAndBlankLines) {
    constexpr std::string_view text = "\n"
                                      "# a leading comment\n"
                                      "version 1\n"
                                      "\n"
                                      "   # an indented comment\n"
                                      "machine M\n"
                                      "\n"
                                      "initial A\n"
                                      "\n";
    Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);
    ASSERT_TRUE(machine.has_value()) << "line " << error.line << ": " << error.message;
    const Machine& machine_ref = parsed(machine, "see the assertion above");
    EXPECT_EQ(machine_ref.name, "M");
    EXPECT_EQ(machine_ref.initial, "A");
    EXPECT_TRUE(machine_ref.transitions.empty());
}

TEST(FsmTableParser, DeclaresStatesInFirstAppearanceOrder) {
    constexpr std::string_view text = "version 1\n"
                                      "machine M\n"
                                      "initial A\n"
                                      "transition A --1--> B\n"
                                      "transition B --2--> C\n"
                                      "transition D --3--> A\n";
    Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);
    ASSERT_TRUE(machine.has_value()) << "line " << error.line << ": " << error.message;
    const Machine& machine_ref = parsed(machine, "see the assertion above");
    const std::vector<std::string> expected{"A", "B", "C", "D"};
    EXPECT_EQ(machine_ref.states, expected);
}

TEST(FsmTableParser, ParsesWhenAndActionClauses) {
    constexpr std::string_view text = "version 1\n"
                                      "machine M\n"
                                      "initial A\n"
                                      "transition A --1--> B\n"
                                      "transition B --2--> C action go\n"
                                      "transition C --3--> D when lt 5\n"
                                      "transition D --4--> A when le -7 action stop\n";
    Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);
    ASSERT_TRUE(machine.has_value()) << "line " << error.line << ": " << error.message;
    const Machine& machine_ref = parsed(machine, "see the assertion above");
    ASSERT_EQ(machine_ref.transitions.size(), 4U);

    EXPECT_FALSE(machine_ref.transitions[0].has_when);
    EXPECT_TRUE(machine_ref.transitions[0].action.empty());

    EXPECT_FALSE(machine_ref.transitions[1].has_when);
    EXPECT_EQ(machine_ref.transitions[1].action, "go");

    ASSERT_TRUE(machine_ref.transitions[2].has_when);
    EXPECT_EQ(machine_ref.transitions[2].when_op, Op::Lt);
    EXPECT_EQ(machine_ref.transitions[2].when_value, 5);
    EXPECT_TRUE(machine_ref.transitions[2].action.empty());

    ASSERT_TRUE(machine_ref.transitions[3].has_when);
    EXPECT_EQ(machine_ref.transitions[3].when_op, Op::Le);
    EXPECT_EQ(machine_ref.transitions[3].when_value, -7);
    EXPECT_EQ(machine_ref.transitions[3].action, "stop");
}

TEST(FsmTableParser, AcceptsLeadingAndTrailingWhitespace) {
    constexpr std::string_view text
        = "   version 1   \n"
          "\tmachine M\t\n"
          "  initial   A  \n"
          "   transition   A   --1-->   B   when   eq   0   action   f  \n";
    Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);
    ASSERT_TRUE(machine.has_value()) << "line " << error.line << ": " << error.message;
    const Machine& machine_ref = parsed(machine, "see the assertion above");
    ASSERT_EQ(machine_ref.transitions.size(), 1U);
    EXPECT_EQ(machine_ref.transitions[0].from, "A");
    EXPECT_EQ(machine_ref.transitions[0].to, "B");
    EXPECT_EQ(machine_ref.transitions[0].action, "f");
    ASSERT_TRUE(machine_ref.transitions[0].has_when);
    EXPECT_EQ(machine_ref.transitions[0].when_op, Op::Eq);
    EXPECT_EQ(machine_ref.transitions[0].when_value, 0);
}

// ---------------------------------------------------------------- errors
//
// Line-number convention for a MISSING required directive (nothing to point at): 0,
// which is what Error::line documents itself to mean. See QUESTIONS.md Q1.

TEST(FsmTableParser, RejectsMissingVersion) {
    expect_rejected("machine M\ninitial A\n", 0, "version absent");
}

TEST(FsmTableParser, RejectsWrongVersionNumber) {
    expect_rejected("version 2\nmachine M\ninitial A\n", 1, "version 2");
}

// Beyond the frozen list: rule 1 also requires version 1 to be the first
// non-comment line, which is an error the frozen names do not reach.
TEST(FsmTableParser, RejectsVersionThatIsNotFirst) {
    expect_rejected("machine M\nversion 1\ninitial A\n", 2, "version not first");
}

TEST(FsmTableParser, RejectsDuplicateMachine) {
    expect_rejected("version 1\nmachine M\nmachine N\ninitial A\n", 3, "second machine");
}

TEST(FsmTableParser, RejectsMissingInitial) {
    expect_rejected("version 1\nmachine M\ntransition A --1--> B\n", 0, "initial absent");
}

TEST(FsmTableParser, RejectsUnknownDirective) {
    expect_rejected("version 1\nmachine M\ninitial A\nfrobnicate 3\n", 4, "unknown directive");
}

TEST(FsmTableParser, RejectsTrailingJunk) {
    expect_rejected("version 1\nmachine M\ninitial A\ntransition A --1--> B bogus\n", 4, "junk");
}

TEST(FsmTableParser, RejectsBadKindToken) {
    expect_rejected("version 1\nmachine M\ninitial A\ntransition A -- 1 --> B\n", 4,
                    "spaced arrow");
}

TEST(FsmTableParser, RejectsKindOutOfRange) {
    expect_rejected("version 1\nmachine M\ninitial A\ntransition A --256--> B\n", 4, "kind 256");
}

TEST(FsmTableParser, RejectsBadOp) {
    expect_rejected("version 1\nmachine M\ninitial A\ntransition A --1--> B when equals 3\n", 4,
                    "op equals");
}

TEST(FsmTableParser, RejectsWhenValueOutOfRange) {
    expect_rejected("version 1\nmachine M\ninitial A\ntransition A --1--> B when gt 2147483648\n",
                    4, "one past INT_MAX");
}

TEST(FsmTableParser, RejectsOverlongName) {
    const std::string long_name(65, 'x');
    const std::string text
        = "version 1\nmachine M\ninitial A\ntransition A --1--> " + long_name + "\n";
    expect_rejected(text, 4, "65-character state name");
}

TEST(FsmTableParser, RejectsAmbiguousRow) {
    expect_rejected(
        "version 1\nmachine M\ninitial A\ntransition A --1--> B\ntransition A --1--> C\n", 5,
        "two unguarded rows for one from+kind");
}

TEST(FsmTableParser, ReportsTheRightLineNumber) {
    constexpr std::string_view text = "version 1\n"
                                      "machine M\n"
                                      "initial A\n"
                                      "transition A --1--> B\n"
                                      "transition B --2--> C\n"
                                      "transition C --3--> A\n"
                                      "transition A --9--> B when xx 3\n";
    Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);
    EXPECT_FALSE(machine.has_value()) << "a machine came back despite the error";
    EXPECT_EQ(error.line, 7) << "the malformed line is line 7";
}

// ---------------------------------------------------------------- dumping

TEST(FsmTableParser, DumpIsCanonical) {
    constexpr std::string_view text = "# a comment, which the dump must not carry over\n"
                                      "version 1\n"
                                      "machine M\n"
                                      "initial B\n"
                                      "\n"
                                      "transition B --7--> A action z\n"
                                      "transition A --2--> D when gt 1\n"
                                      "transition A --2--> C\n"
                                      "transition A --5--> C when eq 1\n";
    Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);
    ASSERT_TRUE(machine.has_value()) << "line " << error.line << ": " << error.message;
    const Machine& machine_ref = parsed(machine, "see the assertion above");

    const std::string expected = "version 1\n"
                                 "machine M\n"
                                 "initial B\n"
                                 "transition A --2--> D when gt 1\n"
                                 "transition A --2--> C\n"
                                 "transition A --5--> C when eq 1\n"
                                 "transition B --7--> A action z\n";
    EXPECT_EQ(fsmtable::dump(machine_ref), expected);
}

TEST(FsmTableParser, DumpsEveryField) {
    constexpr std::string_view text = "version 1\n"
                                      "machine Everything\n"
                                      "initial S0\n"
                                      "transition S0 --255--> S1 when le -2147483648 action act_1\n"
                                      "transition S1 --0--> S0\n";
    Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);
    ASSERT_TRUE(machine.has_value()) << "line " << error.line << ": " << error.message;
    const Machine& machine_ref = parsed(machine, "see the assertion above");

    const std::string once = fsmtable::dump(machine_ref);
    Error again_error{0, ""};
    const auto reparsed = fsmtable::parse(once, again_error);
    ASSERT_TRUE(reparsed.has_value()) << "the dump must parse again (line " << again_error.line
                                      << ": " << again_error.message << ")";
    const Machine& reparsed_ref = parsed(reparsed, "see the assertion above");

    EXPECT_EQ(reparsed_ref.name, machine_ref.name);
    EXPECT_EQ(reparsed_ref.initial, machine_ref.initial);
    EXPECT_EQ(reparsed_ref.states, machine_ref.states);
    ASSERT_EQ(reparsed_ref.transitions.size(), machine_ref.transitions.size());
    for (std::size_t i = 0; i < reparsed_ref.transitions.size(); ++i) {
        const auto& a = machine_ref.transitions[i];
        const auto& b = reparsed_ref.transitions[i];
        EXPECT_EQ(a.from, b.from) << "row " << i;
        EXPECT_EQ(a.kind, b.kind) << "row " << i;
        EXPECT_EQ(a.has_when, b.has_when) << "row " << i;
        if (a.has_when) {
            EXPECT_EQ(a.when_op, b.when_op) << "row " << i;
            EXPECT_EQ(a.when_value, b.when_value) << "row " << i;
        }
        EXPECT_EQ(a.to, b.to) << "row " << i;
        EXPECT_EQ(a.action, b.action) << "row " << i;
    }
}

TEST(FsmTableParser, DumpParseDumpIsStable) {
    // The oracle: dump(parse(dump(parse(t)))) == dump(parse(t)).
    const std::vector<std::string> texts{std::string(kExample), "version 1\nmachine M\ninitial A\n",
                                         "version 1\nmachine M\ninitial A\n"
                                         "transition A --1--> B when eq 0 action f\n"};
    for (std::size_t i = 0; i < texts.size(); ++i) {
        Error first_error{0, ""};
        const auto first = fsmtable::parse(texts[i], first_error);
        ASSERT_TRUE(first.has_value()) << "text " << i;
        const Machine& first_ref = parsed(first, "see the assertion above");
        const std::string once = fsmtable::dump(first_ref);

        Error second_error{0, ""};
        const auto second = fsmtable::parse(once, second_error);
        ASSERT_TRUE(second.has_value()) << "text " << i << " (dump failed to re-parse)";
        const Machine& second_ref = parsed(second, "see the assertion above");
        EXPECT_EQ(fsmtable::dump(second_ref), once) << "text " << i;
    }
}

// ---------------------------------------------------------------- the corpus

TEST(FsmTableCorpus, EveryCorpusFileObeysTheFrozenRules) {
    // SPEC.md section 7: every invalid_*.fsm must fail, and its line number is recorded
    // here; every other .fsm must parse and must satisfy the dump/parse/dump oracle.
    const std::vector<std::pair<std::string, int>> expected_invalid{
        {"invalid_ambiguous.fsm", 5},
        {"invalid_junk.fsm", 4},
    };

    const auto files = corpus_files();
    ASSERT_FALSE(files.empty()) << "no corpus files found in " << FSMTABLE_CORPUS_DIR;

    for (const auto& file : files) {
        const std::string name = file.filename().string();
        const std::string text = read_file(file);

        const bool is_invalid = name.rfind("invalid_", 0) == 0;
        Error error{0, ""};
        const auto machine = fsmtable::parse(text, error);

        if (is_invalid) {
            auto it = std::find_if(expected_invalid.begin(), expected_invalid.end(),
                                   [&](const auto& entry) { return entry.first == name; });
            ASSERT_NE(it, expected_invalid.end())
                << name << " is an invalid_ file with no expected line number in the table";
            EXPECT_FALSE(machine.has_value()) << name << " must not parse";
            EXPECT_EQ(error.line, it->second) << name << ": wrong line number";
        } else {
            ASSERT_TRUE(machine.has_value())
                << name << " must parse (line " << error.line << ": " << error.message << ")";
            const Machine& machine_ref = parsed(machine, "see the assertion above");
            const std::string once = fsmtable::dump(machine_ref);
            Error again_error{0, ""};
            const auto again = fsmtable::parse(once, again_error);
            ASSERT_TRUE(again.has_value()) << name << ": its dump must parse again";
            const Machine& again_ref = parsed(again, "see the assertion above");
            EXPECT_EQ(fsmtable::dump(again_ref), once) << name << ": dump/parse/dump is not stable";
        }
    }
}

// Beyond the frozen list: the kit's version template asks for this test, and it is what the
// gate's `version` stage is protecting. Both copies of the number come from project(VERSION),
// so a drift between the numbers and the string is a build-system bug, not a runtime one.
TEST(FsmTableVersion, NumbersAgreeWithTheString) {
    const std::string from_numbers = std::to_string(fsmtable::version_major) + "."
                                     + std::to_string(fsmtable::version_minor) + "."
                                     + std::to_string(fsmtable::version_patch);
    EXPECT_EQ(from_numbers, std::string(fsmtable::version()));
}

} // namespace
