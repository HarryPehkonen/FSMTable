// The calculator's tests: lines in, outcomes out, through the same Shell the REPL uses. No
// pipe, no terminal, no argv — everything the machine and the registers decide is reachable
// from here, which is the reason the example is split this way.
#include "calculator.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

// One Shell per test, fed line by line the way a session is: the Clear row is what makes the
// next line unable to see the last one.
long long value_of(calc::Shell& shell, std::string_view line) {
    const calc::Outcome outcome = shell.run_line(line);
    EXPECT_EQ(outcome.kind, calc::Outcome::Kind::Value) << "line: " << line;
    return outcome.value;
}

std::string error_of(calc::Shell& shell, std::string_view line) {
    const calc::Outcome outcome = shell.run_line(line);
    EXPECT_EQ(outcome.kind, calc::Outcome::Kind::Error) << "line: " << line;
    return outcome.message;
}

} // namespace

TEST(Calculator, A_number_alone_is_its_own_answer) {
    calc::Shell shell;
    EXPECT_EQ(value_of(shell, "12"), 12);
}

TEST(Calculator, Blank_lines_say_nothing) {
    calc::Shell shell;
    EXPECT_EQ(shell.run_line("").kind, calc::Outcome::Kind::None);
    EXPECT_EQ(shell.run_line("   ").kind, calc::Outcome::Kind::None);
    EXPECT_EQ(shell.run_line("\t").kind, calc::Outcome::Kind::None);
}

TEST(Calculator, Evaluation_is_left_to_right_there_is_no_precedence) {
    // The machine commits each operand as it arrives, which is what a desk calculator does.
    // These are the cases where that differs from precedence, so if someone ever adds
    // precedence these are the tests that say so: (10 - 2) * 3 is 24, not 10 - 6.
    calc::Shell shell;
    EXPECT_EQ(value_of(shell, "10 - 2 * 3"), 24);
    EXPECT_EQ(value_of(shell, "8 / 4 / 2"), 1);
}

TEST(Calculator, Chained_operators_commit_the_previous_operand) {
    calc::Shell shell;
    EXPECT_EQ(value_of(shell, "1 + 2 + 3"), 6);
    EXPECT_EQ(value_of(shell, "20 - 5 - 3"), 12);
}

TEST(Calculator, All_four_operators_work) {
    calc::Shell shell;
    EXPECT_EQ(value_of(shell, "7 + 3"), 10);
    EXPECT_EQ(value_of(shell, "7 - 3"), 4);
    EXPECT_EQ(value_of(shell, "7 * 3"), 21);
    EXPECT_EQ(value_of(shell, "7 / 3"), 2);
}

TEST(Calculator, Division_is_integer_division) {
    calc::Shell shell;
    EXPECT_EQ(value_of(shell, "9 / 4"), 2);
}

TEST(Calculator, Spacing_does_not_matter) {
    calc::Shell shell;
    EXPECT_EQ(value_of(shell, "12+3*4"), 60); // (12 + 3) * 4
    EXPECT_EQ(value_of(shell, "   12   +   3   "), 15);
}

TEST(Calculator, The_zero_divisor_reaches_the_error_state) {
    // This is the property the whole canonical-order rule exists for, in a machine that has a
    // real use for it: `PendingDiv --1--> Accum` and the guarded `--> Error` are two rows for
    // the same from+kind, and the guarded one must be the one that is tried first.
    calc::Shell shell;
    EXPECT_EQ(error_of(shell, "7 / 0"), "division by zero");
    EXPECT_EQ(shell.state_name(), "Error");
}

TEST(Calculator, A_failed_line_keeps_its_reason_however_it_continues) {
    // The Error state swallows the rest of the line on purpose: a token refused there would be
    // reported as "unexpected operator +", burying the division that actually went wrong.
    calc::Shell shell;
    EXPECT_EQ(error_of(shell, "7 / 0 + 3 * 2"), "division by zero");
}

TEST(Calculator, An_operator_without_a_left_operand_is_refused) {
    calc::Shell shell;
    EXPECT_EQ(error_of(shell, "+ 3"), "unexpected operator +");
}

TEST(Calculator, A_dangling_operator_is_refused) {
    // Equals has no row in a Pending* state, so `12 +` is simply not in this machine's language.
    calc::Shell shell;
    EXPECT_EQ(error_of(shell, "12 +"), "incomplete expression");
}

TEST(Calculator, Two_operands_in_a_row_are_refused) {
    calc::Shell shell;
    EXPECT_EQ(error_of(shell, "12 3"), "unexpected number 3");
}

TEST(Calculator, An_unknown_character_is_refused_with_its_column) {
    calc::Shell shell;
    EXPECT_EQ(error_of(shell, "12 & 3"), "unexpected character '&' at column 4");
    EXPECT_EQ(error_of(shell, "1 + 2 ="), "unexpected character '=' at column 7");
}

TEST(Calculator, A_number_beyond_the_event_slot_is_refused) {
    // The operand travels in the event's int (calculator.fsm's kind table), so the range is the
    // format's, and the message says which limit was hit rather than silently truncating.
    calc::Shell shell;
    EXPECT_EQ(error_of(shell, "99999999999"), "number out of range");
    EXPECT_EQ(value_of(shell, "2147483647"), 2147483647);
}

TEST(Calculator, Overflow_is_the_registers_business_not_a_row) {
    // Division by zero is a row because the divisor is the event's value and the pending
    // operator is a state. The product is neither, so this one cannot be a row.
    calc::Shell shell;
    EXPECT_EQ(error_of(shell, "2147483647 * 2147483647 * 2147483647"), "overflow");
}

TEST(Calculator, Lines_are_independent_even_after_a_failure) {
    calc::Shell shell;
    EXPECT_EQ(value_of(shell, "1 + 1"), 2);
    EXPECT_EQ(value_of(shell, "2 + 2"), 4); // the previous answer is not carried in
    EXPECT_EQ(error_of(shell, "12 +"), "incomplete expression");
    EXPECT_EQ(shell.state_name(), "PendingAdd"); // where the refused line stopped: the
                                                 // pending operator IS the state, so it shows
    EXPECT_EQ(value_of(shell, "3 + 4"), 7);      // and it does not poison the next line
    EXPECT_EQ(shell.state_name(), "Accum");
}