// The Edge example: a stream of `0` and `1` bytes in, one line per reported edge out, until end of
// input.
//
// Exit codes: 0 when the whole stream was bits, 1 when a byte was not — the driver's refusal, since
// the machine's alphabet is its two kinds and a third byte reaches no row. It is reported on the
// last line, marked `!`, the way the protocol and csv examples mark a byte their driver refused.
#include "edge.hpp"

#include <iostream>
#include <iterator>
#include <string>

int main() {
    // The whole stream, read once. The driver's rule is about the stream as a whole — every byte in
    // it has to be a bit — so there is nothing useful to do byte by byte beyond what Reader::run
    // already does, and a reader that reads to end of input needs no prompt and no terminal check.
    const std::string input{std::istreambuf_iterator<char>(std::cin),
                            std::istreambuf_iterator<char>()};

    edge::Reader reader;
    const edge::Result result = reader.run(input);

    for (const std::string& line : result.lines)
        std::cout << line << '\n';
    if (!result.ok) {
        std::cout << "! " << result.error << '\n';
        return 1;
    }
    return 0;
}
