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
    return "usage: fsmtable-gen <input.fsm> [-o <output>] [--target cpp|deno] "
           "[--namespace <name>]\n"
           "\n"
           "Writes code for the machine in <input.fsm>. --target picks the back end: cpp (the\n"
           "default) writes the C++ header described below; deno writes one self-contained\n"
           "TypeScript module (the last section of this text). Without -o the output goes to\n"
           "stdout. Exit: 0 written, 1 the input is unusable, 2 the command line is.\n"
           "\n"
           "C++ (--target cpp):\n"
           "  <Machine>State      enum class, in first-appearance order\n"
           "  <Machine>Kind       enum class of the event kinds: a name the file declared, or\n"
           "                      k<number> when it declared none\n"
           "  <Machine>Event      { kind, value } — kind is what the back end reads, value is\n"
           "                      the format's single refinement slot\n"
           "  <Machine>StateNames the state names, for logs and code -> text round trips\n"
           "  <Machine>KindNames  the kind names the file declared, for code -> text round trips\n"
           "  <Machine>KindNameOf(<kind>) the declared name, or empty for an unnamed kind\n"
           "  <Machine>Rows       the transition table in the format's canonical order\n"
           "  make<Machine>()     a ready machine (one instance per thread: process() writes)\n"
           "\n"
           "Each action name in the file becomes a declared function, e.g. for `action go`:\n"
           "    void go(const <Machine>Event& event);\n"
           "Define those in your own translation unit; a name the file uses and you do not\n"
           "define is a link error, never a row that quietly never fires.\n"
           "\n"
           "Names that are C++ keywords (or a machine with more than 65535 states) are refused.\n"
           "\n"
           "TypeScript (--target deno):\n"
           "  State               the states as a union of string literals\n"
           "  states              the same names as an array, in first-appearance order\n"
           "  Kind                enum of the event kinds: a declared name, or k<number>\n"
           "  Event               { kind, value }\n"
           "  actionNames, Action every action the file names: a const array, and the union of\n"
           "                      its element type, so a registry typed by it is checked by\n"
           "                      `deno check`\n"
           "  initial             the initial state\n"
           "  initialEntry        the initial state's entry clause as action names (Q14)\n"
           "  step(state, event)  the pure table: the 2D lookup, first-match-wins over the rows\n"
           "                      for that pair, and the composed action names to run (exit, the\n"
           "                      row's action, entry)\n"
           "Every name becomes a string literal or an enum member, so the C++ keyword rule and\n"
           "the 65535-state limit do not apply to this target. --namespace has no meaning for it\n"
           "(one module per file) and is refused.\n";
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
    [[nodiscard]] std::string kind_names() const { return prefix + "KindNames"; }
    [[nodiscard]] std::string kind_name_of() const { return prefix + "KindNameOf"; }
    [[nodiscard]] std::string enter_initial() const { return "enterInitial" + prefix; }
    [[nodiscard]] std::string event() const { return prefix + "Event"; }
    [[nodiscard]] std::string state_names() const { return prefix + "StateNames"; }
    [[nodiscard]] std::string name_of() const { return prefix + "NameOf"; }
    [[nodiscard]] std::string rows() const { return prefix + "Rows"; }
    [[nodiscard]] std::string make() const { return "make" + prefix; }
};

// The enumerator for a kind: the name the file declared for it, or `k<number>` when it declared
// none. A declared name is used verbatim — the generator refuses a C++ keyword rather than
// mangling it (QUESTIONS.md Q8), and that rule holds here too.
std::string kind_member(int kind, const std::vector<fsmtable::KindName>& kind_names) {
    for (const fsmtable::KindName& entry : kind_names) {
        if (entry.kind == kind)
            return entry.name;
    }
    return "k" + std::to_string(kind);
}

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

// The same op as the generated TypeScript spells it: the format's own word, lower case, so the
// emitted table reads like the `.fsm` line it came from (`when ge 30` -> op: "ge").
std::string op_code(fsmtable::Op op) {
    switch (op) {
    case fsmtable::Op::Eq:
        return "eq";
    case fsmtable::Op::Lt:
        return "lt";
    case fsmtable::Op::Le:
        return "le";
    case fsmtable::Op::Gt:
        return "gt";
    case fsmtable::Op::Ge:
        return "ge";
    }
    return "eq";
}

