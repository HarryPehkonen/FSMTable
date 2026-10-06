// The protocol example's tests: directives in, what the machine did with them out. No pipe, no
// terminal, no clock — the clock is a value the script chooses, which is the point of the split.
#include "protocol.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using proto::Session;
using proto::Step;

// Feeds one directive and requires that the session could read it at all. A directive that the
// machine refuses is a normal outcome, not a failure of this helper.
Step step(Session& session, const std::string& directive) {
    Step result;
    std::string error;
    const bool readable = session.feed(directive, result, error);
    EXPECT_TRUE(readable) << "the directive '" << directive << "' is not readable: " << error;
    return result;
}

std::vector<std::string> ran(std::initializer_list<const char*> names) {
    std::vector<std::string> out;
    out.reserve(names.size());
    for (const char* name : names)
        out.push_back(name);
    return out;
}

} // namespace

TEST(Protocol, TheHandshakeRunsTheExitTheRowActionAndTheEntry) {
    Session session;

    const Step opened = step(session, "open");
    EXPECT_TRUE(opened.matched);
    EXPECT_EQ(opened.from, "Closed");
    EXPECT_EQ(opened.to, "SynSent");
    // Closed has no exit clause, so the row's action runs first and SynSent's entry last.
    EXPECT_EQ(opened.actions, ran({"on_open", "arm_timer"}));

    const Step acked = step(session, "syn_ack");
    EXPECT_TRUE(acked.matched);
    EXPECT_EQ(acked.from, "SynSent");
    EXPECT_EQ(acked.to, "Established");
    EXPECT_EQ(acked.actions, ran({"cancel_timer", "on_syn_ack"}));
}

TEST(Protocol, AQuietTickIsTheUnguardedHalfOfThePair) {
    Session session;
    step(session, "open");

    const Step quiet = step(session, "tick 999");
    EXPECT_TRUE(quiet.matched) << "the unguarded row absorbs a tick that has not expired";
    EXPECT_EQ(quiet.from, quiet.to);
    EXPECT_EQ(quiet.to, "SynSent");
    // The unguarded row has no action of its own, but it is still a self-transition, and a
    // self-transition is an external one: SynSent's exit and entry run (ENTRY_EXIT.md, Q13). A
    // driver with real timer work reads this as "put it in the row's action": a row fires once per
    // event, a state's clauses run every single time the state is entered or left.
    EXPECT_EQ(quiet.actions, ran({"cancel_timer", "arm_timer"}));
    EXPECT_EQ(session.retransmits(), 0);
}

TEST(Protocol, TheTimeoutIsInclusiveAtTheBoundary) {
    Session session;
    step(session, "open");

    const Step early = step(session, "tick 1000");
    EXPECT_EQ(early.actions, ran({"cancel_timer", "retransmit", "arm_timer"}))
        << "`ge 1000` includes 1000; the exit and entry around it are the self-transition's";
    EXPECT_EQ(session.retransmits(), 1);

    const Step late = step(session, "tick 4000");
    EXPECT_EQ(late.actions, ran({"cancel_timer", "retransmit", "arm_timer"}));
    EXPECT_EQ(session.retransmits(), 2);
    EXPECT_EQ(late.to, "SynSent");
}

TEST(Protocol, GivingUpLeavesThroughTheExitAndEntersRefused) {
    Session session;
    step(session, "open");
    step(session, "tick 1000");

    const Step gave_up = step(session, "expire");
    EXPECT_TRUE(gave_up.matched);
    EXPECT_EQ(gave_up.from, "SynSent");
    EXPECT_EQ(gave_up.to, "Refused");
    // All three parts: SynSent's exit, the row's action, Refused's entry.
    EXPECT_EQ(gave_up.actions, ran({"cancel_timer", "on_give_up", "on_refused"}));
}

TEST(Protocol, AResetWhileOpeningRefusesToo) {
    Session session;
    step(session, "open");

    const Step refused = step(session, "reset");
    EXPECT_EQ(refused.to, "Refused");
    EXPECT_EQ(refused.actions, ran({"cancel_timer", "on_refused_while_opening", "on_refused"}));
}

