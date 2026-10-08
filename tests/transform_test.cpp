// The text -> text direction: `fsmtable::rename`, and the rename spelling a command line reads.
//
// This suite exists for the one property the addition has to have: the output must PARSE to the
// machine the input was with the requested names substituted, and it must be the input's own
// bytes everywhere else — every comment, every blank line, every untouched directive and the
// spacing inside an edited line. A rename that went to `dump` would pass the first half of that
// and fail the second, which is why the byte-level cases below are written against an expected
// TEXT rather than against a re-parse.
#include "fsmtable.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fsmtable::Error;
using fsmtable::KindName;
using fsmtable::Machine;
using fsmtable::Rename;
using fsmtable::Transition;

Rename spec(Rename::Target target, const char* from, const char* to) {
    return Rename{target, from, to};
}

// The fixture the byte-level cases work on: a comment header with an indented second line, blank
// lines, a named kind, a row that spells a kind as a NUMBER, a decorated state, and a row whose
// fields are separated by a tab and doubled spaces. Between them they are every site a rename may
// touch and every byte it must leave alone.
constexpr std::string_view kCommented = "# The comment header: a dump would drop all of this.\n"
                                        "#   * and its indented second line\n"
                                        "\n"
                                        "version 1\n"
                                        "machine Door\n"
                                        "initial Closed\n"
                                        "\n"
                                        "kind open = 1\n"
                                        "kind knock = 2\n"
                                        "\n"
                                        "state Closed entry on_entry_closed\n"
                                        "\n"
                                        "transition Closed\t--open-->   Open action on_opening\n"
                                        "transition Open --2--> Broken\n"
                                        "transition Broken --knock--> Closed\n";

// The same file with `Closed` called `Shut`. Written out by hand rather than derived from the
// input: a test that computed its expectation with the code under test would assert nothing.
constexpr std::string_view kShut = "# The comment header: a dump would drop all of this.\n"
                                   "#   * and its indented second line\n"
                                   "\n"
                                   "version 1\n"
                                   "machine Door\n"
                                   "initial Shut\n"
                                   "\n"
                                   "kind open = 1\n"
                                   "kind knock = 2\n"
                                   "\n"
                                   "state Shut entry on_entry_closed\n"
                                   "\n"
                                   "transition Shut\t--open-->   Open action on_opening\n"
                                   "transition Open --2--> Broken\n"
                                   "transition Broken --knock--> Shut\n";

// A machine whose state and kind share a spelling, and whose action names share it too — the
// case a rewrite done by replacing text wherever it appears would get wrong.
constexpr std::string_view kShared = "version 1\n"
                                     "machine Shared\n"
                                     "initial open\n"
                                     "kind open = 1\n"
                                     "state open entry open\n"
                                     "transition open --open--> Done action open\n";

// ---------------------------------------------------------------- helpers

// Renames and requires success. The unwrap is visible to the checker and to a reader
// (tests/parser_test.cpp says why the tests route through a helper like this).
std::string renamed_ok(std::string_view text, const std::vector<Rename>& renames) {
    Error error{0, ""};
    const auto out = fsmtable::rename(text, renames, error);
    if (!out.has_value()) {
        ADD_FAILURE() << "rename refused: line " << error.line << ": " << error.message;
        return std::string(text);
    }
    return *out;
}

// Renames and requires a refusal; returns what the caller has to assert about it.
struct Refusal {
    int line = -1;
    std::string message;
};

Refusal refused(std::string_view text, const std::vector<Rename>& renames) {
    Error error{0, ""};
    const auto out = fsmtable::rename(text, renames, error);
    if (out.has_value()) {
        ADD_FAILURE() << "rename wrote text where the case says it must refuse";
    }
    return Refusal{error.line, error.message};
}

Machine parse_ok(std::string_view text) {
    Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);
    if (!machine.has_value()) {
        ADD_FAILURE() << "the fixture does not parse, line " << error.line << ": " << error.message;
        return Machine{};
    }
    return *machine;
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

