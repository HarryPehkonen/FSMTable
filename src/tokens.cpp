// FSMTable — the format's vocabulary: character classes, tokens, bounded integers, arrows, guards.
//
// Split out of the single 754-line src/fsmtable.cpp on 2026-10-06. That file held four concerns --
// the format's vocabulary, the parser, the writer, the graph analysis -- and a reader looking for
// one had to walk past the other three. No line of logic changed: the bodies below are the same
// text, moved. The one structural change is the namespace: helpers that had internal linkage in
// anonymous namespaces are now named in fsmtable::detail (declared in fsmtable_detail.hpp) so a
// file boundary can reach them. include/ is untouched and no public header changed.

#include "fsmtable_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fsmtable {
namespace detail {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

bool is_ident_start(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }

bool is_ident_char(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }

bool is_digit(char c) { return c >= '0' && c <= '9'; }

std::string_view trim(std::string_view s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && is_space(s[begin]))
        ++begin;
    while (end > begin && is_space(s[end - 1]))
        --end;
    return s.substr(begin, end - begin);
}

// Fields are separated by one or more whitespace characters. A number split by
// whitespace therefore arrives here as two fields, which is how `-- 1 -->` and
// `when eq 1 2` are rejected rather than leniently re-joined.
std::vector<std::string_view> split_fields(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && is_space(line[i]))
            ++i;
        if (i >= line.size())
            break;
        const std::size_t start = i;
        while (i < line.size() && !is_space(line[i]))
            ++i;
        fields.push_back(line.substr(start, i - start));
    }
    return fields;
}

bool valid_name(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameLength)
        return false;
    if (!is_ident_start(name.front()))
        return false;
    for (const char c : name) {
        if (!is_ident_char(c))
            return false;
    }
    return true;
}

// A decimal integer that is consumed entirely and fits [lo, hi], scanned here rather than
// with std::from_chars so that the rules are this file's own and the whole thing is
// index-based: a leading '-' is the only sign accepted (a leading '+' is an error, as the
// format says), every remaining character must be a digit, and a magnitude that would
// overflow long long is refused instead of wrapping.
bool parse_bounded_int(std::string_view token, long long lo, long long hi, long long& out) {
    if (token.empty())
        return false;

    std::size_t i = 0;
    const bool negative = token[0] == '-';
    if (negative)
        i = 1;
    if (i >= token.size())
        return false;

    constexpr unsigned long long kCeiling = 1ULL << 63; // |LLONG_MIN|
    unsigned long long magnitude = 0;
    for (; i < token.size(); ++i) {
        if (!is_digit(token[i]))
            return false;
        const unsigned digit = static_cast<unsigned>(token[i] - '0');
        if (magnitude > (kCeiling - digit) / 10ULL)
            return false; // would not fit
        magnitude = magnitude * 10ULL + digit;
    }

    long long value = 0;
    if (negative) {
        value = magnitude == kCeiling ? std::numeric_limits<long long>::min()
                                      : -static_cast<long long>(magnitude);
    } else {
        if (magnitude > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
            return false;
        }
        value = static_cast<long long>(magnitude);
    }
    if (value < lo || value > hi)
        return false;

    out = value;
    return true;
}

// The arrow slot. `--<digits>-->` as ONE token, so `-- 1 -->` stays an error rather than a
// lenient parse; or `--<name>-->` for a name a `kind` directive declared. Anything else — an
// empty slot, a leading `+`, an inner token that is neither all digits nor an identifier, a
// number out of range — is malformed, which is rule 4 unrelaxed.

ArrowKind read_arrow_kind(std::string_view token) {
    constexpr std::string_view prefix = "--";
    constexpr std::string_view suffix = "-->";
    if (token.size() <= prefix.size() + suffix.size())
        return {};
    if (token.substr(0, prefix.size()) != prefix)
        return {};
    if (token.substr(token.size() - suffix.size()) != suffix)
        return {};
    const std::string_view inner
        = token.substr(prefix.size(), token.size() - prefix.size() - suffix.size());

    ArrowKind slot;
    bool all_digits = true;
    for (const char c : inner) {
        if (!is_digit(c)) {
            all_digits = false;
            break;
        }
    }
    if (all_digits) {
        long long value = 0;
        if (!parse_bounded_int(inner, 0, 255, value))
            return {}; // rule 5: out of range, reported like any other malformed token
        slot.what = KindSlot::Number;
        slot.number = static_cast<int>(value);
        return slot;
    }
    if (!valid_name(inner))
        return {};
    slot.what = KindSlot::Name;
    slot.name = inner;
    return slot;
}

bool parse_op(std::string_view token, Op& op) {
    if (token == "eq") {
        op = Op::Eq;
        return true;
    }
    if (token == "lt") {
        op = Op::Lt;
        return true;
    }
    if (token == "le") {
        op = Op::Le;
        return true;
    }
    if (token == "gt") {
        op = Op::Gt;
        return true;
    }
    if (token == "ge") {
        op = Op::Ge;
        return true;
    }
    return false;
}

std::string_view op_text(Op op) {
    switch (op) {
    case Op::Eq:
        return "eq";
    case Op::Lt:
        return "lt";
    case Op::Le:
        return "le";
    case Op::Gt:
        return "gt";
    case Op::Ge:
        return "ge";
    }
    return "eq";
}

const KindName* find_kind_name(const std::vector<KindName>& declared, std::string_view name) {
    for (const KindName& entry : declared) {
        if (entry.name == name)
            return &entry;
    }
    return nullptr;
}

const KindName* find_kind_number(const std::vector<KindName>& declared, int kind) {
    for (const KindName& entry : declared) {
        if (entry.kind == kind)
            return &entry;
    }
    return nullptr;
}

void declare_state(Machine& machine, std::string_view name) {
    const auto known = std::find(machine.states.begin(), machine.states.end(), name);
    if (known == machine.states.end()) {
        machine.states.emplace_back(name);
    }
}

std::optional<Machine> reject(Error& error, int line, std::string message) {
    error.line = line;
    error.message = std::move(message);
    return std::nullopt;
}

std::string quoted(std::string_view token) { return "'" + std::string(token) + "'"; }

} // namespace detail
} // namespace fsmtable
