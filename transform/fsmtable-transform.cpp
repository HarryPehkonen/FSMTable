// fsmtable-transform — rewrite a .fsm's names, text to text.
//
// The tool half of `fsmtable::rename` (src/transform.cpp): it reads one .fsm, applies the renames
// a command line gives it, and writes the machine back out with every comment, blank line and
// untouched directive byte for byte where the file wrote it. Nothing but the names the caller
// asked for moves — which is why this is not `fsmtable-inspect --canonical`: dumping the
// canonical form is exactly what you want when you are normalising a file, and exactly what
// drops the comments that carry the reasoning (TRANSFORM.md).
//
// The command line is read first, then the file, then the renames, and the three refusals have
// three exit codes: 0 the renamed machine was written, 1 the rename could not be made, 2 the
// command line is wrong or the file could not be read or parsed. A file that does not parse is
// the command line's (2) rather than a rename's (1) — the convention this tool was specified
// with, and the one place its numbers differ from fsmtable-gen's, which exits 1 for an input it
// cannot use.
#include "fsmtable.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void usage(std::ostream& out) {
    out << "fsmtable-transform — rewrite a .fsm's names, text to text\n"
        << "\n"
        << "  fsmtable-transform rename <file.fsm> <rename>...\n"
        << "                                           write the renamed machine to stdout\n"
        << "  fsmtable-transform --help\n"
        << "\n"
        << "A rename is <target>:<old>=<new>, one of\n"
        << "\n"
        << "  machine:TrafficLight=Lamp   the machine's own name\n"
        << "  state:Red=Green             a state, wherever the file names one\n"
        << "  kind:tick=beat              a kind's NAME — a row that writes the number keeps it\n"
        << "\n"
        << "Every comment, blank line and untouched directive comes back byte for byte: the\n"
        << "output is the same machine in the same file, with the names substituted. A rename\n"
        << "that would give two things one name is refused rather than merged, and the whole\n"
        << "list applies at once, so `state:A=B state:B=A` is a swap rather than a chase.\n"
        << "\n"
        << "Exit codes: 0 the renamed machine was written, 1 the rename could not be made (an\n"
        << "old name the file does not have, or a name another name already holds), 2 the\n"
        << "command line is wrong, or the file could not be read or parsed.\n";
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

// Two renames of the same old name in one namespace: the second one contradicts the first, and a
// caller who wrote both meant one of them. The library refuses it too; here it is the command
// line's fault, so the exit code is 2.
bool same_old_name(const fsmtable::Rename& a, const fsmtable::Rename& b) {
    return a.target == b.target && a.from == b.from;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        // A command line arrives as (int argc, char** argv), and reading argv[i] at all is
        // pointer arithmetic by this check's definition — the same reading .ci/tidy-baseline.txt
        // records for gen/fsmtable-gen.cpp and examples/inspector/inspector.cpp.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage(std::cout);
            return 0;
        }
        if (!arg.empty() && arg.front() == '-') {
            std::cerr << "fsmtable-transform: unknown option '" << arg << "'\n";
            usage(std::cerr);
            return 2;
        }
        positional.push_back(arg);
    }

    if (positional.empty()) {
        usage(std::cerr);
        return 2;
    }
    if (positional.front() != "rename") {
        std::cerr << "fsmtable-transform: unknown command '" << positional.front() << "'\n";
        usage(std::cerr);
        return 2;
    }
    if (positional.size() < 3) {
        std::cerr << "fsmtable-transform: rename takes a file and at least one rename\n";
        usage(std::cerr);
        return 2;
    }

    const std::string path = positional[1];
    std::vector<fsmtable::Rename> renames;
    for (std::size_t i = 2; i < positional.size(); ++i) {
        fsmtable::Rename entry;
        std::string message;
        if (!fsmtable::parse_rename_spec(positional[i], entry, message)) {
            std::cerr << "fsmtable-transform: " << message << "\n";
            return 2;
        }
        for (const fsmtable::Rename& seen : renames) {
            if (same_old_name(seen, entry)) {
                std::cerr << "fsmtable-transform: two renames for the same name ('" << entry.from
                          << "')\n";
                return 2;
            }
        }
        renames.push_back(entry);
    }

    std::string text;
    if (!read_file(path, text)) {
        std::cerr << "fsmtable-transform: cannot read " << path << "\n";
        return 2;
    }

    // The file is parsed HERE rather than only inside `rename`, because the two refusals carry
    // two exit codes and this is the only place that can tell them apart: a file that does not
    // parse is the command line's (2), a rename that cannot be made is the rename's (1).
    fsmtable::Error error{0, ""};
    if (!fsmtable::parse(text, error).has_value()) {
        std::cerr << path << ":" << error.line << ": " << error.message << "\n";
        return 2;
    }

    const auto renamed = fsmtable::rename(text, renames, error);
    if (!renamed.has_value()) {
        std::cerr << path << ": " << error.message << "\n";
        return 1;
    }
    std::cout << *renamed;
    return 0;
}