// Every kind the header needs an enumerator for: the ones the rows use, and the ones the file
// declared whether or not a row uses them. A declaration is part of the file, and a caller may
// drive an event no row currently matches.
std::vector<int> kinds_of(const std::vector<const fsmtable::Transition*>& rows,
                          const std::vector<fsmtable::KindName>& kind_names) {
    std::vector<int> kinds;
    for (const fsmtable::KindName& entry : kind_names) {
        if (std::find(kinds.begin(), kinds.end(), entry.kind) == kinds.end())
            kinds.push_back(entry.kind);
    }
    for (const fsmtable::Transition* row : rows) {
        if (std::find(kinds.begin(), kinds.end(), row->kind) == kinds.end())
            kinds.push_back(row->kind);
    }
    std::sort(kinds.begin(), kinds.end());
    return kinds;
}

std::vector<std::string> actions_of(const std::vector<const fsmtable::Transition*>& rows,
                                    const std::vector<fsmtable::StateAction>& state_actions) {
    std::vector<std::string> actions;
    for (const fsmtable::Transition* row : rows) {
        if (row->action.empty())
            continue;
        if (std::find(actions.begin(), actions.end(), row->action) == actions.end())
            actions.push_back(row->action);
    }
    // Entry and exit clauses are actions too: declared here, so a name the file uses and the
    // caller never defines is a link error like any other. Ordered by state name, the order the
    // dump writes the state lines in.
    std::vector<const fsmtable::StateAction*> decorated;
    decorated.reserve(state_actions.size());
    for (const fsmtable::StateAction& entry : state_actions) {
        decorated.push_back(&entry);
    }
    std::sort(decorated.begin(), decorated.end(),
              [](const fsmtable::StateAction* a, const fsmtable::StateAction* b) {
                  return a->state < b->state;
              });
    for (const fsmtable::StateAction* entry : decorated) {
        if (!entry->enter.empty()
            && std::find(actions.begin(), actions.end(), entry->enter) == actions.end()) {
            actions.push_back(entry->enter);
        }
        if (!entry->exit.empty()
            && std::find(actions.begin(), actions.end(), entry->exit) == actions.end()) {
            actions.push_back(entry->exit);
        }
    }
    return actions;
}

// One state's clauses, or empty when the file decorated nothing for it.
std::string enter_of(const std::vector<fsmtable::StateAction>& state_actions,
                     const std::string& state) {
    for (const fsmtable::StateAction& entry : state_actions) {
        if (entry.state == state)
            return entry.enter;
    }
    return {};
}

std::string exit_of(const std::vector<fsmtable::StateAction>& state_actions,
                    const std::string& state) {
    for (const fsmtable::StateAction& entry : state_actions) {
        if (entry.state == state)
            return entry.exit;
    }
    return {};
}

// A generated function that runs one transition's exit, action and entry. `key` is the three
// names, so two rows that do the same three things share one function.
struct Wrapper {
    std::string key;
    std::string name;
    std::vector<std::string> calls;
};

// ------------------------------------------------------------------------------- the header

