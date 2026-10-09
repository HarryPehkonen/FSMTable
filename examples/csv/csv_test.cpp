// The CSV reader's own tests: byte streams in, the lines the actions produced out. The machine is
// exercised through the driver, because the driver is what classifies a byte and the classification
// is half of what this example is about.
#include "csv.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using csv::Result;

Result run(const std::string& bytes) {
    csv::Reader reader;
    return reader.run(bytes);
}

// The lines a run that was in the machine's language produced, with the run's own verdict asserted
// first so a refusal shows up as the failure it is rather than as a short list.
std::vector<std::string> lines_of(const std::string& bytes) {
    const Result result = run(bytes);
    EXPECT_TRUE(result.ok) << result.error;
    return result.lines;
}

} // namespace

TEST(CsvReader, ReadsASimpleRecord) {
    EXPECT_EQ(lines_of("a,b,c\n"),
              (std::vector<std::string>{"Cell: a", "Cell: b", "Cell: c", "NEW LINE"}));
}

TEST(CsvReader, AnEmptyFieldIsAnEmptyCell) {
    EXPECT_EQ(lines_of("a,,\n"),
              (std::vector<std::string>{"Cell: a", "Cell: ", "Cell: ", "NEW LINE"}));
}

TEST(CsvReader, AQuotedFieldMayHoldAComma) {
    EXPECT_EQ(lines_of("\"a,b\",c\n"),
              (std::vector<std::string>{"Cell: a,b", "Cell: c", "NEW LINE"}));
}

TEST(CsvReader, AQuotedFieldMayHoldALineEnd) {
    // The cell really does contain a newline byte, so the trace line really does break: what the
    // machine collected is what is printed, warts and all.
    EXPECT_EQ(lines_of("\"a\nb\"\n"), (std::vector<std::string>{"Cell: a\nb", "NEW LINE"}));
}

TEST(CsvReader, TwoQuotesInAQuotedFieldAreOneQuote) {
    EXPECT_EQ(lines_of("\"x\"\"y\"\n"), (std::vector<std::string>{"Cell: x\"y", "NEW LINE"}));
}

TEST(CsvReader, AQuoteInsideAnUnquotedFieldIsAppended) {
    // RFC 4180 forbids this; this reader absorbs it rather than refusing (QUESTIONS.md Q22).
    EXPECT_EQ(lines_of("a\"b\n"), (std::vector<std::string>{"Cell: a\"b", "NEW LINE"}));
}

TEST(CsvReader, AnEmptyQuotedFieldIsOneEmptyCell) {
    EXPECT_EQ(lines_of("\"\"\n"), (std::vector<std::string>{"Cell: ", "NEW LINE"}));
}

TEST(CsvReader, ACrlfIsOneLineEnd) {
    EXPECT_EQ(lines_of("a,b\r\n"), (std::vector<std::string>{"Cell: a", "Cell: b", "NEW LINE"}));
}

TEST(CsvReader, ALoneCarriageReturnIsData) {
    // Only the pair CRLF is the driver's rule; a \r that no \n follows is an ordinary byte
    // (QUESTIONS.md Q21).
    EXPECT_EQ(lines_of("a\rb\n"), (std::vector<std::string>{"Cell: a\rb", "NEW LINE"}));
}

TEST(CsvReader, TheLastRecordNeedsNoTrailingLineEnd) {
    EXPECT_EQ(lines_of("a,b"), (std::vector<std::string>{"Cell: a", "Cell: b", "NEW LINE"}));
}

TEST(CsvReader, AQuotedFieldAtEndOfInputIsClosed) {
    // The closing quote arrived, so the field is whole even though no line end followed it.
    EXPECT_EQ(lines_of("\"abc\""), (std::vector<std::string>{"Cell: abc", "NEW LINE"}));
}

TEST(CsvReader, EmptyInputProducesNothing) {
    const Result result = run("");
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.lines.empty());
}

TEST(CsvReader, DataAfterAClosingQuoteIsRefused) {
    const Result result = run("\"ab\"x,bad\n");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.offset, 5U); // the `x`, the 5th byte: " a b " x
    EXPECT_EQ(result.error, "byte 5: QuoteSeen has no row for a data byte");
    EXPECT_TRUE(result.lines.empty()); // nothing was emitted for a record that never ended
}

TEST(CsvReader, EndOfInputInsideAQuotedFieldIsRefused) {
    const Result result = run("\"abc");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.offset, 0U); // not tied to a byte
    EXPECT_EQ(result.error, "end of input inside a quoted field");
    EXPECT_TRUE(result.lines.empty());
}

TEST(CsvReader, ARefusedByteLeavesTheMachineWhereItWas) {
    csv::Reader reader;
    const Result result = reader.run("\"ab\"x\n");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(reader.state_name(), "QuoteSeen"); // the byte that did not match changed nothing
}

TEST(CsvReader, TheMachineIsBackAtFieldStartAfterARecord) {
    csv::Reader reader;
    const Result result = reader.run("a\n");
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(reader.state_name(), "FieldStart");
}
