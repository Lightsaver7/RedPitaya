/**
 * memcpy_neon / memcpy_stride_8bit_neon.
 *
 * Both are hand-written NEON on ARM and something else everywhere else, so the
 * property worth testing is that the ARM path produces exactly what the plain
 * path would. memcpy_neon takes the assembly route only when the length is a
 * multiple of 64 and at least 64, so the sizes below straddle that boundary in
 * both directions, and the source and destination are deliberately offset from
 * a 64-byte boundary in one of the cases.
 *
 * memcpy_stride_8bit_neon now has a portable path as well, so the same
 * assertions run everywhere: on ARM they check the assembly, elsewhere the
 * fallback loop, and both have to produce the same bytes.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <numeric>
#include <vector>

#include "data_lib/neon_asm.h"

namespace {

// One byte longer than asked for, so that data() is never null even for size 0:
// forwarding a null pointer to memcpy is undefined even with a length of zero,
// and the point of the size-0 case is the length, not the pointer.
auto Pattern(std::size_t size) -> std::vector<uint8_t> {
    std::vector<uint8_t> data(size + 1);
    for (std::size_t i = 0; i < data.size(); i++) {
        data[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
    }
    return data;
}

}  // namespace

TEST(MemcpyNeon, CopiesEveryLengthAroundTheSixtyFourByteThreshold) {
    static const std::size_t kSizes[] = {0, 1, 2, 3, 7, 8, 15, 16, 31, 32, 63, 64, 65, 127, 128, 129, 192, 255, 256, 1024, 4096};
    for (std::size_t size : kSizes) {
        const std::vector<uint8_t> source = Pattern(size);
        std::vector<uint8_t> destination(size + 16, 0xCC);

        memcpy_neon(destination.data(), source.data(), size);

        EXPECT_EQ(std::memcmp(destination.data(), source.data(), size), 0) << "size " << size;
        for (std::size_t i = size; i < destination.size(); i++) {
            EXPECT_EQ(destination[i], 0xCC) << "size " << size << " wrote past the end at " << i;
        }
    }
}

// The NEON path is taken only when the length is a multiple of 64 and at least
// 64; every other length falls back to plain memcpy, which has no alignment
// requirement at all.
TEST(MemcpyNeon, TheFallbackPathAcceptsAnyAlignment) {
    const std::size_t size = 100;  // not a multiple of 64
    const std::vector<uint8_t> source = Pattern(size + 8);
    std::vector<uint8_t> destination(size + 32, 0xCC);

    for (std::size_t offset = 0; offset <= 7; offset++) {
        std::fill(destination.begin(), destination.end(), 0xCC);
        memcpy_neon(destination.data() + offset, source.data() + offset, size);
        EXPECT_EQ(std::memcmp(destination.data() + offset, source.data() + offset, size), 0) << "offset " << offset;
        EXPECT_EQ(destination[offset + size], 0xCC) << "offset " << offset;
    }
}

// The NEON path needs both pointers word aligned - VLDM/VSTM take an alignment
// fault otherwise - so it is selected only when they are. Aligned offsets check
// that the fast path is still correct.
TEST(MemcpyNeon, TheNeonPathAcceptsWordAlignedAddresses) {
    const std::size_t size = 128;  // a multiple of 64: the NEON path
    const std::vector<uint8_t> source = Pattern(size + 16);
    std::vector<uint8_t> destination(size + 32, 0xCC);

    for (std::size_t offset : {std::size_t{0}, std::size_t{4}, std::size_t{8}}) {
        std::fill(destination.begin(), destination.end(), 0xCC);
        memcpy_neon(destination.data() + offset, source.data() + offset, size);
        EXPECT_EQ(std::memcmp(destination.data() + offset, source.data() + offset, size), 0) << "offset " << offset;
        EXPECT_EQ(destination[offset + size], 0xCC) << "offset " << offset << " wrote past the end";
    }
}

TEST(MemcpyNeon, AZeroLengthCopyTouchesNothing) {
    uint8_t source = 0x11;
    uint8_t destination = 0x22;
    memcpy_neon(&destination, &source, 0);
    EXPECT_EQ(destination, 0x22);
}

TEST(MemcpyNeon, AgreesWithPlainMemcpyOnALargeBlock) {
    const std::size_t size = 1 << 16;
    const std::vector<uint8_t> source = Pattern(size);
    std::vector<uint8_t> viaNeon(size, 0);
    std::vector<uint8_t> viaMemcpy(size, 0);

    memcpy_neon(viaNeon.data(), source.data(), size);
    std::memcpy(viaMemcpy.data(), source.data(), size);
    EXPECT_EQ(viaNeon, viaMemcpy);
}

// The stride copy keeps the ODD byte of each 16-bit sample - the 16 to 8 bit
// downconversion the streaming path uses. `n` counts DESTINATION bytes, so it
// reads 2n. On ARM a length that is a multiple of 64 with word-aligned pointers
// takes the assembly; every other case takes the portable loop. Both are
// checked, and they have to agree byte for byte.
TEST(MemcpyStride8Bit, KeepsTheSecondByteOfEverySixteenBitSample) {
    static const std::size_t kSizes[] = {0, 1, 2, 3, 7, 31, 32, 63, 64, 65, 128, 192, 256, 1024};
    for (std::size_t size : kSizes) {
        const std::vector<uint8_t> source = Pattern(size * 2);
        std::vector<uint8_t> destination(size + 16, 0xCC);

        memcpy_stride_8bit_neon(destination.data(), source.data(), size);

        for (std::size_t i = 0; i < size; i++) {
            EXPECT_EQ(destination[i], source[i * 2 + 1]) << "size " << size << " sample " << i;
        }
        for (std::size_t i = size; i < destination.size(); i++) {
            EXPECT_EQ(destination[i], 0xCC) << "size " << size << " wrote past the end at " << i;
        }
    }
}

// A zero-length stride copy used to slip through the `n % 64` guard on ARM - 0
// is a multiple of 64 - and the loop copied one 32-byte block before finding the
// counter negative.
TEST(MemcpyStride8Bit, AZeroLengthCopyTouchesNothing) {
    const std::vector<uint8_t> source(64, 0x11);
    std::vector<uint8_t> destination(64, 0x22);
    memcpy_stride_8bit_neon(destination.data(), source.data(), 0);
    for (uint8_t byte : destination) {
        EXPECT_EQ(byte, 0x22);
    }
}

// Both entry points now select their path by alignment as well as by length, so
// an unaligned pointer produces the same bytes instead of an alignment fault.
TEST(MemcpyStride8Bit, HandlesUnalignedAddresses) {
    const std::size_t size = 128;
    const std::vector<uint8_t> source = Pattern(size * 2 + 16);
    std::vector<uint8_t> destination(size + 32, 0xCC);

    for (std::size_t offset = 1; offset <= 7; offset++) {
        std::fill(destination.begin(), destination.end(), 0xCC);
        memcpy_stride_8bit_neon(destination.data() + offset, source.data() + offset, size);
        for (std::size_t i = 0; i < size; i++) {
            EXPECT_EQ(destination[offset + i], source[offset + i * 2 + 1]) << "offset " << offset << " sample " << i;
        }
    }
}

// memcpy_neon presents the signature of memcpy and is used as a drop-in
// replacement for it. On ARM the NEON path faulted on an unaligned base, and it
// was reachable, not theoretical: printADCHeader/printDACHeader copy
// sizeof(NetworkPackHeader) == 64 bytes - which selects the NEON path - into a
// local NetworkPackHeader whose alignof is 1 because the struct is packed.
TEST(MemcpyNeon, HandlesUnalignedAddressesOnEveryLength) {
    const std::size_t size = 128;  // a multiple of 64: the NEON path when aligned
    const std::vector<uint8_t> source = Pattern(size + 16);
    std::vector<uint8_t> destination(size + 32, 0xCC);

    for (std::size_t offset = 1; offset <= 7; offset++) {
        std::fill(destination.begin(), destination.end(), 0xCC);
        memcpy_neon(destination.data() + offset, source.data() + offset, size);
        EXPECT_EQ(std::memcmp(destination.data() + offset, source.data() + offset, size), 0) << "offset " << offset;
        EXPECT_EQ(destination[offset + size], 0xCC) << "offset " << offset << " wrote past the end";
    }
}
