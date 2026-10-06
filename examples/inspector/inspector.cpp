// fsmtable-inspect — read a .fsm, report what its own text cannot show, and optionally write the
// canonical form back out.
//
// This is the example that uses the LIBRARY without the generator: nothing is generated here, and
// no machine is built at compile time. It is the smallest useful tool you can build on this format
// — a formatter and a checker for .fsm files, in about a hundred and fifty lines — and it is the
// only place in this repository where the analyses are used by something other than their own
// tests. Two of them are SPEC.md section 8's; the third is the addition beside them.
//
// Exit codes, the same convention as fsmtable-gen: 0 nothing to report, 1 the file could not be
// read or a state is unreachable, 2 the command line is wrong. A SINK state and a partially
// covered pair are reported and do not fail the run: ending somewhere is usually deliberate, and
// so is gating a row that has nothing behind it.
#include "fsmtable.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void usage(std::ostream& out) {
    out << "fsmtable-inspect — report what a .fsm's own text cannot show\n"
        << "\n"
        << "  fsmtable-inspect <file.fsm>              summarize it, and report findings\n"
        << "  fsmtable-inspect --canonical <file.fsm>  write the canonical form to stdout\n"
        << "  fsmtable-inspect --help\n"
        << "\n"
        << "Exit codes: 0 nothing to report, 1 the file could not be read or a state is\n"
        << "unreachable, 2 the command line is wrong.\n";
}

bool read_file(const std::string& path, std::string& text) {
    std::ifstream in(path);
    if (!in.good())
        return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return true;
}

// A kind the way the generator names one: the declared name when the file declared it, the
// number otherwise, so the report reads the same way the machine's own text does.
std::string kind_label(int kind, const std::vector<fsmtable::KindName>& names) {
    for (const fsmtable::KindName& entry : names) {
        if (entry.kind == kind)
            return entry.name;
    }
    return std::to_string(kind);
}

std::string joined_pairs(const std::vector<fsmtable::PartialPair>& pairs,
                         const std::vector<fsmtable::KindName>& names) {
    std::string out;
    for (const fsmtable::PartialPair& pair : pairs) {
        if (!out.empty())
            out += ", ";
        out += pair.from + " --" + kind_label(pair.kind, names) + "-->";
    }
    return out;
}

std::string joined(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        if (!out.empty())
            out += ", ";
        out += name;
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    bool canonical = false;
    std::string path;
    for (int i = 1; i < argc; ++i) {
        // A command line arrives as (int argc, char** argv), and reading argv[i] at all is pointer
        // arithmetic by this check's definition — the same reading .ci/tidy-baseline.txt records
        // for gen/fsmtable-gen.cpp. No argument-reading main avoids it, so the honest place to say
        // so is here rather than behind a cast that would hide the array.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const std::string arg = argv[i];
        if (arg == "--canonical") {
            canonical = true;
        } else if (arg == "--help" || arg == "-h") {
            usage(std::cout);
            return 0;
        } else if (!arg.empty() && arg.front() == '-') {
            std::cerr << "fsmtable-inspect: unknown option '" << arg << "'\n";
            usage(std::cerr);
            return 2;
        } else if (path.empty()) {
            path = arg;
        } else {
            std::cerr << "fsmtable-inspect: at most one input file\n";
            usage(std::cerr);
            return 2;
        }
    }
    if (path.empty()) {
        usage(std::cerr);
        return 2;
    }

    std::string text;
    if (!read_file(path, text)) {
        std::cerr << "fsmtable-inspect: cannot read " << path << "\n";
        return 1;
    }

    // The four-argument reading, so the names the file declared and its entry/exit clauses survive
    // into the canonical form (NAMED_KINDS.md, ENTRY_EXIT.md).
    fsmtable::Error error{0, ""};
    std::vector<fsmtable::KindName> kind_names;
    std::vector<fsmtable::StateAction> state_actions;
    const auto parsed = fsmtable::parse(text, error, kind_names, state_actions);
    if (!parsed.has_value()) {
        std::cerr << path << ":" << error.line << ": " << error.message << "\n";
        return 1;
    }
    const fsmtable::Machine& machine = *parsed;

    if (canonical) {
        std::cout << fsmtable::dump(machine, kind_names, state_actions);
        return 0;
    }

    const std::vector<std::string> unreachable = fsmtable::unreachable(machine);
    const std::vector<std::string> sinks = fsmtable::sink_states(machine);
    const std::vector<fsmtable::PartialPair> partial = fsmtable::partial_pairs(machine);

    std::cout << path << ": " << machine.name << " — " << machine.states.size() << " state(s), "
              << machine.transitions.size() << " row(s), " << kind_names.size()
              << " named kind(s)\n";
    std::cout << "  initial:  " << machine.initial << "\n";
    if (!sinks.empty()) {
        std::cout << "  sinks:    " << joined(sinks)
                  << "  (nothing leaves them; often deliberate)\n";
    }
    if (!partial.empty()) {
        std::cout << "  partial:  " << joined_pairs(partial, kind_names)
                  << "  (a guarded row with no unguarded row: a false guard leaves the event)\n";
    }
    if (!unreachable.empty()) {
        std::cout << "  UNREACHABLE: " << joined(unreachable) << "  (no path from "
                  << machine.initial << ")\n";
        return 1;
    }
    std::cout << "  every state is reachable from " << machine.initial << "\n";
    return 0;
}