std::vector<KindName> kinds_of(std::string_view text) {
    Error error{0, ""};
    std::vector<KindName> names;
    const auto machine = fsmtable::parse(text, error, names);
    if (!machine.has_value()) {
        ADD_FAILURE() << "the fixture does not parse, line " << error.line << ": " << error.message;
    }
    return names;
}

// The lines of `text`, without their terminators. A trailing newline adds no line.
std::vector<std::string> lines_of(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t end = text.find('\n', begin);
        const std::size_t stop = end == std::string_view::npos ? text.size() : end;
        lines.emplace_back(text.substr(begin, stop - begin));
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return lines;
}

bool is_comment(const std::string& line) {
    const std::size_t first = line.find_first_not_of(" \t\r\v\f");
    return first == std::string::npos || line[first] == '#';
}

std::vector<std::filesystem::path> fsm_files(const char* directory) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".fsm") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// ---------------------------------------------------------------- the surgical edit

TEST(RenameTest, RenamesAStateEverywhereTheFileNamesIt) {
    const std::string out = renamed_ok(kCommented, {spec(Rename::Target::State, "Closed", "Shut")});
    EXPECT_EQ(out, kShut);
}

TEST(RenameTest, LeavesEveryCommentAndBlankLineByteForByte) {
    const std::string out = renamed_ok(kCommented, {spec(Rename::Target::State, "Closed", "Shut")});
    const std::vector<std::string> before = lines_of(kCommented);
    const std::vector<std::string> after = lines_of(out);
    ASSERT_EQ(before.size(), after.size()) << "the line count moved";
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (is_comment(before[i])) {
            EXPECT_EQ(after[i], before[i]) << "comment line " << (i + 1) << " was rewritten";
        }
    }
}

TEST(RenameTest, LeavesEveryLineTheRenameDoesNotReachByteForByte) {
    const std::string out = renamed_ok(kCommented, {spec(Rename::Target::State, "Closed", "Shut")});
    const std::vector<std::string> before = lines_of(kCommented);
    const std::vector<std::string> after = lines_of(out);
    ASSERT_EQ(before.size(), after.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (!is_comment(before[i]) && before[i].find("Closed") == std::string::npos) {
            EXPECT_EQ(after[i], before[i]) << "line " << (i + 1) << " was rewritten";
        }
    }
}

TEST(RenameTest, RenamesTheKindNameAndOnlyTheRowsThatSpellIt) {
    const std::string out = renamed_ok(kCommented, {spec(Rename::Target::Kind, "open", "unlock")});
    EXPECT_NE(out.find("kind unlock = 1\n"), std::string::npos);
    EXPECT_NE(out.find("transition Closed\t--unlock-->   Open action on_opening\n"),
              std::string::npos);
    // The second kind and the row that writes a number are untouched, byte for byte.
    EXPECT_NE(out.find("kind knock = 2\n"), std::string::npos);
    EXPECT_NE(out.find("transition Open --2--> Broken\n"), std::string::npos);
    EXPECT_EQ(out.find("--open-->"), std::string::npos);
}

TEST(RenameTest, LeavesANumericArrowAloneWhenTheKindNameMoves) {
    const std::string out = renamed_ok(kCommented, {spec(Rename::Target::Kind, "knock", "rap")});
    EXPECT_NE(out.find("transition Broken --rap--> Closed\n"), std::string::npos);
    // `Open --2--> Broken` spells the same kind as `--knock-->` did: the number is not a name,
    // so it stays a number.
    EXPECT_NE(out.find("transition Open --2--> Broken\n"), std::string::npos);
}

TEST(RenameTest, RenamesTheMachineNameAndNothingElse) {
    const std::string out = renamed_ok(kCommented, {spec(Rename::Target::Machine, "Door", "Gate")});
    EXPECT_NE(out.find("machine Gate\n"), std::string::npos);
    const std::vector<std::string> before = lines_of(kCommented);
    const std::vector<std::string> after = lines_of(out);
    ASSERT_EQ(before.size(), after.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (before[i] != "machine Door") {
            EXPECT_EQ(after[i], before[i]) << "line " << (i + 1) << " moved for a machine rename";
        }
    }
}

