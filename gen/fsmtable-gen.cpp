// fsmtable-gen — turn a .fsm file into a self-contained C++ header.
//
// The generated header carries the machine as DATA: a state enum, the event kinds the machine
// uses, a state-name table, and the transition rows. The rows are emitted in the format's
// canonical order — guarded before unguarded within the same from+kind — which is the point:
// fsmgine::compiled::Machine scans its table first-match-wins, so that order is what makes it
// reproduce the format's guard precedence (SPEC.md rule 9). Emitting the rows in file order
// would silently change behaviour exactly when a file wrote the unguarded row first.
//
// The action names in the .fsm become SYMBOLS: this header declares each one, so the caller
// defines them and a misspelt action is a compile (or link) error instead of a row that never
// fires. State, action and machine names that are C++ keywords are refused with a message
// rather than mangled: a generated file that does not compile is worse than an error here.
//
// v1 limits, shared with the format and with the compiled back end: one int refinement slot,
// one action per row, no entry/exit actions. Widening any of those is a change to SPEC.md and
// to fsmgine::compiled::Machine together, not to this tool.
#include "fsmtable.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kDefaultNamespace = "fsmtable_generated";

std::string usage() {
    return "usage: fsmtable-gen <input.fsm> [-o <output.hpp>] [--namespace <name>]\n"
           "\n"
           "Writes a self-contained C++ header for the machine in <input.fsm>:\n"
           "  <Machine>State      enum class, in first-appearance order\n"
           "  <Machine>Kind       enum class of the event kinds the rows use\n"
           "  <Machine>Event      { kind, value } — kind is what the back end reads, value is\n"
           "                      the format's single refinement slot\n"
           "  <Machine>StateNames the state names, for logs and code -> text round trips\n"
           "  <Machine>Rows       the transition table in the format's canonical order\n"
           "  make<Machine>()     a ready machine (one instance per thread: process() writes)\n"
           "\n"
           "Each action name in the file becomes a declared function, e.g. for `action go`:\n"
           "    void go(const <Machine>Event& event);\n"
           "Define those in your own translation unit; a name the file uses and you do not\n"
           "define is a link error, never a row that quietly never fires.\n"
           "\n"
           "Names that are C++ keywords (or a machine with more than 65535 states) are refused.\n"
           "Without -o the header goes to stdout. Exit: 0 written, 1 bad input, 2 bad usage.\n";
}

// FNV-1a 64 over the canonical form: the generated header says which machine it came from, so
// a stale artifact can be spotted without diffing text.
std::uint64_t fingerprint(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL;
    }
    return hash;
}

// C++17 keywords. A name that is one of these cannot be an enum member, a function or a type
// name, so it is refused. (Macro names like NULL cannot be checked for and are mentioned in
// the tool's output instead.)
bool is_cpp_keyword(std::string_view name) {
    constexpr std::array kKeywords{
        std::string_view{"alignas"},      std::string_view{"alignof"},
        std::string_view{"and"},          std::string_view{"and_eq"},
        std::string_view{"asm"},          std::string_view{"auto"},
        std::string_view{"bitand"},       std::string_view{"bitor"},
        std::string_view{"bool"},         std::string_view{"break"},
        std::string_view{"case"},         std::string_view{"catch"},
        std::string_view{"char"},         std::string_view{"char16_t"},
        std::string_view{"char32_t"},     std::string_view{"class"},
        std::string_view{"compl"},        std::string_view{"const"},
        std::string_view{"const_cast"},   std::string_view{"constexpr"},
        std::string_view{"continue"},     std::string_view{"decltype"},
        std::string_view{"default"},      std::string_view{"delete"},
        std::string_view{"do"},           std::string_view{"double"},
        std::string_view{"dynamic_cast"}, std::string_view{"else"},
        std::string_view{"enum"},         std::string_view{"explicit"},
        std::string_view{"export"},       std::string_view{"extern"},
        std::string_view{"false"},        std::string_view{"float"},
        std::string_view{"for"},          std::string_view{"friend"},
        std::string_view{"goto"},         std::string_view{"if"},
        std::string_view{"inline"},       std::string_view{"int"},
        std::string_view{"long"},         std::string_view{"mutable"},
        std::string_view{"namespace"},    std::string_view{"new"},
        std::string_view{"noexcept"},     std::string_view{"not"},
        std::string_view{"not_eq"},       std::string_view{"nullptr"},
        std::string_view{"operator"},     std::string_view{"or"},
        std::string_view{"or_eq"},        std::string_view{"private"},
        std::string_view{"protected"},    std::string_view{"public"},
        std::string_view{"register"},     std::string_view{"reinterpret_cast"},
        std::string_view{"return"},       std::string_view{"short"},
        std::string_view{"signed"},       std::string_view{"sizeof"},
        std::string_view{"static"},       std::string_view{"static_assert"},
        std::string_view{"static_cast"},  std::string_view{"struct"},
        std::string_view{"switch"},       std::string_view{"template"},
        std::string_view{"this"},         std::string_view{"thread_local"},
        std::string_view{"throw"},        std::string_view{"true"},
        std::string_view{"try"},          std::string_view{"typedef"},
        std::string_view{"typeid"},       std::string_view{"typename"},
        std::string_view{"union"},        std::string_view{"unsigned"},
        std::string_view{"using"},        std::string_view{"virtual"},
        std::string_view{"void"},         std::string_view{"volatile"},
        std::string_view{"wchar_t"},      std::string_view{"while"},
        std::string_view{"xor"},          std::string_view{"xor_eq"},
    };
    return std::find(kKeywords.begin(), kKeywords.end(), name) != kKeywords.end();
}

