// fsmtable-transform — rewrite a .fsm's names, or compose two .fsm files, text to text.
//
// The tool half of `fsmtable::rename` (src/transform.cpp) and `fsmtable::merge` (src/merge.cpp).
// Two verbs, one command line, and they differ in exactly one promise:
//
//   rename  reads one .fsm and writes the same machine back with names substituted, keeping every
//           comment, blank line and untouched directive byte for byte. Nothing but the names the
//           caller asked for moves — which is why this is not `fsmtable-inspect --canonical`.
//   merge   reads two .fsm files and writes their UNION, which is what makes it a verb of its own
//           rather than a flag: the composition policy lives in the header above `fsmtable::merge`,
//           and what it writes is a NEW machine — canonical text plus a provenance header naming
//           both parents.
//
// The three refusals carry three exit codes, the same three for both verbs and the same reading:
// 0 something was written, 1 the transform could not be made (rename: an old name the file does not
// have, or a new name another name already holds; merge: a kind the two machines disagree about, or
// a union that would leave a state unreachable), 2 the command line is wrong or a file could not be
// read, parsed or written. A file that does not parse is the command line's (2) rather than a
// transform's (1) — the convention this tool was specified with, and the one place its numbers
// differ from fsmtable-gen's, which exits 1 for an input it cannot use.
#include "fsmtable.hpp"

#include <cstddef>
#include <cstdint>
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
    out << "fsmtable-transform — rewrite a .fsm's names, or compose two .fsm files\n"
        << "\n"
        << "  fsmtable-transform rename <file.fsm> <rename>...\n"
        << "                                           write the renamed machine to stdout\n"
        << "  fsmtable-transform merge <a.fsm> <b.fsm> [-o <out.fsm>]\n"
        << "                                           write the union of two machines\n"
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
        << "A merge is a union fused on the state names the two machines share — that shared\n"
        << "name is the seam. The first machine's name and initial win, and the second's\n"
        << "initial becomes an ordinary state (its former initial-entry clause is dropped, and\n"
        << "said so on stderr). The output is a NEW machine, so it is canonical text: the two\n"
        << "files' comments do not come with it, and a provenance header naming both parents\n"
        << "and the merged fingerprint is written instead. Without -o the machine goes to\n"
        << "stdout.\n"
        << "\n"
        << "Exit codes: 0 written, 1 the transform could not be made (a rename that names an old\n"
        << "name the file does not have, or a new name another name already holds; a merge whose\n"
        << "two machines disagree about a kind, or whose union would leave a state unreachable),\n"
        << "2 the command line is wrong, or a file could not be read, parsed or written.\n";
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