TEST(RenameTest, WritesTheTextBackUnchangedWhenThereIsNoRename) {
    EXPECT_EQ(renamed_ok(kCommented, {}), std::string(kCommented));
}

TEST(RenameTest, RenamesAStateToItsOwnNameWithoutTouchingTheText) {
    EXPECT_EQ(renamed_ok(kCommented, {spec(Rename::Target::State, "Closed", "Closed")}),
              std::string(kCommented));
}

TEST(RenameTest, SwapsTwoStatesAtOnce) {
    // A -> B and B -> A is a swap: applied once, simultaneously, with no intermediate name to
    // collide with. A chase would make both states the same name and then refuse.
    constexpr std::string_view kTwo = "version 1\n"
                                      "machine Turns\n"
                                      "initial Left\n"
                                      "transition Left --1--> Right\n"
                                      "transition Right --1--> Left\n";
    const std::string out = renamed_ok(kTwo, {spec(Rename::Target::State, "Left", "Right"),
                                              spec(Rename::Target::State, "Right", "Left")});
    EXPECT_EQ(out, "version 1\n"
                   "machine Turns\n"
                   "initial Right\n"
                   "transition Right --1--> Left\n"
                   "transition Left --1--> Right\n");
    EXPECT_EQ(render(parse_ok(out)), "machine Turns, initial Right, states: Right Left | "
                                     "Right --1--> Left | Left --1--> Right");
}

TEST(RenameTest, AStateRenameDoesNotTouchAKindWithTheSameSpelling) {
    const std::string out = renamed_ok(kShared, {spec(Rename::Target::State, "open", "held")});
    EXPECT_EQ(out, "version 1\n"
                   "machine Shared\n"
                   "initial held\n"
                   "kind open = 1\n"
                   "state held entry open\n"
                   "transition held --open--> Done action open\n");
}

TEST(RenameTest, AStateRenameDoesNotTouchAnActionWithTheSameSpelling) {
    const std::string out = renamed_ok(kShared, {spec(Rename::Target::State, "open", "held")});
    // `entry open` and `action open` name ACTIONS: an action is not a state, and this card does
    // not rename one. A rewrite that replaced the word wherever it appeared would break the file.
    EXPECT_NE(out.find("state held entry open\n"), std::string::npos);
    EXPECT_NE(out.find("action open\n"), std::string::npos);
}

TEST(RenameTest, RenamesAKindOntoAStateNameBecauseTheyAreSeparateNamespaces) {
    const std::string out = renamed_ok(kShared, {spec(Rename::Target::Kind, "open", "Done")});
    EXPECT_EQ(out, "version 1\n"
                   "machine Shared\n"
                   "initial open\n"
                   "kind Done = 1\n"
                   "state open entry open\n"
                   "transition open --Done--> Done action open\n");
    EXPECT_EQ(render(kinds_of(out)), "Done=1");
}

TEST(RenameTest, KeepsTheSpacingInsideTheLineItEdits) {
    const std::string out = renamed_ok(kCommented, {spec(Rename::Target::State, "Open", "Ajar")});
    EXPECT_NE(out.find("transition Closed\t--open-->   Ajar action on_opening\n"),
              std::string::npos);
}

TEST(RenameTest, TheOutputParsesToTheRenamedMachine) {
    const std::string out = renamed_ok(kCommented, {spec(Rename::Target::Machine, "Door", "Gate"),
                                                    spec(Rename::Target::State, "Closed", "Shut"),
                                                    spec(Rename::Target::Kind, "open", "unlock")});
    EXPECT_EQ(render(parse_ok(out)), "machine Gate, initial Shut, states: Shut Open Broken | "
                                     "Shut --1--> Open action on_opening | Open --2--> Broken | "
                                     "Broken --2--> Shut");
    EXPECT_EQ(render(kinds_of(out)), "unlock=1, knock=2");
}

