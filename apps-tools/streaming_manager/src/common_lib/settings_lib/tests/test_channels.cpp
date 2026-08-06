/**
 * Channels<Enum, MaxChannels> from channels.hpp - the bitmask type behind
 * dac_channels_t and adc_channels_t.
 *
 * Two things make this worth pinning carefully. The first is that the storage
 * type is chosen by MaxChannels, so dac_channels_t and adc_channels_t are both
 * uint8_t-backed but have different valid masks (0b11 and 0b1111), and every
 * out-of-range operation is expected to be silently ignored rather than to
 * assert. The second is the overload set: there are five constructors, and
 * which one a braced or parenthesised literal selects is not obvious -
 * Channels(size_t) takes a channel INDEX while Channels(StorageType) takes a
 * MASK, and `{1}` picks neither of them but the initializer_list one.
 *
 * NOTE: these tests compare `.activeMask`, never two Channels objects with
 * EXPECT_EQ. channels.hpp declares a friend operator<< whose body streams
 * `static_cast<Enum>(ch)` for a scoped enum, which has no operator<< - the
 * declaration is found by ADL, so GoogleTest selects it as the value printer
 * and the body then fails to compile. Comparing the mask keeps the friend
 * uninstantiated. See the report for the one-line fix.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "settings_lib/channels.hpp"

namespace {

// The channel indices an object yields, in iteration order.
auto Enumerate(const dac_channels_t& channels) -> std::vector<int> {
    std::vector<int> out;
    for (int ch : channels) {
        out.push_back(ch);
    }
    return out;
}

auto Enumerate(const adc_channels_t& channels) -> std::vector<int> {
    std::vector<int> out;
    for (int ch : channels) {
        out.push_back(ch);
    }
    return out;
}

}  // namespace

TEST(Channels, TheStorageTypeAndValidMaskFollowMaxChannels) {
    static_assert(std::is_same_v<dac_channels_t::StorageType, uint8_t>, "2 channels fit in a uint8_t");
    static_assert(std::is_same_v<adc_channels_t::StorageType, uint8_t>, "4 channels fit in a uint8_t");
    static_assert(std::is_same_v<dac_channels_t::EnumType, DACChannels>);
    static_assert(std::is_same_v<adc_channels_t::EnumType, ADCChannels>);

    EXPECT_EQ(dac_channels_t().getValidMask(), 0x03);
    EXPECT_EQ(adc_channels_t().getValidMask(), 0x0F);
}

TEST(Channels, ADefaultConstructedObjectIsEmpty) {
    dac_channels_t channels;
    EXPECT_EQ(channels.activeMask, 0u);
    EXPECT_EQ(channels.count(), 0u);
    EXPECT_FALSE(channels.isEnabled(DACChannels::DAC_CH1));
    EXPECT_FALSE(channels.isEnabled(DACChannels::DAC_CH2));
    EXPECT_TRUE(Enumerate(channels).empty());
    EXPECT_EQ(channels.format(), "");
}

// Channels(size_t) is an INDEX, Channels(StorageType) is a MASK. Both are
// explicit and both take a one-byte-ish integer, so the distinction is easy to
// lose; the two lines below construct different objects from the same 1.
TEST(Channels, TheIndexConstructorAndTheMaskConstructorDisagreeOnPurpose) {
    const dac_channels_t byIndex(static_cast<size_t>(1));
    EXPECT_EQ(byIndex.activeMask, 0x02) << "index 1 means CH2";

    const dac_channels_t byMask(static_cast<uint8_t>(1));
    EXPECT_EQ(byMask.activeMask, 0x01) << "mask 1 means CH1";

    // The mask constructor clamps to the valid mask; the index constructor
    // ignores an out-of-range index instead of wrapping it.
    EXPECT_EQ(dac_channels_t(static_cast<uint8_t>(0xFF)).activeMask, 0x03);
    EXPECT_EQ(dac_channels_t(static_cast<size_t>(7)).activeMask, 0x00);
    EXPECT_EQ(adc_channels_t(static_cast<uint8_t>(0xFF)).activeMask, 0x0F);
}

TEST(Channels, TheEnumConstructorAndInitializerListsSetTheExpectedBits) {
    EXPECT_EQ(dac_channels_t(DACChannels::DAC_CH1).activeMask, 0x01);
    EXPECT_EQ(dac_channels_t(DACChannels::DAC_CH2).activeMask, 0x02);

    const dac_channels_t both{DACChannels::DAC_CH1, DACChannels::DAC_CH2};
    EXPECT_EQ(both.activeMask, 0x03);

    const adc_channels_t byIndices{0u, 2u};
    EXPECT_EQ(byIndices.activeMask, 0x05);

    // A brace-init with a single integer picks the initializer_list<size_t>
    // constructor, not the mask constructor: {3} is "channel 3", not "mask 3".
    const adc_channels_t braced{3u};
    EXPECT_EQ(braced.activeMask, 0x08);
    // ... and out-of-range members of the list are dropped silently.
    const dac_channels_t partly{0u, 5u};
    EXPECT_EQ(partly.activeMask, 0x01);
}

TEST(Channels, EnableAndDisableIgnoreOutOfRangeIndices) {
    adc_channels_t channels;
    channels.enable(ADCChannels::ADC_CH2);
    channels.enable(static_cast<size_t>(3));
    EXPECT_EQ(channels.activeMask, 0x0A);
    EXPECT_EQ(channels.count(), 2u);

    channels.enable(static_cast<size_t>(4));
    channels.enable(static_cast<size_t>(64));
    EXPECT_EQ(channels.activeMask, 0x0A) << "out-of-range enable must be a no-op, not a shift overflow";

    channels.disable(ADCChannels::ADC_CH2);
    EXPECT_EQ(channels.activeMask, 0x08);
    channels.disable(static_cast<size_t>(9));
    EXPECT_EQ(channels.activeMask, 0x08);

    EXPECT_FALSE(channels.isEnabled(static_cast<size_t>(4)));
    EXPECT_FALSE(channels.isEnabled(static_cast<size_t>(255)));
}

TEST(Channels, EnableAllAndResetUseTheValidMask) {
    adc_channels_t channels;
    channels.enableAll();
    EXPECT_EQ(channels.activeMask, 0x0F);
    EXPECT_EQ(channels.count(), 4u);
    channels.reset();
    EXPECT_EQ(channels.activeMask, 0x00);
    EXPECT_EQ(channels.count(), 0u);

    dac_channels_t dac;
    dac.enableAll();
    EXPECT_EQ(dac.activeMask, 0x03) << "enableAll must not set bits above MaxChannels";
}

TEST(Channels, IterationYieldsEnabledIndicesInAscendingOrder) {
    adc_channels_t channels{0u, 2u, 3u};
    EXPECT_EQ(Enumerate(channels), (std::vector<int>{0, 2, 3}));

    channels.reset();
    EXPECT_TRUE(Enumerate(channels).empty());

    channels.enable(ADCChannels::ADC_CH4);
    EXPECT_EQ(Enumerate(channels), (std::vector<int>{3}));

    // count() and the iteration length must agree for every mask.
    for (uint8_t mask = 0; mask <= 0x0F; mask++) {
        const adc_channels_t all(mask);
        EXPECT_EQ(Enumerate(all).size(), all.count()) << "mask " << static_cast<int>(mask);
    }
}

TEST(Channels, TheSubscriptProxyReadsAndWrites) {
    dac_channels_t channels;
    channels[DACChannels::DAC_CH1] = true;
    EXPECT_EQ(channels.activeMask, 0x01);
    channels[static_cast<size_t>(1)] = true;
    EXPECT_EQ(channels.activeMask, 0x03);
    channels[DACChannels::DAC_CH1] = false;
    EXPECT_EQ(channels.activeMask, 0x02);

    const dac_channels_t frozen = channels;
    EXPECT_FALSE(frozen[DACChannels::DAC_CH1]);
    EXPECT_TRUE(frozen[DACChannels::DAC_CH2]);
    EXPECT_FALSE(frozen[static_cast<size_t>(7)]);

    // Writing an out-of-range index through the proxy is a no-op as well.
    channels[static_cast<size_t>(6)] = true;
    EXPECT_EQ(channels.activeMask, 0x02);
}

TEST(Channels, SetOperatorsCombineMasks) {
    const adc_channels_t a{0u, 1u};
    const adc_channels_t b{1u, 2u};

    EXPECT_EQ((a + b).activeMask, 0x07);
    EXPECT_EQ((a | b).activeMask, 0x07);
    EXPECT_EQ((a & b).activeMask, 0x02);
    EXPECT_EQ((a - b).activeMask, 0x01);
    EXPECT_EQ((a ^ b).activeMask, 0x05);
    EXPECT_EQ((~a).activeMask, 0x0C) << "complement must stay inside the valid mask";

    adc_channels_t acc{0u};
    acc += b;
    EXPECT_EQ(acc.activeMask, 0x07);
    acc -= b;
    EXPECT_EQ(acc.activeMask, 0x01);
    acc |= b;
    EXPECT_EQ(acc.activeMask, 0x07);
    acc &= b;
    EXPECT_EQ(acc.activeMask, 0x06);
    acc ^= b;
    EXPECT_EQ(acc.activeMask, 0x00);

    EXPECT_TRUE(a == adc_channels_t({0u, 1u}));
    EXPECT_TRUE(a != b);
}

TEST(Channels, TheEnumOperatorsActOnASingleChannel) {
    adc_channels_t channels;
    channels += ADCChannels::ADC_CH3;
    EXPECT_EQ(channels.activeMask, 0x04);
    EXPECT_EQ((channels + ADCChannels::ADC_CH1).activeMask, 0x05);
    EXPECT_EQ(channels.activeMask, 0x04) << "operator+(Enum) must not mutate the left operand";
    channels -= ADCChannels::ADC_CH3;
    EXPECT_EQ(channels.activeMask, 0x00);

    const adc_channels_t one{2u};
    // operator&(Enum) is the membership test and returns bool, unlike
    // operator&(const Channels&), which returns a Channels.
    EXPECT_TRUE(one & ADCChannels::ADC_CH3);
    EXPECT_FALSE(one & ADCChannels::ADC_CH1);
}

TEST(Channels, CopyAndMovePreserveOrClearTheMask) {
    dac_channels_t source{0u, 1u};
    const dac_channels_t copied(source);
    EXPECT_EQ(copied.activeMask, 0x03);
    EXPECT_EQ(source.activeMask, 0x03);

    const dac_channels_t moved(std::move(source));
    EXPECT_EQ(moved.activeMask, 0x03);
    EXPECT_EQ(source.activeMask, 0x00) << "the move constructor clears the source";

    dac_channels_t assigned;
    assigned = copied;
    EXPECT_EQ(assigned.activeMask, 0x03);

    dac_channels_t movedInto;
    dac_channels_t temp{1u};
    movedInto = std::move(temp);
    EXPECT_EQ(movedInto.activeMask, 0x02);
    EXPECT_EQ(temp.activeMask, 0x00);

    // Self-assignment must not clear the mask.
    dac_channels_t self{0u};
    dac_channels_t& alias = self;
    self = alias;
    EXPECT_EQ(self.activeMask, 0x01);
    self = std::move(alias);
    EXPECT_EQ(self.activeMask, 0x01);
}

TEST(Channels, ToStringAndFromStringRoundTripTheMask) {
    for (uint8_t mask = 0; mask <= 0x0F; mask++) {
        const adc_channels_t original(mask);
        const adc_channels_t restored = adc_channels_t::fromString(original.toString());
        EXPECT_EQ(restored.activeMask, original.activeMask) << "mask " << static_cast<int>(mask);
    }
    EXPECT_EQ(adc_channels_t({0u, 3u}).toString(), "9");
}

TEST(Channels, FromStringClampsAndSwallowsGarbage) {
    EXPECT_EQ(adc_channels_t::fromString("255").activeMask, 0x0F) << "clamped to the valid mask";
    EXPECT_EQ(dac_channels_t::fromString("255").activeMask, 0x03);
    EXPECT_EQ(adc_channels_t::fromString("").activeMask, 0x00);
    EXPECT_EQ(adc_channels_t::fromString("not a number").activeMask, 0x00);
    EXPECT_EQ(adc_channels_t::fromString("99999999999999999999999").activeMask, 0x00) << "out_of_range is swallowed too";
    // stoull stops at the first non-digit rather than rejecting the string.
    EXPECT_EQ(adc_channels_t::fromString("3abc").activeMask, 0x03);
}

TEST(Channels, FormatListsChannelsOneBased) {
    adc_channels_t channels{0u, 2u};
    EXPECT_EQ(channels.format(), "CH1, CH3");

    adc_channels_t all;
    all.enableAll();
    EXPECT_EQ(all.format(), "CH1, CH2, CH3, CH4");

    adc_channels_t none;
    EXPECT_EQ(none.format(), "");
}

TEST(Channels, ExtractionEnablesTheChannelItReads) {
    adc_channels_t channels;
    std::istringstream input("2");
    input >> channels;
    EXPECT_EQ(channels.activeMask, 0x04) << "the stream carries an index, not a mask";

    std::istringstream broken("x");
    adc_channels_t untouched{0u};
    broken >> untouched;
    EXPECT_EQ(untouched.activeMask, 0x01) << "a failed extraction leaves the object alone";
}
