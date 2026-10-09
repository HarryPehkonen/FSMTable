// The Edge example's driver: what a byte is — two values out of 256 are bits — the two lines the
// actions can print, and the refusal for a byte that is neither. The machine decides what sequence
// of bits is an edge; this file decides everything the table cannot hold, which here is almost
// nothing, and that is this example's point.
#include "edge.hpp"

#include <string>

namespace {

// The actions reach the run's reader through this pointer, installed by Reader::run for its
// duration. The compiled back end's action type is void (*)(const Event&) — no context parameter —
// so a caller-owned pointer is the only route (QUESTIONS.md Q11). The check is right in general and
// wrong here, exactly as it is for the calculator's registers and the csv reader's buffers: the
// alternative to this pointer is not a const one, it is not having the feature at all.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
edge::Reader* g_reader = nullptr;

// How a refused byte is described to the person who fed it. The machine has no row and no kind for
// this byte, so it cannot explain the refusal and the explanation is this file's (QUESTIONS.md Q10,
// Q27). A printable byte is shown as the character it is and anything else by its value, so the
// newline a shell adds to `echo 01` reads as `0x0a` rather than as a blank.
std::string describe(unsigned char byte) {
    if (byte >= 0x20 && byte < 0x7f)
        return std::string("'") + static_cast<char>(byte) + "'";

    // A std::string rather than a `const char*`: indexing a string is a call, while indexing a
    // pointer is pointer arithmetic to clang-tidy's cppcoreguidelines check — the same finding the
    // repository records for `argv[i]` rather than writes a cast around (`.ci/tidy-baseline.txt`).
    static const std::string digits = "0123456789abcdef";
    std::string hex = "0x";
    hex.push_back(digits[static_cast<std::size_t>(byte >> 4)]);
    hex.push_back(digits[static_cast<std::size_t>(byte & 0x0f)]);
    return hex;
}

} // namespace

namespace edge {

void install(Reader* reader) { g_reader = reader; }

Reader& reader() { return *g_reader; }

Reader::Reader() : machine_(fsmtable_generated::makeEdge()) { g_reader = this; }

std::string_view Reader::state_name() const { return machine_.currentStateName(); }

void Reader::rising() { out_.push_back("0 --> 1"); }

void Reader::falling() { out_.push_back("1 --> 0"); }

Result Reader::run(std::string_view bytes) {
    Result result;
    machine_ = fsmtable_generated::makeEdge();
    out_.clear();
    install(this);

    std::size_t i = 0;
    while (i < bytes.size()) {
        const char c = bytes[i];
        ++i; // the byte this event is about, 1-based

        // The driver's whole vocabulary: two values out of 256 are bits. There is no third kind and
        // none is invented — a byte that is neither is reported rather than guessed at, and the run
        // stops where the machine would have had nothing to say (QUESTIONS.md Q27).
        EventKind kind = EventKind::zero;
        if (c == '0') {
            kind = EventKind::zero;
        } else if (c == '1') {
            kind = EventKind::one;
        } else {
            result.ok = false;
            result.offset = i;
            result.error = "byte " + std::to_string(i) + ": "
                           + describe(static_cast<unsigned char>(c)) + " is not a bit";
            result.lines = out_;
            install(nullptr);
            return result;
        }

        // The machine is total over its two kinds — every state has a row for `zero` and for `one`
        // (edge.fsm) — so process() cannot refuse anything. There is no machine-refusal path in
        // this example, which is why the return value is not read; the tests pin the claim that
        // there is nothing for it to report.
        machine_.process(Event{kind});
    }

    install(nullptr);
    result.lines = out_;
    return result;
}

} // namespace edge

// The actions the generated header declares. A name the .fsm uses and this file does not define is
// a link error, so the two cannot drift apart quietly.
namespace fsmtable_generated {

void rising(const EdgeEvent& /*event*/) { edge::reader().rising(); }

void falling(const EdgeEvent& /*event*/) { edge::reader().falling(); }

} // namespace fsmtable_generated