void emit_header(std::ostringstream& out, const Names& names, const fsmtable::Machine& machine,
                 const std::vector<fsmtable::KindName>& kind_names,
                 const std::vector<fsmtable::StateAction>& state_actions, std::string_view source,
                 std::uint64_t canonical_fingerprint) {
    const std::vector<const fsmtable::Transition*> rows = canonical_order(machine);
    const std::vector<int> kinds = kinds_of(rows, kind_names);
    const std::vector<std::string> actions = actions_of(rows, state_actions);

    // Entry and exit composition (ENTRY_EXIT.md): the back end runs exactly one function per
    // transition, so a row whose states are decorated points at a generated function that runs
    // the source state's exit, the row's own action and the target state's entry. A row whose
    // states carry no clause points straight at its action, which is what leaves every artifact
    // without entry/exit byte for byte what it was.
    std::vector<Wrapper> wrappers;
    const auto taken = [&](const std::string& candidate) {
        if (std::find(actions.begin(), actions.end(), candidate) != actions.end())
            return true;
        for (const Wrapper& wrapper : wrappers)
            if (wrapper.name == candidate)
                return true;
        return false;
    };
    std::vector<std::string> row_action(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const fsmtable::Transition* row = rows[i];
        const std::string exit = exit_of(state_actions, row->from);
        const std::string enter = enter_of(state_actions, row->to);
        if (exit.empty() && enter.empty()) {
            row_action[i] = row->action.empty() ? "nullptr" : "&" + row->action;
            continue;
        }
        Wrapper made;
        if (!exit.empty())
            made.calls.push_back(exit);
        if (!row->action.empty())
            made.calls.push_back(row->action);
        if (!enter.empty())
            made.calls.push_back(enter);
        // The key is just the calls in order, built from them rather than beside them, so the
        // two cannot drift apart.
        for (const std::string& call : made.calls) {
            made.key += call;
            made.key += '\n';
        }

        std::size_t at = wrappers.size();
        for (std::size_t w = 0; w < wrappers.size(); ++w) {
            if (wrappers[w].key == made.key)
                at = w;
        }
        if (at == wrappers.size()) {
            const std::string base = row->from + "_leaving_to_" + row->to;
            made.name = base;
            for (int n = 2; taken(made.name); ++n)
                made.name = base + "_" + std::to_string(n);
            wrappers.push_back(made);
        }
        row_action[i] = "&" + wrappers[at].name;
    }

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
        << "#include <utility>\n"
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
        << "// The event kinds this machine uses: one the file declared is emitted under its own\n"
        << "// name, one it did not is k<number>.\n"
        << "enum class " << names.kind() << " : std::uint8_t {\n";
    for (const int kind : kinds)
        out << "    " << kind_member(kind, kind_names) << " = " << kind << ",\n";
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

    if (!wrappers.empty()) {
        out << "\n// Entry and exit composition (ENTRY_EXIT.md): one function per distinct pair "
               "of\n"
            << "// state clauses and row action, because the back end runs one function per row.\n";
        for (const Wrapper& wrapper : wrappers) {
            out << "inline void " << wrapper.name << "(const " << names.event() << "& event) {\n";
            for (const std::string& call : wrapper.calls)
                out << "    " << call << "(event);\n";
            out << "}\n";
        }
    }

    const std::string initial_enter = enter_of(state_actions, machine.initial);
    if (!initial_enter.empty()) {
        out << "\n// The initial state's entry action — nothing calls it. The back end's\n"
            << "// setInitialState is not a transition, and a factory with a side effect is the "
               "kind\n"
            << "// of surprise this exercise is against. Call it once after " << names.make()
            << "() if the\n// state needs it; the event is a placeholder, because there is no "
               "event.\n"
            << "inline void " << names.enter_initial() << "(const " << names.event()
            << "& event = {}) {\n"
            << "    " << initial_enter << "(event);\n"
            << "}\n";
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
        << "}\n";

    if (!kind_names.empty()) {
        // The other direction for the kinds. Only the kinds the file named appear: an unnamed
        // one has no text name to give back, and inventing `k3` here would make a code -> text
        // round trip write a name the format would then reject.
        std::vector<const fsmtable::KindName*> named;
        named.reserve(kind_names.size());
        for (const fsmtable::KindName& entry : kind_names) {
            named.push_back(&entry);
        }
        std::sort(named.begin(), named.end(),
                  [](const fsmtable::KindName* a, const fsmtable::KindName* b) {
                      if (a->kind != b->kind)
                          return a->kind < b->kind;
                      return a->name < b->name;
                  });
        out << "\n// The kind names the file declared, for logs and code -> text round trips.\n"
            << "// An undeclared kind has no name: the lookup returns empty rather than a guess.\n"
            << "inline constexpr std::array<std::pair<" << names.kind() << ", std::string_view>, "
            << named.size() << "> " << names.kind_names() << "{{\n";
        for (const fsmtable::KindName* entry : named) {
            out << "    {" << names.kind() << "::" << kind_member(entry->kind, kind_names) << ", \""
                << entry->name << "\"},\n";
        }
        out << "}};\n"
            << "\n"
            << "[[nodiscard]] inline std::string_view " << names.kind_name_of() << "("
            << names.kind() << " kind) {\n"
            << "    for (const std::pair<" << names.kind()
            << ", std::string_view>& entry : " << names.kind_names() << ") {\n"
            << "        if (entry.first == kind)\n"
            << "            return entry.second;\n"
            << "    }\n"
            << "    return {};\n"
            << "}\n";
    }

    out << "\n"
        << "// The transition table: from, kind, refined, op, value, to, action.\n"
        << "inline constexpr std::array<fsmgine::compiled::Transition<" << names.state() << ", "
        << names.event() << ">, " << rows.size() << "> " << names.rows() << "{{\n";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const fsmtable::Transition* row = rows[i];
        out << "    {" << names.state() << "::" << row->from << ", " << names.kind()
            << "::" << kind_member(row->kind, kind_names) << ", "
            << (row->has_when ? "true" : "false")
            << ", fsmgine::compiled::Op::" << op_name(row->when_op) << ", " << row->when_value
            << ", " << names.state() << "::" << row->to << ", " << row_action[i] << "},\n";
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

// ------------------------------------------------------------------- the deno back end

// The text -> TypeScript direction: one self-contained module per machine. The shape is a union
// of string literals for the states, a Kind enum, Event { kind, value }, and a pure
// step(state, event) driven by a 2D lookup of from+kind -> the rows for that pair.
//
// The one place the two back ends part company is entry and exit composition. The C++ header
// points each row at ONE generated function, because fsmgine::compiled::Machine takes one
// function pointer per row. TypeScript has no such constraint, so there is no wrapper function to
// invent: the same composition (ENTRY_EXIT.md's exit, then the row's action, then the entry) is
// carried as the ordered list of action NAMES on the row, and step() hands that list back. It
// keeps step() pure and keeps the actions in the caller's own registry — and it is the same three
// calls the C++ header folds into a function, because the same helpers build both.
void emit_deno(std::ostringstream& out, const fsmtable::Machine& machine,
               const std::vector<fsmtable::KindName>& kind_names,
               const std::vector<fsmtable::StateAction>& state_actions, std::string_view source,
               std::uint64_t canonical_fingerprint) {
    const std::vector<const fsmtable::Transition*> rows = canonical_order(machine);
    const std::vector<int> kinds = kinds_of(rows, kind_names);
    const std::vector<std::string> actions = actions_of(rows, state_actions);

    // The calls one row runs, in ENTRY_EXIT.md's order: the source state's exit, then the row's
    // own action, then the target state's entry. Empty when a row's ends carry no clause and it
    // names no action.
    const auto composed = [&state_actions](const fsmtable::Transition& row) {
        std::vector<std::string> calls;
        const std::string exit = exit_of(state_actions, row.from);
        if (!exit.empty())
            calls.push_back(exit);
        if (!row.action.empty())
            calls.push_back(row.action);
        const std::string enter = enter_of(state_actions, row.to);
        if (!enter.empty())
            calls.push_back(enter);
        return calls;
    };
    // A TypeScript array of string literals: ["a", "b"], or [] when there is nothing to run.
    const auto literal_list = [](const std::vector<std::string>& items) {
        std::string text = "[";
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i > 0)
                text += ", ";
            text += '"' + items[i] + '"';
        }
        text += ']';
        return text;
    };

    out << "// Generated by fsmtable-gen --target deno from " << source << " — DO NOT EDIT.\n"
        << "// Regenerate with: fsmtable-gen --target deno " << source << " -o <this file>\n"
        << "// Fingerprint of the canonical form (FNV-1a 64): 0x" << std::hex << std::uppercase
        << canonical_fingerprint << std::dec << std::nouppercase << "\n"
        << "//\n"
        << "// The rows are in the format's canonical order (guarded before unguarded within one\n"
        << "// from+kind): step() takes the first row that answers, so that order IS the guard\n"
        << "// precedence rule (SPEC.md rule 9). Entry and exit clauses are composed here, at\n"
        << "// generation time, into each row's action list: the source state's exit, then the\n"
        << "// row's own action, then the target state's entry (ENTRY_EXIT.md).\n"
        << "\n"
        << "export type State = ";
    for (std::size_t i = 0; i < machine.states.size(); ++i)
        out << (i == 0 ? "" : " | ") << '"' << machine.states[i] << '"';
    out << ";\n"
        << "\n"
        << "// The states in first-appearance order, for logs and for the round trip back to "
           "text.\n"
        << "export const states: readonly State[] = " << literal_list(machine.states) << ";\n"
        << "\n"
        << "// The event kinds this machine uses: one the file declared is emitted under its own\n"
        << "// name, one it did not is k<number>.\n"
        << "export enum Kind {\n";
    for (const int kind : kinds)
        out << "  " << kind_member(kind, kind_names) << " = " << kind << ",\n";
    out << "}\n"
        << "\n"
        << "export interface Event {\n"
        << "  kind: Kind; // what step() reads\n"
        << "  value: number; // the single refinement slot; read only when a row is refined\n"
        << "}\n"
        << "\n"
        << "// Every action the file names, as a union type. The caller keys its own registry by "
           "it,\n"
        << "// and `deno check` refuses a registry that is missing one of these names — the "
           "guarantee\n"
        << "// the C++ header gets from declaring each action as a function.\n"
        << "export const actionNames = " << literal_list(actions) << " as const;\n"
        << "export type Action = (typeof actionNames)[number];\n"
        << "\n"
        << "export interface Step {\n"
        << "  state: State; // the state after this event (unchanged when nothing fired)\n"
        << "  fired: boolean; // did a row answer the event\n"
        << "  actions: readonly Action[]; // exit, the row's action, entry — in that order\n"
        << "}\n"
        << "\n"
        << "// The initial state. initialEntry is the initial state's own entry clause, if it has\n"
        << "// one: nothing runs it — the machine is being created, not entered (ENTRY_EXIT.md).\n"
        << "export const initial: State = " << '"' << machine.initial << '"' << ";\n";

    std::vector<std::string> initial_entry;
    const std::string initial_enter = enter_of(state_actions, machine.initial);
    if (!initial_enter.empty())
        initial_entry.push_back(initial_enter);
    out << "export const initialEntry: readonly Action[] = " << literal_list(initial_entry) << ";\n"
        << "\n"
        << R"(type Op = "eq" | "lt" | "le" | "gt" | "ge";)" << "\n"
        << "\n"
        << "interface Row {\n"
        << "  from: State;\n"
        << "  kind: Kind;\n"
        << "  to: State;\n"
        << "  refined: { op: Op; value: number } | null; // null = the unguarded row of the pair\n"
        << "  actions: readonly Action[]; // the composed calls, in order\n"
        << "}\n"
        << "\n"
        << "// from + kind -> the rows for that pair, in canonical order. A Map of Maps, not an\n"
        << "// object literal: a state named __proto__ is a prototype key in an object literal "
           "and\n"
        << "// its rows would vanish; a Map keys by the value, whatever the value is.\n"
        << "const table: ReadonlyMap<State, ReadonlyMap<Kind, readonly Row[]>> = new Map<\n"
        << "  State,\n"
        << "  ReadonlyMap<Kind, readonly Row[]>\n"
        << ">([\n";
    for (const std::string& state : machine.states) {
        std::vector<const fsmtable::Transition*> from;
        for (const fsmtable::Transition* row : rows) {
            if (row->from == state)
                from.push_back(row);
        }
        if (from.empty())
            continue;
        std::vector<int> from_kinds;
        for (const fsmtable::Transition* row : from) {
            if (std::find(from_kinds.begin(), from_kinds.end(), row->kind) == from_kinds.end())
                from_kinds.push_back(row->kind);
        }
        out << "  [" << '"' << state << '"' << ", new Map<Kind, readonly Row[]>([\n";
        for (const int kind : from_kinds) {
            out << "    [Kind." << kind_member(kind, kind_names) << ", [\n";
            for (const fsmtable::Transition* row : from) {
                if (row->kind != kind)
                    continue;
                out << "      { from: " << '"' << row->from << '"' << ", kind: Kind."
                    << kind_member(row->kind, kind_names) << ", to: " << '"' << row->to << '"'
                    << ", refined: ";
                if (row->has_when) {
                    out << "{ op: " << '"' << op_code(row->when_op) << '"'
                        << ", value: " << row->when_value << " }";
                } else {
                    out << "null";
                }
                out << ", actions: " << literal_list(composed(*row)) << " },\n";
            }
            out << "    ]],\n";
        }
        out << "  ])],\n";
    }
    out << "]);\n"
        << "\n"
        << "// The five comparisons a when clause can carry, in the format's own words.\n"
        << "const comparators: Readonly<Record<Op, (value: number, actual: number) => boolean>> = "
           "{\n"
        << "  eq: (value, actual) => actual === value,\n"
        << "  lt: (value, actual) => actual < value,\n"
        << "  le: (value, actual) => actual <= value,\n"
        << "  gt: (value, actual) => actual > value,\n"
        << "  ge: (value, actual) => actual >= value,\n"
        << "};\n"
        << "\n"
        << "// The whole of the machine's behaviour: the 2D lookup, then first-match-wins over "
           "the\n"
        << "// rows for that pair. Pure — it reads the table and the event and returns what to "
           "run;\n"
        << "// the caller holds the state and owns the actions.\n"
        << "export function step(state: State, event: Event): Step {\n"
        << "  const rows = table.get(state)?.get(event.kind);\n"
        << "  if (rows) {\n"
        << "    for (const row of rows) {\n"
        << "      if (row.refined === null ||\n"
        << "          comparators[row.refined.op](row.refined.value, event.value)) {\n"
        << "        return { state: row.to, fired: true, actions: row.actions };\n"
        << "      }\n"
        << "    }\n"
        << "  }\n"
        << "  return { state, fired: false, actions: [] };\n"
        << "}\n";
}

