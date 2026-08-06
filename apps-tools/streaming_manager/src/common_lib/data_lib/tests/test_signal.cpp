/**
 * signal.hpp is a vendored copy of the sigslot single-header library, not code
 * written for this project, so this file does not attempt to test it: it pins
 * the subset of the API the streaming libraries actually use, in the way they
 * use it. net_lib, config_net_lib, broadcast_lib and streaming_lib all declare
 * sigslot::signal<...> members and connect lambdas or member functions to them,
 * so what has to keep working is: connect, emit, connection lifetime, weak-ptr
 * tracking of a shared_ptr receiver, blocking, and disconnect_all.
 *
 * The value of pinning it is that dropping in a newer sigslot, or building with
 * a compiler that handles its SFINAE differently, breaks here rather than in a
 * networking path that only misfires under load.
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "data_lib/signal.hpp"

namespace {

struct Receiver {
    int calls = 0;
    int last = 0;
    void onValue(int value) {
        calls++;
        last = value;
    }
};

int g_freeFunctionCalls = 0;

void FreeSlot(int value) {
    g_freeFunctionCalls += value;
}

}  // namespace

TEST(Signal, ALambdaSlotReceivesEveryEmission) {
    sigslot::signal<int> signal;
    std::vector<int> seen;
    signal.connect([&seen](int value) { seen.push_back(value); });

    EXPECT_EQ(signal.slot_count(), 1u);
    signal(1);
    signal(2);
    signal(3);
    EXPECT_EQ(seen, (std::vector<int>{1, 2, 3}));
}

TEST(Signal, SlotsAreCalledInConnectionOrder) {
    sigslot::signal<> signal;
    std::string order;
    signal.connect([&order]() { order += "a"; });
    signal.connect([&order]() { order += "b"; });
    signal.connect([&order]() { order += "c"; });

    EXPECT_EQ(signal.slot_count(), 3u);
    signal();
    EXPECT_EQ(order, "abc");
}

TEST(Signal, AFreeFunctionAndAMemberFunctionCanBothBeSlots) {
    g_freeFunctionCalls = 0;
    Receiver receiver;

    sigslot::signal<int> signal;
    signal.connect(&FreeSlot);
    signal.connect(&Receiver::onValue, &receiver);

    signal(5);
    EXPECT_EQ(g_freeFunctionCalls, 5);
    EXPECT_EQ(receiver.calls, 1);
    EXPECT_EQ(receiver.last, 5);
}

TEST(Signal, DisconnectingAConnectionStopsThatSlotOnly) {
    sigslot::signal<int> signal;
    int first = 0;
    int second = 0;
    auto connection = signal.connect([&first](int value) { first += value; });
    signal.connect([&second](int value) { second += value; });

    signal(1);
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 1);

    EXPECT_TRUE(connection.valid());
    EXPECT_TRUE(connection.connected());
    connection.disconnect();
    EXPECT_FALSE(connection.connected());
    EXPECT_EQ(signal.slot_count(), 1u);

    signal(10);
    EXPECT_EQ(first, 1) << "the disconnected slot must not fire again";
    EXPECT_EQ(second, 11);
}

TEST(Signal, BlockingSuspendsASlotWithoutDisconnectingIt) {
    sigslot::signal<int> signal;
    int calls = 0;
    auto connection = signal.connect([&calls](int) { calls++; });

    signal(1);
    EXPECT_EQ(calls, 1);

    connection.block();
    EXPECT_TRUE(connection.blocked());
    signal(1);
    EXPECT_EQ(calls, 1) << "a blocked slot is skipped";
    EXPECT_TRUE(connection.connected()) << "...but it is still connected";

    connection.unblock();
    EXPECT_FALSE(connection.blocked());
    signal(1);
    EXPECT_EQ(calls, 2);
}

TEST(Signal, AScopedConnectionDisconnectsWhenItGoesOutOfScope) {
    sigslot::signal<int> signal;
    int calls = 0;
    {
        sigslot::scoped_connection scoped = signal.connect([&calls](int) { calls++; });
        signal(1);
        EXPECT_EQ(calls, 1);
        EXPECT_EQ(signal.slot_count(), 1u);
    }
    EXPECT_EQ(signal.slot_count(), 0u);
    signal(1);
    EXPECT_EQ(calls, 1);
}

// This is the pattern that makes sigslot worth vendoring: the slot owner is held
// by weak_ptr, so a receiver that dies takes its slot with it and the signal
// does not call into freed memory.
TEST(Signal, ASlotBoundToASharedPtrDisappearsWithTheReceiver) {
    sigslot::signal<int> signal;
    auto receiver = std::make_shared<Receiver>();
    signal.connect(&Receiver::onValue, receiver);

    signal(7);
    EXPECT_EQ(receiver->calls, 1);
    EXPECT_EQ(receiver->last, 7);

    receiver.reset();
    signal(9);  // must not touch the destroyed Receiver
    EXPECT_EQ(signal.slot_count(), 0u) << "the expired slot is pruned";
}

TEST(Signal, DisconnectAllRemovesEverySlot) {
    sigslot::signal<int> signal;
    int calls = 0;
    signal.connect([&calls](int) { calls++; });
    signal.connect([&calls](int) { calls++; });
    ASSERT_EQ(signal.slot_count(), 2u);

    signal.disconnect_all();
    EXPECT_EQ(signal.slot_count(), 0u);
    signal(1);
    EXPECT_EQ(calls, 0);
}

TEST(Signal, ASignalWithNoSlotsIsSafeToEmit) {
    sigslot::signal<int, std::string> signal;
    EXPECT_EQ(signal.slot_count(), 0u);
    signal(1, "x");
    SUCCEED();
}

TEST(Signal, SlotsOutliveTheEmitterVariableTheyCapture) {
    // Connecting several argument types is what the config layer does; check
    // that multi-argument signals forward every argument unchanged.
    sigslot::signal<int, std::string, bool> signal;
    int seenInt = 0;
    std::string seenText;
    bool seenFlag = false;
    signal.connect([&](int i, const std::string& text, bool flag) {
        seenInt = i;
        seenText = text;
        seenFlag = flag;
    });

    signal(42, "hello", true);
    EXPECT_EQ(seenInt, 42);
    EXPECT_EQ(seenText, "hello");
    EXPECT_TRUE(seenFlag);
}
