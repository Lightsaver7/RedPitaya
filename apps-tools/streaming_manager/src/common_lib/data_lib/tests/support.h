/**
 * Helpers shared by the data_lib tests.
 *
 * CDataBufferDMA is normally handed a pointer into memory the DMA engine has
 * mapped, described by a uio_lib::MemoryRegionT. There is no such mapping in a
 * unit test, so Arena hands out an ordinary heap block carved into regions with
 * the same shape: `start` is a device address the library only ever stores and
 * arithmetically offsets, and `startMemory` is the pointer it actually writes
 * through.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "data_lib/buffer.h"
#include "uio_lib/memory_manager.h"

namespace data_test {

// A fake device address base. Nothing dereferences it; it only has to be
// distinguishable from 0 so that "no buffer" and "buffer at address 0" cannot
// be confused.
constexpr uint32_t kDeviceBase = 0x1E000000u;

// One heap allocation cut into `count` equally sized regions.
class Arena {
   public:
    Arena(std::size_t count, std::size_t blockSize) : m_blockSize(blockSize), m_storage(new uint8_t[count * blockSize]) {
        std::memset(m_storage.get(), 0, count * blockSize);
        m_regions.reserve(count);
        for (std::size_t i = 0; i < count; i++) {
            uio_lib::MemoryRegionT region;
            region.tag = uio_lib::MemoryTAG::MM_ADC;
            region.start = static_cast<uint32_t>(kDeviceBase + i * blockSize);
            region.end = static_cast<uint32_t>(kDeviceBase + (i + 1) * blockSize);
            region.startMemory = m_storage.get() + i * blockSize;
            region.size = blockSize;
            region.isFree = false;
            m_regions.push_back(region);
        }
    }

    auto regions() const -> const std::vector<uio_lib::MemoryRegionT>& { return m_regions; }
    auto blockSize() const -> std::size_t { return m_blockSize; }
    auto memoryOf(std::size_t index) const -> uint8_t* { return m_regions[index].startMemory; }

   private:
    std::size_t m_blockSize;
    std::unique_ptr<uint8_t[]> m_storage;
    std::vector<uio_lib::MemoryRegionT> m_regions;
};

// A CDataBufferDMA over one region of an arena, with the write counter reset -
// the constructors do not initialise it, so every test that touches the write
// accounting has to call resetWriteSize() first.
inline auto MakeBuffer(const Arena& arena, std::size_t index, uint8_t bits, std::size_t headerSize = 0) -> DataLib::CDataBufferDMA::Ptr {
    const auto& region = arena.regions()[index];
    auto buffer = DataLib::CDataBufferDMA::Create(region.start, region.size, region.startMemory, bits);
    if (headerSize) {
        buffer->initHeaderAddress(headerSize);
    }
    buffer->resetWriteSize();
    return buffer;
}

}  // namespace data_test