TEST(RenameTest, EveryParseableFileInTheTreeSurvivesARename) {
    constexpr std::string_view kNewName = "ZzzRenamed";
    int checked = 0;
    std::vector<std::filesystem::path> files = fsm_files(FSMTABLE_CORPUS_DIR);
    for (const std::filesystem::path& extra : fsm_files(FSMTABLE_FIXTURES_DIR)) {
        files.push_back(extra);
    }
    for (const std::filesystem::path& path : files) {
        const std::string text = read_file(path);
        Error error{0, ""};
        const auto machine = fsmtable::parse(text, error);
        if (!machine.has_value()) {
            continue; // the corpus carries deliberately invalid files (SPEC.md section 7)
        }
        const std::string out = renamed_ok(
            text, {spec(Rename::Target::State, machine->initial.c_str(), "ZzzRenamed")});
        const Machine renamed = parse_ok(out);
        ASSERT_EQ(renamed.initial, kNewName) << path;
        ASSERT_EQ(renamed.states.size(), machine->states.size()) << path;
        // Two things are then compared for every file: the machine, with the initial state's
        // own name substituted, and every comment line, byte for byte.
        const std::vector<std::string> names = renamed.states;
        EXPECT_NE(std::find(names.begin(), names.end(), std::string(kNewName)), names.end())
            << path << ": the renamed state is not in the machine that came back";
        EXPECT_EQ(std::find(names.begin(), names.end(), machine->initial), names.end())
            << path << ": the old name is still a state";
        for (const Transition& row : renamed.transitions) {
            EXPECT_NE(row.from, machine->initial) << path << ": a row still names the old state";
            EXPECT_NE(row.to, machine->initial) << path << ": a row still names the old state";
        }
        const std::vector<std::string> before = lines_of(text);
        const std::vector<std::string> after = lines_of(out);
        ASSERT_EQ(before.size(), after.size()) << path;
        for (std::size_t i = 0; i < before.size(); ++i) {
            if (is_comment(before[i])) {
                EXPECT_EQ(after[i], before[i]) << path << " line " << (i + 1);
            }
        }
        ++checked;
    }
    // A glob that matched nothing would make this test pass vacuously. Today the walk finds
    // seven files that parse (three in the corpus, four fixtures).
    EXPECT_GE(checked, 7) << "the corpus and the fixtures should be walked, not skipped";
}

// ---------------------------------------------------------------- what is refused

TEST(RenameTest, RefusesAnUnknownState) {
    const Refusal refusal = refused(kCommented, {spec(Rename::Target::State, "Nope", "Shut")});
    EXPECT_EQ(refusal.line, 0);
    EXPECT_NE(refusal.message.find("Nope"), std::string::npos);
    EXPECT_NE(refusal.message.find("state"), std::string::npos);
}

TEST(RenameTest, RefusesAnUnknownKind) {
    const Refusal refusal = refused(kCommented, {spec(Rename::Target::Kind, "nope", "beat")});
    EXPECT_EQ(refusal.line, 0);
    EXPECT_NE(refusal.message.find("nope"), std::string::npos);
    EXPECT_NE(refusal.message.find("kind"), std::string::npos);
}

TEST(RenameTest, RefusesAMachineNameTheFileDoesNotHave) {
    const Refusal refusal = refused(kCommented, {spec(Rename::Target::Machine, "Other", "Gate")});
    EXPECT_EQ(refusal.line, 0);
    EXPECT_NE(refusal.message.find("Other"), std::string::npos);
    EXPECT_NE(refusal.message.find("Door"), std::string::npos);
}

