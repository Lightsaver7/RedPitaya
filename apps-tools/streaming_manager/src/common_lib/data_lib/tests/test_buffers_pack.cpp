/**
 * CDataBuffersPackDMA - the per-channel map of DMA buffers that travels through
 * the streaming pipeline as one unit.
 *
 * The aggregate accessors are not all the same shape and the difference
 * matters: getLenghtBuffers/getLenghtDataBuffers/getLostAll SUM across
 * channels, while getDataBuffersLenght/getBuffersSamples/getOSCRate/
 * getTimeCapture take the MAXIMUM. Which one a caller wants depends on whether
 * the channels are laid out side by side in memory or in parallel in time, so
 * both are pinned explicitly.
 *
 * Channel keys here are ZERO-based (CH1 == 0), the opposite of the one-based
 * convention in CStreamSettings.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

#include "data_lib/buffers_pack.h"
#include "support.h"

using data_test::Arena;
using data_test::MakeBuffer;
using DataLib::CDataBufferDMA;
using DataLib::CDataBuffersPackDMA;
using DataLib::EDataBuffersPackChannel;

TEST(BuffersPack, IsNeitherCopyableNorMovable) {
    static_assert(!std::is_copy_constructible_v<CDataBuffersPackDMA>);
    static_assert(!std::is_move_constructible_v<CDataBuffersPackDMA>);
    SUCCEED();
}

TEST(BuffersPack, TheChannelKeysAreZeroBased) {
    EXPECT_EQ(static_cast<int>(DataLib::CH1), 0);
    EXPECT_EQ(static_cast<int>(DataLib::CH2), 1);
    EXPECT_EQ(static_cast<int>(DataLib::CH3), 2);
    EXPECT_EQ(static_cast<int>(DataLib::CH4), 3);
}

TEST(BuffersPack, AnEmptyPackAnswersEverythingWithZero) {
    auto pack = CDataBuffersPackDMA::Create();

    EXPECT_EQ(pack->getBuffer(DataLib::CH1), nullptr);
    EXPECT_EQ(pack->getBufferDataAddress(DataLib::CH1), 0u);
    EXPECT_FALSE(pack->isChannelPresent(DataLib::CH1));
    EXPECT_EQ(pack->getLenghtBuffers(), 0u);
    EXPECT_EQ(pack->getLenghtDataBuffers(), 0u);
    EXPECT_EQ(pack->getDataBuffersLenght(), 0u);
    EXPECT_EQ(pack->getBuffersSamples(), 0u);
    EXPECT_EQ(pack->getLostAll(), 0u);
    EXPECT_EQ(pack->getTimeCapture(), 0);
    EXPECT_EQ(pack->getOSCRate(), 0u);
    EXPECT_TRUE(pack->checkDataBuffersEqual()) << "no buffers means nothing unequal";
    EXPECT_EQ(pack->getNextDMABufferForWrite(), nullptr);
    EXPECT_TRUE(pack->isAllDataWrite()) << "nothing to write means everything is written";
}

TEST(BuffersPack, AddedBuffersAreRetrievableByTheirChannel) {
    Arena arena(2, 1024);
    auto pack = CDataBuffersPackDMA::Create();
    auto first = MakeBuffer(arena, 0, 16);
    auto second = MakeBuffer(arena, 1, 16);

    pack->addBuffer(DataLib::CH1, first);
    pack->addBuffer(DataLib::CH3, second);

    EXPECT_EQ(pack->getBuffer(DataLib::CH1), first);
    EXPECT_EQ(pack->getBuffer(DataLib::CH3), second);
    EXPECT_EQ(pack->getBuffer(DataLib::CH2), nullptr);
    EXPECT_TRUE(pack->isChannelPresent(DataLib::CH1));
    EXPECT_FALSE(pack->isChannelPresent(DataLib::CH2));
    EXPECT_TRUE(pack->isChannelPresent(DataLib::CH3));
    EXPECT_FALSE(pack->isChannelPresent(DataLib::CH4));

    EXPECT_EQ(pack->getBufferDataAddress(DataLib::CH1), first->getDataAddress());
    EXPECT_EQ(pack->getBufferDataAddress(DataLib::CH3), second->getDataAddress());
    EXPECT_EQ(pack->getBufferDataAddress(DataLib::CH4), 0u) << "an absent channel reports address 0";
}

TEST(BuffersPack, AddingTwiceToAChannelReplacesTheBuffer) {
    Arena arena(2, 1024);
    auto pack = CDataBuffersPackDMA::Create();
    auto first = MakeBuffer(arena, 0, 16);
    auto second = MakeBuffer(arena, 1, 16);

    pack->addBuffer(DataLib::CH1, first);
    pack->addBuffer(DataLib::CH1, second);
    EXPECT_EQ(pack->getBuffer(DataLib::CH1), second);
    EXPECT_EQ(pack->getLenghtBuffers(), 1024u) << "the replaced buffer is gone, not counted twice";
    EXPECT_EQ(first.use_count(), 1) << "the pack released its reference to the old buffer";
}

TEST(BuffersPack, LengthsSumAndTheOtherAggregatesTakeTheMaximum) {
    Arena arena(2, 4096);
    auto pack = CDataBuffersPackDMA::Create();

    // Two channels of different sizes, both 16-bit.
    auto wide = CDataBufferDMA::Create(arena.regions()[0].start, 4096, arena.memoryOf(0), 16);
    auto narrow = CDataBufferDMA::Create(arena.regions()[1].start, 2048, arena.memoryOf(1), 16);
    wide->initHeaderAddress(128);
    narrow->initHeaderAddress(128);
    pack->addBuffer(DataLib::CH1, wide);
    pack->addBuffer(DataLib::CH2, narrow);

    EXPECT_EQ(pack->getLenghtBuffers(), 4096u + 2048u) << "sum, header included";
    EXPECT_EQ(pack->getLenghtDataBuffers(), (4096u - 128u) + (2048u - 128u)) << "sum, header excluded";
    EXPECT_EQ(pack->getDataBuffersLenght(), 4096u - 128u) << "maximum data length";
    EXPECT_EQ(pack->getBuffersSamples(), (4096u - 128u) / 2u) << "maximum sample count";

    wide->setLostSamples(DataLib::EDataLost::FPGA, 3);
    narrow->setLostSamples(DataLib::EDataLost::FPGA, 5);
    EXPECT_EQ(pack->getLostAll(), (3u + 5u) * 2u) << "sum, converted to bytes at 16 bits per sample";

    wide->setTimeCapture(100);
    narrow->setTimeCapture(700);
    EXPECT_EQ(pack->getTimeCapture(), 700) << "maximum capture time";
}

TEST(BuffersPack, TheOscRateIsBroadcastToEveryChannelAndReadBackAsTheMaximum) {
    Arena arena(2, 1024);
    auto pack = CDataBuffersPackDMA::Create();
    auto a = MakeBuffer(arena, 0, 16);
    auto b = MakeBuffer(arena, 1, 16);
    pack->addBuffer(DataLib::CH1, a);
    pack->addBuffer(DataLib::CH2, b);

    pack->setOSCRate(125000000ull);
    EXPECT_EQ(a->getADCBaseRate(), 125000000ull);
    EXPECT_EQ(b->getADCBaseRate(), 125000000ull);
    EXPECT_EQ(pack->getOSCRate(), 125000000ull);

    // Reading takes the maximum, so a single channel left behind is not hidden.
    b->setADCBaseRate(250000000ull);
    EXPECT_EQ(pack->getOSCRate(), 250000000ull);

    pack->setADCBits(12);
    EXPECT_EQ(a->getADCBaseBits(), 12);
    EXPECT_EQ(b->getADCBaseBits(), 12);
    EXPECT_EQ(a->getBitBySample(), 16) << "setADCBits sets the BASE width, not the sample width";
}

TEST(BuffersPack, CheckDataBuffersEqualComparesLengthAndWidthAcrossChannels) {
    Arena arena(3, 4096);
    auto pack = CDataBuffersPackDMA::Create();
    auto a = CDataBufferDMA::Create(arena.regions()[0].start, 2048, arena.memoryOf(0), 16);
    auto b = CDataBufferDMA::Create(arena.regions()[1].start, 2048, arena.memoryOf(1), 16);
    pack->addBuffer(DataLib::CH1, a);
    pack->addBuffer(DataLib::CH2, b);
    EXPECT_TRUE(pack->checkDataBuffersEqual());

    b->setBitBySample(8);
    EXPECT_FALSE(pack->checkDataBuffersEqual()) << "different sample widths";

    b->setBitBySample(16);
    b->initHeaderAddress(128);
    EXPECT_FALSE(pack->checkDataBuffersEqual()) << "different data lengths once one carries a header";

    // A zero-length buffer is skipped rather than treated as a mismatch, which
    // is what lets a pack hold a placeholder channel.
    auto empty = CDataBufferDMA::CreateEmpty(16);
    auto other = CDataBuffersPackDMA::Create();
    other->addBuffer(DataLib::CH1, a);
    other->addBuffer(DataLib::CH2, empty);
    EXPECT_TRUE(other->checkDataBuffersEqual());
}

// getNextDMABufferForWrite walks CH1..CH4 in order and returns the first
// channel with room left, so a caller can fill a pack by calling it in a loop.
// It reads getWriteSizeLeft(), which is only meaningful after resetWriteSizeAll().
TEST(BuffersPack, TheWriteCursorWalksChannelsInOrderUntilThePackIsFull) {
    Arena arena(2, 1024);
    auto pack = CDataBuffersPackDMA::Create();
    auto a = MakeBuffer(arena, 0, 16);
    auto b = MakeBuffer(arena, 1, 16);
    pack->addBuffer(DataLib::CH2, b);
    pack->addBuffer(DataLib::CH1, a);

    pack->resetWriteSizeAll();
    EXPECT_FALSE(pack->isAllDataWrite());
    EXPECT_EQ(pack->getNextDMABufferForWrite(), a) << "CH1 comes first regardless of insertion order";

    a->addWriteSize(1024);
    EXPECT_EQ(pack->getNextDMABufferForWrite(), b) << "CH1 is full, move on";
    EXPECT_FALSE(pack->isAllDataWrite());

    b->addWriteSize(512);
    EXPECT_EQ(pack->getNextDMABufferForWrite(), b) << "partially filled still has room";

    b->addWriteSize(512);
    EXPECT_EQ(pack->getNextDMABufferForWrite(), nullptr);
    EXPECT_TRUE(pack->isAllDataWrite());

    pack->resetWriteSizeAll();
    EXPECT_EQ(pack->getNextDMABufferForWrite(), a);
    EXPECT_FALSE(pack->isAllDataWrite());
    EXPECT_EQ(a->getWriteSize(), 0u);
    EXPECT_EQ(b->getWriteSize(), 0u);
}

TEST(BuffersPack, ThePackKeepsItsBuffersAliveWhileItExists) {
    Arena arena(1, 256);
    auto buffer = MakeBuffer(arena, 0, 8);
    EXPECT_EQ(buffer.use_count(), 1);
    {
        auto pack = CDataBuffersPackDMA::Create();
        pack->addBuffer(DataLib::CH1, buffer);
        EXPECT_EQ(buffer.use_count(), 2);
    }
    EXPECT_EQ(buffer.use_count(), 1);
}

// addBuffer used to store a null Ptr, after which every aggregate that walks the
// map dereferenced it: setOSCRate, getOSCRate, setADCBits,
// checkDataBuffersEqual, getDataBuffersLenght, getLenghtBuffers,
// getLenghtDataBuffers, getLostAll, getBuffersSamples, getTimeCapture,
// debugPack*, verifyPack, getInfoFromHeader*. Only getBuffer,
// getBufferDataAddress, getNextDMABufferForWrite and isAllDataWrite checked.
// addBuffer now refuses it, which keeps one invariant for the whole class:
// isChannelPresent(ch) is true exactly when getBuffer(ch) is usable.
TEST(BuffersPack, ANullBufferIsRefusedRatherThanStored) {
    auto pack = CDataBuffersPackDMA::Create();
    pack->addBuffer(DataLib::CH1, nullptr);

    EXPECT_FALSE(pack->isChannelPresent(DataLib::CH1));
    EXPECT_EQ(pack->getBuffer(DataLib::CH1), nullptr);
    EXPECT_EQ(pack->getLenghtBuffers(), 0u);
    EXPECT_EQ(pack->getLenghtDataBuffers(), 0u);
    EXPECT_EQ(pack->getDataBuffersLenght(), 0u);
    EXPECT_EQ(pack->getBuffersSamples(), 0u);
    EXPECT_EQ(pack->getLostAll(), 0u);
    EXPECT_EQ(pack->getTimeCapture(), 0);
    EXPECT_EQ(pack->getOSCRate(), 0u);
    EXPECT_TRUE(pack->checkDataBuffersEqual());
    pack->setOSCRate(1);
    pack->setADCBits(8);
    pack->resetWriteSizeAll();
    EXPECT_EQ(pack->getNextDMABufferForWrite(), nullptr);
    EXPECT_TRUE(pack->isAllDataWrite());
}

// A null must not evict a channel that is already there either.
TEST(BuffersPack, ARefusedNullLeavesAnExistingChannelAlone) {
    Arena arena(1, 512);
    auto pack = CDataBuffersPackDMA::Create();
    auto buffer = MakeBuffer(arena, 0, 16);
    pack->addBuffer(DataLib::CH1, buffer);

    pack->addBuffer(DataLib::CH1, nullptr);
    EXPECT_EQ(pack->getBuffer(DataLib::CH1), buffer);
    EXPECT_EQ(pack->getLenghtBuffers(), 512u);
}
