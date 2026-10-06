// The calculator: one line in, one answer out, until end of input or Ctrl-C.
//
// Everything that decides anything lives in calc::Shell (calculator.cpp), which never touches a
// stream. This file is the imperative shell around it: read a line, hand it over, print what
// came back.
#include "calculator.hpp"

#include <cstdio>
#include <iostream>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace {

// A prompt only for a person at a keyboard: piping a file in should produce exactly the answers
// and nothing else, which is what the end-to-end test compares.
bool interactive() {
#if defined(__unix__) || defined(__APPLE__)
    return isatty(fileno(stdin)) != 0;
#else
    return false;
#endif
}

} // namespace

int main() {
    calc::Shell shell;
    const bool show_prompt = interactive();

    std::string line;
    while (true) {
        if (show_prompt) {
            std::cout << "> " << std::flush;
        }
        if (!std::getline(std::cin, line)) {
            break; // end of input; Ctrl-C ends it too, the default way
        }
        const calc::Outcome outcome = shell.run_line(line);
        switch (outcome.kind) {
        case calc::Outcome::Kind::None:
            break;
        case calc::Outcome::Kind::Value:
            std::cout << outcome.value << '\n';
            break;
        case calc::Outcome::Kind::Error:
            std::cout << "error: " << outcome.message << '\n';
            break;
        }
    }
    return 0;
}
