// fsmTable — the libFuzzer target for stage A.
//
// The oracle is the format's own promise: `parse` is total, an error never carries a
// partial machine, and a machine that parsed survives dump -> parse -> dump unchanged.
// Those three checks are the whole target; the corpus in corpus/ is the seed.
#include "fsmtable.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

[[noreturn]] void broken(const char* what) {
    // libFuzzer turns the abort into a crashing input plus an artifact, which is the
    // only useful way to report a violation from here.
    std::fputs("fsmtable: ", stderr);
    std::fputs(what, stderr);
    std::fputs("\n", stderr);
    std::abort();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // libFuzzer hands over bytes, and the format is bytes. Converting them one at a time
    // keeps this a value conversion: no reinterpret_cast and no cast through void, the
    // two things the Core Guidelines actually forbid. The copy costs nothing here — the
    // parser it feeds allocates for every machine it builds.
    std::string owned;
    owned.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        owned.push_back(static_cast<char>(data[i]));
    }
    const std::string_view text(owned);

    std::size_t lines = 1;
    for (const char c : text) {
        if (c == '\n')
            ++lines;
    }

    fsmtable::Error error{0, ""};
    const auto machine = fsmtable::parse(text, error);

    if (!machine.has_value()) {
        // The error contract, checked on every rejection the fuzzer can invent.
        if (error.message.empty())
            broken("rejected an input with an empty message");
        if (error.line < 0)
            broken("rejected an input with a negative line number");
        if (static_cast<std::size_t>(error.line) > lines) {
            broken("rejected an input with a line number past the end of the text");
        }
        return 0;
    }

    // It parsed, so the canonical form must exist, parse again, and be a fixed point.
    const std::string once = fsmtable::dump(*machine);
    fsmtable::Error again_error{0, ""};
    const auto again = fsmtable::parse(once, again_error);
    if (!again.has_value()) {
        broken("a machine that parsed produced a dump that does not parse");
    }
    if (fsmtable::dump(*again) != once) {
        broken("dump -> parse -> dump is not stable");
    }
    return 0;
}
