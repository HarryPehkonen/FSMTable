// The CSV example's driver: what a byte is, that CRLF is one line end, what a record left open at
// end of input means, and one definition per action csv.fsm names. The machine decides what
// sequence of bytes is legal; this file decides everything the table cannot hold.
#include "csv.hpp"

#include <string>

namespace {

// The actions reach the run's reader through this pointer, installed by Reader::run for its
// duration. The compiled back end's action type is void (*)(const Event&) — no context parameter —
// so a caller-owned pointer is the only route (QUESTIONS.md Q11). The check is right in general and
// wrong here, exactly as it is for the calculator's registers: the alternative to this pointer is
// not a const one, it is not having the feature at all.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
csv::Reader* g_reader = nullptr;

// How a refused byte is described to the person who fed it: the state it arrived in and the kind
// of byte it was, both named the way the .fsm names them. The table never explains a refusal, so
// the explanation is this file's (QUESTIONS.md Q10, and the README's "The refusal").
std::string describe(std::string_view state, csv::EventKind kind) {
    return std::string(state) + " has no row for a "
           + std::string(fsmtable_generated::CsvKindNameOf(kind)) + " byte";
}

} // namespace

namespace csv {

void install(Reader* reader) { g_reader = reader; }

Reader& reader() { return *g_reader; }

Reader::Reader() : machine_(fsmtable_generated::makeCsv()) { g_reader = this; }

std::string_view Reader::state_name() const { return machine_.currentStateName(); }

void Reader::append(char byte) { buffer_.push_back(byte); }

void Reader::end_cell() {
    out_.push_back("Cell: " + buffer_);
    buffer_.clear();
}

void Reader::end_record() {
    end_cell(); // a line end closes the field the last comma did not (QUESTIONS.md Q24)
    out_.push_back("NEW LINE");
    dirty_ = false; // nothing is pending, so end of input has nothing to flush (Q25)
}

Result Reader::run(std::string_view bytes) {
    Result result;
    machine_ = fsmtable_generated::makeCsv();
    buffer_.clear();
    out_.clear();
    dirty_ = false;
    install(this);

    std::size_t i = 0;
    while (i < bytes.size()) {
        const char c = bytes[i];
        ++i; // the byte this event is about, 1-based

        EventKind kind = EventKind::data; // everything unforeseen is data, a lone \r included
        if (c == '\r' && i < bytes.size() && bytes[i] == '\n') {
            ++i; // CRLF is one line end; it is the driver's rule that the file's two bytes are one
            kind = EventKind::newline;
        } else if (c == '\n') {
            kind = EventKind::newline;
        } else if (c == ',') {
            kind = EventKind::comma;
        } else if (c == '"') {
            kind = EventKind::quote;
        }
        // The byte rides on the event, which is what lets one `append` serve every row that
        // collects a character. Every kind carries it, so the escape row — QuoteSeen --quote-->
        // Quoted, whose `append` must add a literal quote — needs no special case at all.

        dirty_ = true;
        const std::string_view from = machine_.currentStateName();
        const int value = static_cast<int>(static_cast<unsigned char>(c));
        if (!machine_.process(Event{kind, value})) {
            result.ok = false;
            result.offset = i;
            result.error = "byte " + std::to_string(i) + ": " + describe(from, kind);
            result.lines = out_;
            install(nullptr);
            return result;
        }
    }

    // End of input is not an event, so the table has nothing to say about it (Q25). Two things it
    // can be: a quoted field that never closed, which is malformed and refused here, and a last
    // record with no trailing line end, which is closed and reported like any other.
    if (machine_.getCurrentState() == fsmtable_generated::CsvState::Quoted) {
        result.ok = false;
        result.error = "end of input inside a quoted field";
        result.lines = out_;
        install(nullptr);
        return result;
    }
    if (dirty_)
        end_record();

    install(nullptr);
    result.lines = out_;
    return result;
}

} // namespace csv

// The actions the generated header declares. A name the .fsm uses and this file does not define is
// a link error, so the two cannot drift apart quietly.
namespace fsmtable_generated {

void append(const CsvEvent& event) { csv::reader().append(static_cast<char>(event.value)); }

void end_cell(const CsvEvent& /*event*/) { csv::reader().end_cell(); }

void end_record(const CsvEvent& /*event*/) { csv::reader().end_record(); }

} // namespace fsmtable_generated
