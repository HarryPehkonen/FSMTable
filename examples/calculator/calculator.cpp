// The support code the machine names: the tokeniser, the line driver, and one action per thing
// the .fsm asks for. The split follows the machine's own contract — the table decides what
// sequence is legal, and nothing else does. The numbers live here, because no row can hold one.
#include "calculator.hpp"

#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

// The actions reach the registers through this pointer, installed by Shell::run_line for the
// duration of one line and cleared afterwards. The compiled back end's action type is
// void (*)(const Event&) — no context parameter — so this is the only route (QUESTIONS.md Q11).
// The check is right in general and wrong here: the alternative to this pointer is not a
// const one, it is not having the feature at all.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
calc::Registers* g_registers = nullptr;

// The machine names its events by number (GENERATOR.md, "Not here yet", item 1). Naming them
// once, here, is the entire cost of that in this example.
constexpr auto kNumber = fsmtable_generated::CalculatorKind::k1;
constexpr auto kAdd = fsmtable_generated::CalculatorKind::k2;
constexpr auto kSub = fsmtable_generated::CalculatorKind::k3;
constexpr auto kMul = fsmtable_generated::CalculatorKind::k4;
constexpr auto kDiv = fsmtable_generated::CalculatorKind::k5;
constexpr auto kEquals = fsmtable_generated::CalculatorKind::k6;
constexpr auto kClear = fsmtable_generated::CalculatorKind::k7;

fsmtable_generated::CalculatorEvent event_for(fsmtable_generated::CalculatorKind kind, int value) {
    fsmtable_generated::CalculatorEvent event{};
    event.kind = kind;
    event.value = value;
    return event;
}

// How a refused token is described to the person who typed it.
std::string describe(const calc::Token& token) {
    switch (token.first) {
    case kNumber:
        return "number " + std::to_string(token.second);
    case kAdd:
        return "operator +";
    case kSub:
        return "operator -";
    case kMul:
        return "operator *";
    case kDiv:
        return "operator /";
    case kEquals:
        return "end of line";
    default:
        return "token";
    }
}

// The one place the arithmetic happens: the four *_operand actions differ only in the operator
// they name. They are four one-line actions rather than one that switches on the event's kind
// because the format has no action parameters (v2 item 3) and because the .fsm should read like
// the grammar.
void apply(fsmtable_generated::CalculatorKind op, int operand) {
    calc::Registers& registers = calc::registers();
    if (!registers.error.empty()) {
        return; // a line that has already failed stays failed
    }

    long long result = 0;
    bool overflow = false;
    switch (op) {
    case kAdd:
        overflow
            = __builtin_add_overflow(registers.value, static_cast<long long>(operand), &result);
        break;
    case kSub:
        overflow
            = __builtin_sub_overflow(registers.value, static_cast<long long>(operand), &result);
        break;
    case kMul:
        overflow
            = __builtin_mul_overflow(registers.value, static_cast<long long>(operand), &result);
        break;
    case kDiv:
        if (operand == 0) {
            // The machine routes a zero divisor to Error before this can run, so this is the
            // arithmetic layer's own invariant rather than the format's (QUESTIONS.md Q10).
            registers.error = "division by zero";
            return;
        }
        result = registers.value / static_cast<long long>(operand);
        break;
    default:
        return;
    }

    if (overflow) {
        // Not a row, and it cannot be: a guard sees the event, and the product is not in it.
        registers.error = "overflow";
        return;
    }
    registers.value = result;
}

} // namespace

namespace calc {

void install(Registers* registers) { g_registers = registers; }

Registers& registers() { return *g_registers; }

bool tokenise(std::string_view line, std::vector<Token>& tokens, std::string& error) {
    tokens.clear();
    constexpr auto kLargestOperand
        = static_cast<unsigned long long>(std::numeric_limits<int>::max());
    std::size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c >= '0' && c <= '9') {
            // Index-based scanning, like the library's own integer scanner: pointer arithmetic
            // is a clang-tidy finding in this repo (.ci/tidy-baseline.txt).
            unsigned long long value = 0;
            bool too_large = false;
            std::size_t end = i;
            while (end < line.size() && line[end] >= '0' && line[end] <= '9') {
                const auto digit = static_cast<unsigned long long>(line[end] - '0');
                if (too_large || value > (kLargestOperand - digit) / 10U) {
                    too_large = true;
                } else {
                    value = value * 10U + digit;
                }
                ++end;
            }
            if (too_large) {
                error = "number out of range";
                return false;
            }
            tokens.emplace_back(kNumber, static_cast<int>(value));
            i = end;
            continue;
        }
        switch (c) {
        case '+':
            tokens.emplace_back(kAdd, 0);
            break;
        case '-':
            tokens.emplace_back(kSub, 0);
            break;
        case '*':
            tokens.emplace_back(kMul, 0);
            break;
        case '/':
            tokens.emplace_back(kDiv, 0);
            break;
        default:
            error = "unexpected character '" + std::string(1, c) + "' at column "
                    + std::to_string(i + 1);
            return false;
        }
        ++i;
    }
    return true;
}

Shell::Shell() : machine_(fsmtable_generated::makeCalculator()) {}

std::string_view Shell::state_name() const { return machine_.currentStateName(); }

Outcome Shell::run_line(std::string_view line) {
    std::vector<Token> tokens;
    std::string failure;
    const bool readable = tokenise(line, tokens, failure);
    const bool blank = readable && tokens.empty();

    install(&registers_);
    // Clear runs at the START of a line rather than at the end of the previous one,
    // so the state the machine is left in is the state this line reached — which is
    // what the tests (and anyone embedding this) can observe.
    machine_.process(event_for(kClear, 0));

    if (readable && !blank) {
        for (const Token& token : tokens) {
            if (!machine_.process(event_for(token.first, token.second))) {
                // The machine has no row here: the sequence is not in its language, and saying
                // so is the shell's job — the table never explains a refusal (QUESTIONS.md Q10).
                failure = "unexpected " + describe(token);
                break;
            }
        }
        if (failure.empty() && !machine_.process(event_for(kEquals, 0))) {
            failure = "incomplete expression"; // `12 +`: Equals has no row in a Pending* state
        }
    }

    Outcome outcome;
    if (!readable || !failure.empty()) {
        outcome.kind = Outcome::Kind::Error;
        outcome.message = failure;
    } else if (!blank) {
        if (!registers_.error.empty()) {
            outcome.kind = Outcome::Kind::Error;
            outcome.message = registers_.error;
        } else if (registers_.has_value) {
            outcome.kind = Outcome::Kind::Value;
            outcome.value = registers_.value;
        }
    }

    install(nullptr);
    return outcome;
}

} // namespace calc

// The actions the generated header declares. A name the .fsm uses and this file does not define
// is a link error, so the two cannot drift apart quietly.
namespace fsmtable_generated {

void take_operand(const CalculatorEvent& event) {
    calc::Registers& registers = calc::registers();
    registers.value = event.value;
    registers.has_value = true;
}

void add_operand(const CalculatorEvent& event) { apply(kAdd, event.value); }

void sub_operand(const CalculatorEvent& event) { apply(kSub, event.value); }

void mul_operand(const CalculatorEvent& event) { apply(kMul, event.value); }

void div_operand(const CalculatorEvent& event) { apply(kDiv, event.value); }

void note_division_by_zero(const CalculatorEvent& /*event*/) {
    calc::registers().error = "division by zero";
}

void clear_all(const CalculatorEvent& /*event*/) {
    calc::Registers& registers = calc::registers();
    registers.value = 0;
    registers.has_value = false;
    registers.error.clear();
}

} // namespace fsmtable_generated