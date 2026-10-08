// FSMTable — declarations for the format's vocabulary (implementation detail, never public).
//
// These helpers had internal linkage in an anonymous namespace inside src/fsmtable.cpp. Splitting
// that file (2026-10-06) needs them named across translation units, so they live in
// fsmtable::detail. Nothing in include/ mentions them and no public header changed; ArrowKind and
// the three limits moved here verbatim.

#ifndef FSMTABLE_DETAIL_HPP
#define FSMTABLE_DETAIL_HPP

#include "fsmtable.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsmtable {
namespace detail {

enum class KindSlot : std::uint8_t { Number, Name, Malformed };

// The arrow token of a transition row.
struct ArrowKind {
    KindSlot what = KindSlot::Malformed;
    int number = 0;        // when what == Number
    std::string_view name; // when what == Name
};

constexpr std::size_t kMaxNameLength = 64;
constexpr long long kIntMin = -2147483648LL;
constexpr long long kIntMax = 2147483647LL;

std::string_view trim(std::string_view s);
std::vector<std::string_view> split_fields(std::string_view line);
// The format's idea of whitespace. It was file-private in tokens.cpp; transform.cpp splits a line
// into fields WITH their offsets, and a second predicate could drift from this one and make the
// two views of the same line disagree about where a field begins.
bool is_space(char c);
bool valid_name(std::string_view name);
bool parse_bounded_int(std::string_view token, long long lo, long long hi, long long& out);
ArrowKind read_arrow_kind(std::string_view token);
bool parse_op(std::string_view token, Op& op);
const KindName* find_kind_name(const std::vector<KindName>& declared, std::string_view name);
const KindName* find_kind_number(const std::vector<KindName>& declared, int kind);
std::string_view op_text(Op op);
void declare_state(Machine& machine, std::string_view name);
std::optional<Machine> reject(Error& error, int line, std::string message);
std::string quoted(std::string_view token);

} // namespace detail
} // namespace fsmtable

#endif // FSMTABLE_DETAIL_HPP