bool is_identifier(std::string_view name) {
    if (name.empty())
        return false;
    const auto start_ok
        = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
    if (!start_ok(name.front()))
        return false;
    for (const char c : name) {
        if (!start_ok(c) && !(c >= '0' && c <= '9'))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------- what the header is made of

struct Names {
    std::string space;  // the namespace the header opens
    std::string prefix; // e.g. "TrafficLight"

    [[nodiscard]] std::string state() const { return prefix + "State"; }
    [[nodiscard]] std::string kind() const { return prefix + "Kind"; }
    [[nodiscard]] std::string event() const { return prefix + "Event"; }
    [[nodiscard]] std::string state_names() const { return prefix + "StateNames"; }
    [[nodiscard]] std::string name_of() const { return prefix + "NameOf"; }
    [[nodiscard]] std::string rows() const { return prefix + "Rows"; }
    [[nodiscard]] std::string make() const { return "make" + prefix; }
};

std::string kind_member(int kind) { return "k" + std::to_string(kind); }

// The canonical order of SPEC.md section 4, mirrored from dump()'s comparator. This is what
// makes the emitted table mean the same thing as the text.
std::vector<const fsmtable::Transition*> canonical_order(const fsmtable::Machine& machine) {
    std::vector<const fsmtable::Transition*> rows;
    rows.reserve(machine.transitions.size());
    for (const fsmtable::Transition& row : machine.transitions)
        rows.push_back(&row);
    std::sort(rows.begin(), rows.end(),
              [](const fsmtable::Transition* a, const fsmtable::Transition* b) {
                  if (a->from != b->from)
                      return a->from < b->from;
                  if (a->kind != b->kind)
                      return a->kind < b->kind;
                  if (a->has_when != b->has_when)
                      return a->has_when;
                  if (a->to != b->to)
                      return a->to < b->to;
                  if (a->action != b->action)
                      return a->action < b->action;
                  if (a->when_op != b->when_op)
                      return a->when_op < b->when_op;
                  return a->when_value < b->when_value;
              });
    return rows;
}

std::string op_name(fsmtable::Op op) {
    switch (op) {
    case fsmtable::Op::Eq:
        return "Eq";
    case fsmtable::Op::Lt:
        return "Lt";
    case fsmtable::Op::Le:
        return "Le";
    case fsmtable::Op::Gt:
        return "Gt";
    case fsmtable::Op::Ge:
        return "Ge";
    }
    return "Eq";
}

std::vector<int> kinds_of(const std::vector<const fsmtable::Transition*>& rows) {
    std::vector<int> kinds;
    for (const fsmtable::Transition* row : rows) {
        if (std::find(kinds.begin(), kinds.end(), row->kind) == kinds.end())
            kinds.push_back(row->kind);
    }
    std::sort(kinds.begin(), kinds.end());
    return kinds;
}

std::vector<std::string> actions_of(const std::vector<const fsmtable::Transition*>& rows) {
    std::vector<std::string> actions;
    for (const fsmtable::Transition* row : rows) {
        if (row->action.empty())
            continue;
        if (std::find(actions.begin(), actions.end(), row->action) == actions.end())
            actions.push_back(row->action);
    }
    return actions;
}

// ------------------------------------------------------------------------------- the header

void emit_header(std::ostringstream& out, const Names& names, const fsmtable::Machine& machine,
                 std::string_view source, std::uint64_t canonical_fingerprint) {
    const std::vector<const fsmtable::Transition*> rows = canonical_order(machine);
    const std::vector<int> kinds = kinds_of(rows);
    const std::vector<std::string> actions = actions_of(rows);

    out << "// Generated by fsmtable-gen from " << source << " — DO NOT EDIT.\n"
        << "// Regenerate with: fsmtable-gen " << source << " -o <this file>\n"
        << "// Fingerprint of the canonical form (FNV-1a 64): 0x" << std::hex << std::uppercase
        << canonical_fingerprint << std::dec << std::nouppercase << "\n"
        << "//\n"
        << "// The rows are in the format's canonical order (guarded before unguarded within one\n"
        << "// from+kind), which is what makes fsmgine::compiled::Machine's first-match-wins\n"
        << "// reproduce the format's guard precedence.\n"
        << "#pragma once\n"
        << "\n"
        << "// Generated: keep the formatter out of it, so that regenerating and formatting\n"
        << "// cannot become a two-step dance in the caller's tree.\n"
        << "// clang-format off\n"
        << "\n"
        << "#include <FSMgine/compiled/Machine.hpp>\n"
        << "\n"
        << "#include <array>\n"
        << "#include <cstddef>\n"
        << "#include <cstdint>\n"
        << "#include <string_view>\n"
        << "#include <vector>\n"
        << "\n"
        << "namespace " << names.space << " {\n"
        << "\n"
        << "// " << machine.name << " — states in first-appearance order.\n"
        << "enum class " << names.state() << " : std::uint16_t {\n";
    for (const std::string& state : machine.states)
        out << "    " << state << ",\n";
    out << "};\n"
        << "\n"
        << "// The event kinds this machine's rows use.\n"
        << "enum class " << names.kind() << " : std::uint8_t {\n";
    for (const int kind : kinds)
        out << "    " << kind_member(kind) << " = " << kind << ",\n";
    out << "};\n"
        << "\n"
        << "struct " << names.event() << " {\n"
        << "    " << names.kind() << " kind; // what the compiled back end reads\n"
        << "    // The format's single refinement slot; unread when no row is guarded.\n"
        << "    int value = 0;\n"
        << "};\n";

    if (!actions.empty()) {
        out << "\n// Actions this machine calls. Define them in your own translation unit.\n";
        for (const std::string& action : actions)
            out << "void " << action << "(const " << names.event() << "& event);\n";
    }

    out << "\n// The state names, for logs and for code -> text round trips.\n"
        << "inline constexpr std::array<std::string_view, " << machine.states.size() << "> "
        << names.state_names() << "{{";
    for (std::size_t i = 0; i < machine.states.size(); ++i)
        out << (i == 0 ? "" : ", ") << "\"" << machine.states[i] << "\"";
    out << "}};\n"
        << "\n"
        << "[[nodiscard]] inline std::string_view " << names.name_of() << "(" << names.state()
        << " state) {\n"
        << "    return " << names.state_names() << "[static_cast<std::size_t>(state)];\n"
        << "}\n"
        << "\n"
        << "// The transition table: from, kind, refined, op, value, to, action.\n"
        << "inline constexpr std::array<fsmgine::compiled::Transition<" << names.state() << ", "
        << names.event() << ">, " << rows.size() << "> " << names.rows() << "{{\n";
    for (const fsmtable::Transition* row : rows) {
        out << "    {" << names.state() << "::" << row->from << ", " << names.kind()
            << "::" << kind_member(row->kind) << ", " << (row->has_when ? "true" : "false")
            << ", fsmgine::compiled::Op::" << op_name(row->when_op) << ", " << row->when_value
            << ", " << names.state() << "::" << row->to << ", "
            << (row->action.empty() ? "nullptr" : "&" + row->action) << "},\n";
    }
    out << "}};\n"
        << "\n"
        << "// One machine ready to drive. Build one per thread: process() writes its own\n"
        << "// current state, so a shared instance is a data race.\n"
        << "[[nodiscard]] inline fsmgine::compiled::Machine<" << names.state() << ", "
        << names.event() << "> " << names.make() << "() {\n"
        << "    fsmgine::compiled::Machine<" << names.state() << ", " << names.event()
        << "> machine{\n"
        << "        &" << names.event() << "::kind, &" << names.event() << "::value,\n"
        << "        std::vector<fsmgine::compiled::Transition<" << names.state() << ", "
        << names.event() << ">>{" << names.rows() << ".begin(), " << names.rows() << ".end()}};\n"
        << "    machine.withNames(&" << names.name_of() << ");\n"
        << "    machine.setInitialState(" << names.state() << "::" << machine.initial << ");\n"
        << "    return machine;\n"
        << "}\n"
        << "\n"
        << "} // namespace " << names.space << "\n"
        << "// clang-format on\n";
}

// ------------------------------------------------------------------------------- the checks

// Every name in the file becomes a C++ identifier, so each one is checked before anything is
// emitted: the message names the offender and the line-free name, which is what a user needs to
// fix the .fsm.
bool check_names(const fsmtable::Machine& machine, std::string& complaint) {
    if (is_cpp_keyword(machine.name)) {
        complaint = "the machine name '" + machine.name + "' is a C++ keyword";
        return false;
    }
    for (const std::string& state : machine.states) {
        if (is_cpp_keyword(state)) {
            complaint = "the state name '" + state + "' is a C++ keyword";
            return false;
        }
    }
    for (const fsmtable::Transition& row : machine.transitions) {
        if (row.action.empty())
            continue;
        if (is_cpp_keyword(row.action)) {
            complaint = "the action name '" + row.action + "' is a C++ keyword";
            return false;
        }
    }
    if (machine.states.size() > 65535U) {
        complaint = "the machine has more than 65535 states, which the emitted enum cannot hold";
        return false;
    }
    return true;
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

} // namespace

int main(int argc, char** argv) {
    std::string input;
    std::string output;
    std::string space(kDefaultNamespace);
    bool namespace_given = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << usage();
            return 0;
        }
        if (arg == "-o" || arg == "--namespace") {
            if (i + 1 >= argc) {
                std::cerr << "fsmtable-gen: " << arg << " needs an argument\n";
                return 2;
            }
            const std::string value = argv[++i];
            if (arg == "-o")
                output = value;
            else {
                space = value;
                namespace_given = true;
            }
            continue;
        }
        if (arg.size() > 1 && arg.front() == '-') {
            std::cerr << "fsmtable-gen: unknown option " << arg << "\n" << usage();
            return 2;
        }
        if (!input.empty()) {
            std::cerr << "fsmtable-gen: more than one input file\n";
            return 2;
        }
        input = arg;
    }

    if (input.empty()) {
        std::cerr << usage();
        return 2;
    }
    if (namespace_given && (!is_identifier(space) || is_cpp_keyword(space))) {
        std::cerr << "fsmtable-gen: --namespace '" << space << "' is not a usable identifier\n";
        return 2;
    }

    std::string text;
    if (!read_file(input, text)) {
        std::cerr << "fsmtable-gen: cannot read " << input << "\n";
        return 1;
    }

    fsmtable::Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);
    if (!machine.has_value()) {
        std::cerr << "fsmtable-gen: " << input << ":" << error.line << ": " << error.message
                  << "\n";
        return 1;
    }

    std::string complaint;
    if (!check_names(*machine, complaint)) {
        std::cerr << "fsmtable-gen: " << input << ": " << complaint << "\n";
        return 1;
    }

    Names names;
    names.space = space;
    names.prefix = machine->name;

    std::ostringstream header;
    emit_header(header, names, *machine, input, fingerprint(fsmtable::dump(*machine)));
    const std::string rendered = header.str();

    if (output.empty()) {
        std::cout << rendered;
        return 0;
    }
    std::ofstream out(output);
    if (!out.good()) {
        std::cerr << "fsmtable-gen: cannot write " << output << "\n";
        return 1;
    }
    out << rendered;
    out.close();
    if (!out.good()) {
        std::cerr << "fsmtable-gen: writing " << output << " failed\n";
        return 1;
    }
    std::cerr << "fsmtable-gen: " << output << " — " << machine->states.size() << " state(s), "
              << machine->transitions.size() << " row(s)\n";
    return 0;
}