// The protocol example: a script of directives in, the trace out, until end of input.
//
// Exit codes: 0 when every directive was answered by the machine, 1 when at least one was not —
// either because the machine has no row for it in that state (a protocol violation) or because the
// directive itself could not be read. Both are marked in the trace.
#include "protocol.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include <unistd.h> // isatty: a prompt only when a person is watching

namespace {

constexpr std::size_t kColumn = 22; // where the state column starts

std::string padded(const std::string& text) {
    std::string out = text;
    while (out.size() < kColumn)
        out.push_back(' ');
    return out;
}

std::string joined(const std::vector<std::string>& actions) {
    if (actions.empty())
        return "(nothing ran)";
    std::string out;
    for (const std::string& action : actions) {
        if (!out.empty())
            out += ", ";
        out += action;
    }
    return out;
}

// One trace line: what went in, where the machine was and went, and what ran on the way.
void report(const std::string& directive, const proto::Step& step) {
    std::cout << padded(directive) << step.from << " -> " << step.to << "   "
              << joined(step.actions) << "\n";
}

} // namespace

int main() {
    const bool interactive = isatty(fileno(stdin)) != 0;
    proto::Session session;
    std::string line;
    bool reported = false;

    if (interactive)
        std::cout << "directives: open, syn_ack, data <n>, fin, reset, tick <ms>, expire\n";

    while (true) {
        if (interactive)
            std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line))
            break;

        // Blank lines and `#` comments are the script's, not the machine's — and a comment may
        // follow a directive on the same line, which is what makes the script self-documenting.
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        const std::size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos)
            continue;
        std::string directive = line.substr(first);
        while (!directive.empty()
               && (directive.back() == ' ' || directive.back() == '\t' || directive.back() == '\r'))
            directive.pop_back();

        proto::Step step;
        std::string error;
        if (!session.feed(directive, step, error)) {
            std::cout << padded(directive) << "! " << error << "\n";
            reported = true;
            continue;
        }
        if (!step.matched) {
            std::cout << padded(directive) << "! " << step.from
                      << " is not a state this directive means anything in\n";
            reported = true;
            continue;
        }
        report(directive, step);
    }

    if (interactive)
        std::cout << "ended in " << session.state_name() << ", " << session.retransmits()
                  << " retransmit(s)\n";
    return reported ? 1 : 0;
}