// rename <file.fsm> <rename>... — the surgical edit. `-o` is not this verb's: a rename's output is
// the machine on stdout, which is what makes `> f.new && mv f.new f.fsm` the safe shape
// (TRANSFORM.md), and refusing the option here keeps the two verbs' command lines from drifting
// into each other.
int run_rename(const std::vector<std::string>& positional, bool has_output) {
    if (has_output) {
        std::cerr << "fsmtable-transform: -o is merge's; rename writes the machine to stdout\n";
        usage(std::cerr);
        return 2;
    }
    if (positional.size() < 3) {
        std::cerr << "fsmtable-transform: rename takes a file and at least one rename\n";
        usage(std::cerr);
        return 2;
    }

    const std::string& path = positional[1];
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

// merge <a.fsm> <b.fsm> [-o <out.fsm>] — the union, plus the header that says where it came from.
// The header is three claims, the same three a generated artifact's header makes (GENERATOR.md):
// which two files were composed, the exact command that composes them again, and the fingerprint of
// the canonical form — so a merged machine and the artifact generated from it can be compared
// without diffing text. The command names `<this file>` rather than the path this run was given,
// the placeholder the generator's header uses: otherwise the same merge, written to two places,
// would produce two different files.
int run_merge(const std::vector<std::string>& positional, const std::string& output,
              bool has_output) {
    if (positional.size() != 3) {
        std::cerr << "fsmtable-transform: merge takes exactly two files\n";
        usage(std::cerr);
        return 2;
    }
    const std::string& a_path = positional[1];
    const std::string& b_path = positional[2];

    std::string a_text;
    std::string b_text;
    if (!read_file(a_path, a_text)) {
        std::cerr << "fsmtable-transform: cannot read " << a_path << "\n";
        return 2;
    }
    if (!read_file(b_path, b_text)) {
        std::cerr << "fsmtable-transform: cannot read " << b_path << "\n";
        return 2;
    }

    // Both files are parsed here for the same reason `rename` parses its own: a file that does not
    // parse is the command line's (2) rather than the merge's (1), and this is the only place that
    // knows which of the two the line number belongs to. The names come back for the header.
    fsmtable::Error error{0, ""};
    const auto a = fsmtable::parse(a_text, error);
    if (!a.has_value()) {
        std::cerr << a_path << ":" << error.line << ": " << error.message << "\n";
        return 2;
    }
    const auto b = fsmtable::parse(b_text, error);
    if (!b.has_value()) {
        std::cerr << b_path << ":" << error.line << ": " << error.message << "\n";
        return 2;
    }

    const auto merged = fsmtable::merge(a_text, b_text, error);
    if (!merged.has_value()) {
        std::cerr << a_path << " + " << b_path << ": " << error.message << "\n";
        return 1;
    }

    // What the merge had to drop is reported and does not fail the run: the union is well defined,
    // and the clause that went is named rather than left for a reader to notice (TRANSFORM.md).
    for (const std::string& warning : merged->warnings) {
        std::cerr << "fsmtable-transform: " << b_path << ": " << warning << "\n";
    }

    std::ostringstream file;
    file << "# Merged by fsmtable-transform from " << a_path << " (machine " << a->name << ")\n"
         << "# and " << b_path << " (machine " << b->name << "), fused on the state names the two\n"
         << "# machines share. Re-merge with: fsmtable-transform merge " << a_path << " " << b_path
         << " -o <this file>\n"
         << "# Fingerprint of the canonical form (FNV-1a 64): 0x" << std::hex << std::uppercase
         << merged->fingerprint << std::dec << std::nouppercase << "\n"
         << merged->text;

    if (!has_output) {
        std::cout << file.str();
        return 0;
    }

    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        std::cerr << "fsmtable-transform: cannot write " << output << "\n";
        return 2;
    }
    out << file.str();
    out.close();
    if (!out.good()) {
        std::cerr << "fsmtable-transform: cannot write " << output << "\n";
        return 2;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // The command line is copied into strings once, here, rather than walked as argv inside the
    // option loop: a command line arrives as (int argc, char** argv), and reading argv[i] at all is
    // pointer arithmetic by this check's definition — the same reading .ci/tidy-baseline.txt
    // records for gen/fsmtable-gen.cpp and examples/inspector/inspector.cpp. Copying it does not
    // hide the array, and it leaves one place where the array is touched instead of three.
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 1; i < argc; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        args.emplace_back(argv[i]);
    }

    std::string output;
    bool has_output = false;
    std::vector<std::string> positional;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--help" || arg == "-h") {
            usage(std::cout);
            return 0;
        }
        if (arg == "-o") {
            if (i + 1 >= args.size()) {
                std::cerr << "fsmtable-transform: -o needs the file to write\n";
                usage(std::cerr);
                return 2;
            }
            if (has_output) {
                std::cerr << "fsmtable-transform: -o was given twice\n";
                usage(std::cerr);
                return 2;
            }
            output = args[i + 1];
            has_output = true;
            ++i;
            continue;
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
    if (positional.front() == "rename")
        return run_rename(positional, has_output);
    if (positional.front() == "merge")
        return run_merge(positional, output, has_output);

    std::cerr << "fsmtable-transform: unknown command '" << positional.front() << "'\n";
    usage(std::cerr);
    return 2;
}