TEST(RenameTest, RefusesACollisionWithAnExistingStateAndNamesBoth) {
    const Refusal refusal = refused(kCommented, {spec(Rename::Target::State, "Closed", "Open")});
    EXPECT_EQ(refusal.line, 0);
    EXPECT_NE(refusal.message.find("Closed"), std::string::npos);
    EXPECT_NE(refusal.message.find("Open"), std::string::npos);
}

TEST(RenameTest, RefusesACollisionWithAnExistingKindAndNamesBoth) {
    const Refusal refusal = refused(kCommented, {spec(Rename::Target::Kind, "open", "knock")});
    EXPECT_EQ(refusal.line, 0);
    EXPECT_NE(refusal.message.find("open"), std::string::npos);
    EXPECT_NE(refusal.message.find("knock"), std::string::npos);
}

TEST(RenameTest, RefusesTwoRenamesOntoOneName) {
    const Refusal refusal = refused(kCommented, {spec(Rename::Target::State, "Closed", "X"),
                                                 spec(Rename::Target::State, "Open", "X")});
    EXPECT_EQ(refusal.line, 0);
    EXPECT_NE(refusal.message.find("Closed"), std::string::npos);
    EXPECT_NE(refusal.message.find("Open"), std::string::npos);
}

TEST(RenameTest, RefusesTheSameNameTwice) {
    const Refusal refusal = refused(kCommented, {spec(Rename::Target::State, "Closed", "X"),
                                                 spec(Rename::Target::State, "Closed", "Y")});
    EXPECT_EQ(refusal.line, 0);
    EXPECT_NE(refusal.message.find("Closed"), std::string::npos);
}

TEST(RenameTest, RefusesARenameWhoseTargetNameIsNotAnIdentifier) {
    const Refusal refusal = refused(kCommented, {spec(Rename::Target::State, "Closed", "1Shut")});
    EXPECT_EQ(refusal.line, 0);
    EXPECT_FALSE(refusal.message.empty());
}

TEST(RenameTest, ATextThatDoesNotParseIsReportedByParse) {
    // The library's own reading: `rename` parses its text, so a text no machine comes out of is
    // reported with the parser's error rather than a rename error of its own. The tool refuses
    // such a file before it gets here, which is why the exit code is the command line's.
    constexpr std::string_view kJunk = "version 1\nmachine M\ninitial A\nbogus A --1--> B\n";
    const Refusal refusal = refused(kJunk, {spec(Rename::Target::State, "A", "B0")});
    EXPECT_EQ(refusal.line, 4);
    EXPECT_NE(refusal.message.find("bogus"), std::string::npos);
}

// ---------------------------------------------------------------- the spec spelling

TEST(RenameSpecTest, ReadsEveryTarget) {
    Rename rename;
    std::string message;
    ASSERT_TRUE(fsmtable::parse_rename_spec("machine:Door=Gate", rename, message)) << message;
    EXPECT_EQ(rename.target, Rename::Target::Machine);
    EXPECT_EQ(rename.from, "Door");
    EXPECT_EQ(rename.to, "Gate");
    ASSERT_TRUE(fsmtable::parse_rename_spec("state:Closed=Shut", rename, message)) << message;
    EXPECT_EQ(rename.target, Rename::Target::State);
    ASSERT_TRUE(fsmtable::parse_rename_spec("kind:open=unlock", rename, message)) << message;
    EXPECT_EQ(rename.target, Rename::Target::Kind);
}

TEST(RenameSpecTest, RefusesWhatIsNotARename) {
    const std::vector<std::string> bad = {
        "",
        "Closed",
        "state:Closed",
        "colour:Closed=Shut",
        "state:=Shut",
        "state:Closed=",
        "state:Closed=1",
        ":Closed=Shut",
        "state:Closed=Shut=Twice",
    };
    for (const std::string& candidate : bad) {
        Rename rename;
        std::string message;
        EXPECT_FALSE(fsmtable::parse_rename_spec(candidate, rename, message))
            << "'" << candidate << "' was read as a rename";
        EXPECT_FALSE(message.empty()) << "'" << candidate << "' was refused without a message";
    }
}

} // namespace
