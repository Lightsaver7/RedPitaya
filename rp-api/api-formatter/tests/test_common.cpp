/**
 * Unit tests for the type/channel lookup helpers in writers/common.h
 * (SBufferPack). These are pure static functions shared by all three
 * writers, so a regression here silently changes WAV bit depth, TDMS type
 * mapping and CSV column headers at once.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "common.h"
#include "rp_formatter.h"

using namespace rp_formatter_api;

TEST(BufferPackTypes, BitsCountMatchesTheUnderlyingCType) {
    EXPECT_EQ(SBufferPack::getBitsCount(RP_F_ui8_Bit), 8u);
    EXPECT_EQ(SBufferPack::getBitsCount(RP_F_ui16_Bit), 16u);
    EXPECT_EQ(SBufferPack::getBitsCount(RP_F_ui32_Bit), 32u);
    EXPECT_EQ(SBufferPack::getBitsCount(RP_F_i32_Bit), 32u);
    EXPECT_EQ(SBufferPack::getBitsCount(RP_F_f32_Bit), 32u);
    EXPECT_EQ(SBufferPack::getBitsCount(RP_F_ui64_Bit), 64u);
    EXPECT_EQ(SBufferPack::getBitsCount(RP_F_i64_Bit), 64u);
    EXPECT_EQ(SBufferPack::getBitsCount(RP_F_d64_Bit), 64u);
}

TEST(BufferPackTypes, BitsCountIsZeroForAnUnknownType) {
    EXPECT_EQ(SBufferPack::getBitsCount(static_cast<rp_bits_t>(99)), 0u);
}

TEST(BufferPackTypes, TypeNameIsStableForEverySupportedType) {
    EXPECT_STREQ(SBufferPack::getTypeName(RP_F_ui8_Bit), "uint8");
    EXPECT_STREQ(SBufferPack::getTypeName(RP_F_ui16_Bit), "uint16");
    EXPECT_STREQ(SBufferPack::getTypeName(RP_F_ui32_Bit), "uint32");
    EXPECT_STREQ(SBufferPack::getTypeName(RP_F_i32_Bit), "int32");
    EXPECT_STREQ(SBufferPack::getTypeName(RP_F_f32_Bit), "float32");
    EXPECT_STREQ(SBufferPack::getTypeName(RP_F_ui64_Bit), "uint64");
    EXPECT_STREQ(SBufferPack::getTypeName(RP_F_i64_Bit), "int64");
    EXPECT_STREQ(SBufferPack::getTypeName(RP_F_d64_Bit), "double64");
    EXPECT_STREQ(SBufferPack::getTypeName(static_cast<rp_bits_t>(99)), "unknown");
}

TEST(BufferPackTypes, WavSupportExcludesExactlyThe32And64BitIntegerTypes) {
    EXPECT_FALSE(SBufferPack::getWavSupport(RP_F_ui32_Bit));
    EXPECT_FALSE(SBufferPack::getWavSupport(RP_F_i32_Bit));
    EXPECT_FALSE(SBufferPack::getWavSupport(RP_F_ui64_Bit));
    EXPECT_FALSE(SBufferPack::getWavSupport(RP_F_i64_Bit));

    EXPECT_TRUE(SBufferPack::getWavSupport(RP_F_ui8_Bit));
    EXPECT_TRUE(SBufferPack::getWavSupport(RP_F_ui16_Bit));
    EXPECT_TRUE(SBufferPack::getWavSupport(RP_F_f32_Bit));
    EXPECT_TRUE(SBufferPack::getWavSupport(RP_F_d64_Bit));
}

TEST(BufferPackTypes, ChannelNamesCoverCh1ToCh10PlusTimeAndIndex) {
    const std::vector<std::string> expected = {"CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7", "CH8", "CH9", "CH10", "TIME", "INDEX"};
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(std::string(SBufferPack::getChannelName(static_cast<rp_channel_t>(i))), expected[i]) << "channel index " << i;
    }
}

TEST(BufferPackTypes, ChannelNameIsUnknownPastTheLastDefinedChannel) {
    EXPECT_STREQ(SBufferPack::getChannelName(static_cast<rp_channel_t>(RP_F_INDEX + 1)), "UNKNOWN");
}

TEST(BufferPack, ClearDropsEveryPerChannelMap) {
    SBufferPack pack;
    uint8_t sample = 7;
    pack.m_bits[RP_F_CH1] = RP_F_ui8_Bit;
    pack.m_samplesCount[RP_F_CH1] = 1;
    pack.m_buffer[RP_F_CH1] = &sample;
    pack.m_name[RP_F_CH1] = "Named";

    pack.clear();

    EXPECT_TRUE(pack.m_bits.empty());
    EXPECT_TRUE(pack.m_samplesCount.empty());
    EXPECT_TRUE(pack.m_buffer.empty());
    EXPECT_TRUE(pack.m_name.empty());
}
