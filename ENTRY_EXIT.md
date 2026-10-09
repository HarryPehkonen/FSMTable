# Entry and exit actions — the second addition to the reader

SPEC.md section 2 is frozen, and rule 1 says a state is declared by its first appearance: there
is no `state` declaration. This adds a way to *decorate* a state with actions that run when a
transition leaves it and when one arrives — no frozen signature changed, in the same shape as
named kinds (NAMED_KINDS.md).

## The syntax

    # both clauses
    state Idle entry on_entry_idle exit on_exit_idle
    # entry only
    state Running entry on_entry_running
    # exit only
    state Error exit on_exit_error

Rules:

1. `state <name>`, then at least one clause. `entry` before `exit`, no other order, no other
   field anywhere on the line.
2. Each clause appears at most once and names exactly one action.
3. Two lines for one state merge, as long as no clause repeats: `state A entry x` and
   `state A exit y` are one decoration written twice.
4. The state must be one the machine has — a name on a `transition` (either end) or the
   `initial` line. A decoration for any other name is refused. The name pattern cannot catch a
   misspelt state, so the parse does it at the end, reporting the line the decoration is on.
5. A decoration does not declare a state and does not create a transition. It cannot make a
   machine do anything its rows did not already do.
6. Clause names are action names: the same pattern, and the generator refuses C++ keywords for
   them exactly as it does for row actions.

The file is still `version 1`. Like a kind name, this is another spelling for something the
frozen format already had room for, not a second format.

## What it means

A transition that fires runs, in order:

    the source state's exit, then the row's own action, then the target state's entry

That is the statechart order, and the only order that needs no cooperation from the row. Two
readings worth recording (QUESTIONS.md Q13, Q14):

* **A row that stays put is an external transition.** The machine leaves the state and enters it
  again, so both clauses run. Version 1 has no way to express "internal transition" — a row that
  names a `to` is the only way to say "stay" — and the row *does* name a target, so this is the
  simplest reading (SPEC.md section 0) as well as the one a reader of the file will expect.
* **The initial state's entry action is not called by the generated factory.** The compiled back
  end's `setInitialState` is not a transition and runs nothing; a factory that fired an action
  would be a side effect hidden inside a constructor. The generator emits the clause as a
  function instead, and nothing calls it:

      inline void enterInitialIdle(const IdleEvent& event = {}) { on_entry_idle(event); }

  Call it once after `makeIdle()` if that state needs it. The event is a placeholder: there is
  no event, the machine is being created.

* A row that does not fire runs nothing: the clauses belong to transitions, not to time.
* Clause actions are actions in the ordinary sense — declared by the generated header, defined
  by the caller, keyword-checked. A `.fsm` that decorates a state with a name nobody defines is
  a link error like any other, which is the point of resolving names at generation time.

## What the generator emits

The back end runs exactly one function per transition, so the generator composes the three calls
into one function and points the row at it:

    inline void Closed_leaving_to_Open(const DoorEvent& event) {
        on_exit_closed(event);
        on_opening(event);
        on_entry_open(event);
    }

* The name is `<from>_leaving_to_<to>`; a second row with the same two ends gets `_2`, `_3`, …
* Rows that compose the same three calls **share one function**. The calculator's seven `Clear`
  rows all point at `Accum_leaving_to_Entry`, the name of the first row that needed it.
* A row whose ends carry no clause still points straight at its own action — which is why every
  artifact that uses no entry or exit is byte for byte what it was before this addition. The
  test that holds the traffic light's single action to `&on_red_green` is that guarantee: a
  composed row would point somewhere else and fail it.

## The artifact, in full

    enum class <M>State / <M>Kind          the states and the kinds
    struct <M>Event { <M>Kind kind; int value; }
    void <action>(const <M>Event&);        one per row action AND per clause
    <from>_leaving_to_<to>(const <M>Event&)  the composed function, only when something composes
    enterInitial<M>(const <M>Event& = {})  the initial state's entry action, called by nobody
    <M>StateNames / <M>NameOf              names for logs and round trips (NAMED_KINDS.md)
    <M>Rows                                the table, canonical order
    make<M>()                              one machine, ready to drive

## The canonical form

The clauses are in it, so changing an entry action changes the generated header's fingerprint:

    std::optional<Machine> parse(text, error, kind_names, state_actions);
    std::string dump(const Machine&, const std::vector<KindName>&,
                     const std::vector<StateAction>&);

`StateAction` is `{state, enter, exit}`, with an empty string for a clause the file did not write.
Both functions are below the frozen block; `parse(text, error, kind_names)` delegates to the
four-argument form and throws the decorations away, and `dump(machine)` is still numbers only.
Like a kind name, a decoration is resolved while parsing and `Machine` does not record it.

The dump writes the state lines sorted **by state name**, after the kind declarations and before
the rows. By name rather than in the machine's own state order, because a canonical round trip
rebuilds that order (QUESTIONS.md Q7) and the dump has to be stable across one.

## What it does not do

* No action at machine creation (above) and none when a row fails to match.
* No internal-transition distinction (Q13).
* No per-pair ordering: the clauses belong to states, not to a source/target combination.
* No separate event: a clause action receives the event that fired the row, like any other.
* The interpreted back end (`fsmgine::FSM`) has no notion of entry or exit, so the differential
  corpus — which decorates nothing — stays the one place the two back ends are compared. A
  machine that uses the clauses is outside what a plain reading of the rows predicts, which is
  why the generator's own tests drive the Door fixture directly instead of through that harness.
* No FSMgine change was needed, and none was made: the composition happens at generation time,
  in the generated header. That is the whole reason this one could be built without the two
  libraries having to move in lockstep.
