// The CSV example: a byte stream in, one line per cell and one per record out, until end of input.
//
// Exit codes: 0 when the whole stream was in the machine's language, 1 when a byte was not — a byte
// after a closing quote, or end of input inside a quoted field. Both are reported on the last line,
// marked `!`, the way the protocol example marks a directive the machine refused.
#include "csv.hpp"

#include <iostream>
#include <iterator>
#include <string>

int main() {
    // The whole stream, read once. The driver's rules are about the stream as a whole — CRLF is one
    // line end, and a record may be left open at the end — so there is nothing useful to do line by
    // line, and a reader that reads to end of input needs no prompt and no terminal check.
    const std::string input{std::istreambuf_iterator<char>(std::cin),
                            std::istreambuf_iterator<char>()};

    csv::Reader reader;
    const csv::Result result = reader.run(input);

    for (const std::string& line : result.lines)
        std::cout << line << '\n';
    if (!result.ok) {
        std::cout << "! " << result.error << '\n';
        return 1;
    }
    return 0;
}
