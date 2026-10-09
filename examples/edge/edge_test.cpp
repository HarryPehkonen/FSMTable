// The Edge detector's own tests: a bit stream in, the lines the actions printed out. The machine is
// exercised through the driver, because the driver is what classifies a byte and the classification
// is the one thing this example's table does not hold.
#include "edge.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using edge::Result;

Result run(const std::string& bytes) {
    edge::Reader reader;
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

TEST(EdgeDetector, ARisingEdgeIsReported) {
    EXPECT_EQ(lines_of("01"), (std::vector<std::string>{"0 --> 1"}));
}

TEST(EdgeDetector, AFallingEdgeIsReported) {
    EXPECT_EQ(lines_of("10"), (std::vector<std::string>{"1 --> 0"}));
}

TEST(EdgeDetector, ARepeatedBitIsSilentlyIgnored) {
    EXPECT_TRUE(lines_of("00").empty());
    EXPECT_TRUE(lines_of("11").empty());
}

TEST(EdgeDetector, TheFirstBitIsAbsorbedWithoutReporting) {
    // `Start` is the state that makes this true: nothing -> 0 is not a change, so the first bit
    // moves and reports nothing (QUESTIONS.md Q26).
    EXPECT_TRUE(lines_of("0").empty());
    EXPECT_TRUE(lines_of("1").empty());
}

TEST(EdgeDetector, TheSameZeroKindMeansThreeDifferentThings) {
    // The observation edge.fsm exists to make. One kind, three arrivals, and the state is the whole
    // difference: absorbed in `Start`, absorbed in `SawZero`, reporting in `SawOne`.
    EXPECT_TRUE(lines_of("0").empty());
    EXPECT_TRUE(lines_of("00").empty());
    EXPECT_EQ(lines_of("10"), (std::vector<std::string>{"1 --> 0"}));
}

TEST(EdgeDetector, EmptyInputProducesNothing) {
    const Result result = run("");
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.lines.empty());
}

TEST(EdgeDetector, TheStateIsTheLastBitSeen) {
    edge::Reader untouched;
    EXPECT_EQ(untouched.state_name(),
              "Start"); // nothing has arrived, so there is nothing to remember

    edge::Reader after_one_bit;
    after_one_bit.run("0");
    EXPECT_EQ(after_one_bit.state_name(), "SawZero");

    edge::Reader after_three_bits;
    after_three_bits.run("001");
    EXPECT_EQ(after_three_bits.state_name(), "SawOne");
}

TEST(EdgeDetector, TheWholeStreamIsTheEdgesInOrder) {
    EXPECT_EQ(lines_of("0010110110"), (std::vector<std::string>{"0 --> 1", "1 --> 0", "0 --> 1",
                                                                "1 --> 0", "0 --> 1", "1 --> 0"}));
}

TEST(EdgeDetector, AByteThatIsNeitherBitIsRefused) {
    const Result result = run("01x");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.offset, 3U); // the `x`, the third byte: 0 1 x
    EXPECT_EQ(result.error, "byte 3: 'x' is not a bit");
    // The edge before the byte is kept — it really happened — and nothing after it is invented.
    EXPECT_EQ(result.lines, (std::vector<std::string>{"0 --> 1"}));
}

TEST(EdgeDetector, ARefusedByteLeavesTheMachineWhereItWas) {
    edge::Reader reader;
    const Result result = reader.run("01x");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(reader.state_name(), "SawOne"); // a byte with no kind changed nothing
}

TEST(EdgeDetector, ANewlineIsAByteLikeAnyOther) {
    // `echo 01 | ./build/edge` is refused, and the message names the byte rather than printing a
    // blank: the input is a bit stream, not a text file (QUESTIONS.md Q27).
    const Result result = run("01\n");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.offset, 3U);
    EXPECT_EQ(result.error, "byte 3: 0x0a is not a bit");
}
