// The CSV example's API: a byte stream in, one line per cell and one per record out. main.cpp owns
// the streams; this owns the machine and the small amount of driver-side bookkeeping the format
// cannot hold — what each byte IS, that CRLF is one line end, and what to do with a record left
// open at end of input. Everything that decides anything is reachable from a test without a pipe.
#pragma once

#include "fsm_csv.hpp" // generated at build time from csv.fsm

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace csv {

// The generated types under the names this example uses for them. `EventKind` and its enumerators
// come straight from csv.fsm's own `kind` declarations, so nothing here has to be kept in step with
// the machine by hand (NAMED_KINDS.md).
using EventKind = fsmtable_generated::CsvKind;
using Event = fsmtable_generated::CsvEvent;

// What one run of the reader produced.
struct Result {
    // false: a byte had no row in the state it arrived in — a byte after a closing quote, or end of
    // input inside a quoted field. Both are the driver's report of the machine refusing, and the
    // run stops at the first one (README.md, "The refusal").
    bool ok = true;

    // The 1-based byte offset a refused byte sat at; 0 when ok, and 0 when the refusal is not tied
    // to a byte (end of input).
    std::size_t offset = 0;

    // The driver's message for a refusal; empty when ok.
    std::string error;

    // The output, in order: one `Cell: <text>` per cell and one `NEW LINE` per record. An empty
    // cell is `Cell: ` with nothing after the space, and a cell holding a quoted line end really
    // does break the line — the text is what the machine collected, not a rendering of it.
    std::vector<std::string> lines;
};

// The driver: the machine, the field being assembled, and the output so far.
class Reader {
public:
    Reader();

    // Reads a whole byte stream. Classifies each byte, collapses CRLF to one line end, feeds the
    // machine, and closes a record left open at end of input. The classification, the collapse and
    // the flush are the driver's, not the table's (README.md; QUESTIONS.md Q21 and Q25).
    Result run(std::string_view bytes);

    // The machine's current state, exposed because it is the machine's own invariant and the tests
    // assert on it (for example, that a refused byte left the machine where it was).
    std::string_view state_name() const;

    // The three actions csv.fsm names, one definition each in csv.cpp. The compiled back end's
    // action type is void (*)(const Event&) — no context parameter — so they reach the run's reader
    // through a pointer installed for its duration (QUESTIONS.md Q11). `append` collects the byte
    // the event carried; `end_cell` closes the field a comma ended; `end_record` closes the field
    // AND the record a line end ended.
    void append(char byte);
    void end_cell();
    void end_record();

private:
    fsmgine::compiled::Machine<fsmtable_generated::CsvState, Event> machine_;
    std::string buffer_;           // the field being assembled
    std::vector<std::string> out_; // what the actions have produced
    bool dirty_ = false;           // a byte has been read since the last record ended
};

// The run's reader, reached by the actions. `run` installs it; a reader that is not running is
// nobody's (the calculator's registers work the same way).
void install(Reader* reader);
Reader& reader();

} // namespace csv