TEST(Protocol, RefusedIsWhereTheStoryEnds) {
    Session session;
    step(session, "open");
    step(session, "expire");

    const Step afterwards = step(session, "open");
    EXPECT_FALSE(afterwards.matched)
        << "nothing leaves Refused: a caller that wants to retry opens "
           "a new connection";
    EXPECT_EQ(afterwards.from, "Refused");
    EXPECT_EQ(afterwards.to, "Refused");
    EXPECT_TRUE(afterwards.actions.empty());
}

TEST(Protocol, DataInClosedIsNotInTheMachinesLanguage) {
    Session session;

    const Step refused = step(session, "data 64");
    EXPECT_FALSE(refused.matched);
    EXPECT_EQ(refused.from, "Closed");
    EXPECT_EQ(refused.to, "Closed") << "the state is unchanged by a directive that matched nothing";
    EXPECT_TRUE(refused.actions.empty());
}

TEST(Protocol, ThePeerFinEntersTimeWaitAndTheTimeWaitExpires) {
    Session session;
    step(session, "open");
    step(session, "syn_ack");

    const Step finished = step(session, "fin");
    EXPECT_EQ(finished.to, "TimeWait");
    EXPECT_EQ(finished.actions, ran({"on_peer_fin", "arm_time_wait"}))
        << "Established has no exit clause, so only the row's action and the entry run";

    const Step early = step(session, "tick 1999");
    EXPECT_EQ(early.to, "TimeWait") << "the unguarded row absorbs the early tick";
    EXPECT_EQ(early.actions, ran({"arm_time_wait"}))
        << "TimeWait has no exit clause, so only its entry runs around the self-transition";

    const Step late = step(session, "tick 2000");
    EXPECT_EQ(late.to, "Closed");
    EXPECT_EQ(late.actions, ran({"on_time_wait_over"}))
        << "TimeWait has no exit clause and Closed none, so the row's action stands alone";
}

TEST(Protocol, AnEstablishedConnectionAbsorbsATick) {
    Session session;
    step(session, "open");
    step(session, "syn_ack");

    const Step idle = step(session, "tick 5000");
    EXPECT_TRUE(idle.matched) << "a tick while a connection is up is a decision, not a gap";
    EXPECT_EQ(idle.from, "Established");
    EXPECT_EQ(idle.to, "Established");
    EXPECT_TRUE(idle.actions.empty())
        << "Established has no clauses, so this self-transition passes through silently";
}

TEST(Protocol, DataInTimeWaitReopensTheConnection) {
    Session session;
    step(session, "open");
    step(session, "syn_ack");
    step(session, "fin");

    const Step reopened = step(session, "data 512");
    EXPECT_TRUE(reopened.matched);
    EXPECT_EQ(reopened.to, "Established");
    EXPECT_EQ(reopened.actions, ran({"on_reopen"}));
}

TEST(Protocol, ADataValueRidesOnTheEventAndTheActionsCanSeeIt) {
    Session session;
    step(session, "open");
    step(session, "syn_ack");

    const Step packet = step(session, "data 1500");
    EXPECT_TRUE(packet.matched);
    EXPECT_EQ(packet.from, "Established");
    EXPECT_EQ(packet.to, "Established") << "a self-transition: the state is left and entered again";
    EXPECT_EQ(packet.actions, ran({"on_data"}));
}

TEST(Protocol, AnUnreadableDirectiveIsTheDriversMistakeNotTheMachines) {
    Session session;
    Step step_out;
    std::string error;

    EXPECT_FALSE(session.feed("frobnicate", step_out, error));
    EXPECT_NE(error.find("unknown directive"), std::string::npos) << error;

    EXPECT_FALSE(session.feed("", step_out, error));
    EXPECT_NE(error.find("empty"), std::string::npos) << error;

    EXPECT_FALSE(session.feed("tick", step_out, error));
    EXPECT_NE(error.find("needs a value"), std::string::npos) << error;

    EXPECT_FALSE(session.feed("open 3", step_out, error));
    EXPECT_NE(error.find("unexpected"), std::string::npos) << error;

    EXPECT_FALSE(session.feed("data twelve", step_out, error));
    EXPECT_NE(error.find("not a value"), std::string::npos) << error;

    EXPECT_EQ(session.state_name(), "Closed")
        << "a directive that could not be read changed nothing";
}