// ------------------------------------------------------------------------------- the checks

// Every name in the file becomes a C++ identifier, so each one is checked before anything is
// emitted: the message names the offender and the line-free name, which is what a user needs to
// fix the .fsm.
bool check_names(const fsmtable::Machine& machine,
                 const std::vector<fsmtable::KindName>& kind_names,
                 const std::vector<fsmtable::StateAction>& state_actions, std::string& complaint) {
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
    for (const fsmtable::KindName& entry : kind_names) {
        if (is_cpp_keyword(entry.name)) {
            complaint = "the kind name '" + entry.name + "' is a C++ keyword";
            return false;
        }
    }
    for (const fsmtable::StateAction& entry : state_actions) {
        const std::vector<std::string> clauses{entry.enter, entry.exit};
        for (const std::string& action : clauses) {
            if (!action.empty() && is_cpp_keyword(action)) {
                complaint = "the entry or exit action '" + action + "' is a C++ keyword";
                return false;
            }
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

    std::string target = "cpp";

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << usage();
            return 0;
        }
        if (arg == "-o" || arg == "--namespace" || arg == "--target") {
            if (i + 1 >= argc) {
                std::cerr << "fsmtable-gen: " << arg << " needs an argument\n";
                return 2;
            }
            const std::string value = argv[++i];
            if (arg == "-o")
                output = value;
            else if (arg == "--target")
                target = value;
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

    if (target != "cpp" && target != "deno") {
        std::cerr << "fsmtable-gen: unknown --target '" << target << "' (cpp or deno)\n";
        return 2;
    }
    if (input.empty()) {
        std::cerr << usage();
        return 2;
    }
    if (namespace_given) {
        if (target == "deno") {
            std::cerr << "fsmtable-gen: --namespace does not apply to --target deno: one module "
                         "per file, imported under whatever name the caller likes\n";
            return 2;
        }
        if (!is_identifier(space) || is_cpp_keyword(space)) {
            std::cerr << "fsmtable-gen: --namespace '" << space << "' is not a usable identifier\n";
            return 2;
        }
    }

    std::string text;
    if (!read_file(input, text)) {
        std::cerr << "fsmtable-gen: cannot read " << input << "\n";
        return 1;
    }

    fsmtable::Error error{0, ""};
    std::vector<fsmtable::KindName> kind_names;
    std::vector<fsmtable::StateAction> state_actions;
    const auto machine = fsmtable::parse(text, error, kind_names, state_actions);
    if (!machine.has_value()) {
        std::cerr << "fsmtable-gen: " << input << ":" << error.line << ": " << error.message
                  << "\n";
        return 1;
    }

    // The C++ target's names are checked here: each becomes an enumerator, a function or a type
    // name, so a keyword (or a 65536th state, which the uint16_t enum cannot hold) is refused
    // rather than emitted. The TypeScript target has no such names to defend — every state,
    // action and kind becomes a string literal or an enum member, and a reserved word is a legal
    // enum member (measured: `enum Kind { class = 1 }` compiles) — so the check stays where the
    // rule lives rather than refusing input the deno back end handles correctly.
    std::string complaint;
    if (target == "cpp" && !check_names(*machine, kind_names, state_actions, complaint)) {
        std::cerr << "fsmtable-gen: " << input << ": " << complaint << "\n";
        return 1;
    }

    Names names;
    names.space = space;
    names.prefix = machine->name;

    // The fingerprint covers the named canonical form: renaming a kind changes the emitted code,
    // so it has to change this too. A file that declares no names hashes exactly what it always
    // hashed, because the two dumps are the same text.
    const std::uint64_t canonical_fingerprint
        = fingerprint(fsmtable::dump(*machine, kind_names, state_actions));
    std::ostringstream rendered_stream;
    if (target == "deno")
        emit_deno(rendered_stream, *machine, kind_names, state_actions, input,
                  canonical_fingerprint);
    else
        emit_header(rendered_stream, names, *machine, kind_names, state_actions, input,
                    canonical_fingerprint);
    const std::string rendered = rendered_stream.str();

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