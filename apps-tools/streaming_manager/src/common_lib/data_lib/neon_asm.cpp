#include "neon_asm.h"
#include <stdint.h>
#include <stdlib.h>

namespace {

// Keeps the second byte of every 16-bit sample - the 16 to 8 bit
// downconversion the streaming path needs. `n` counts DESTINATION bytes, so 2n
// bytes are read. This is what memcpy_stride_8bit_neon means on any target
// without the NEON version, and what it falls back to on ARM when the NEON
// preconditions do not hold; off ARM the function used to call exit(-10)
// instead, so any host-side tool that reached it died with no diagnostic.
inline void stride_8bit_portable(volatile void* dst, volatile const void* src, size_t n) noexcept {
    volatile uint8_t* out = static_cast<volatile uint8_t*>(dst);
    volatile const uint8_t* in = static_cast<volatile const uint8_t*>(src);
    for (size_t i = 0; i < n; i++) {
        out[i] = in[i * 2 + 1];
    }
}

#ifdef ARCH_ARM
// VLDM/VSTM and VLD2.8 take an alignment fault unless the base address is word
// aligned - verified on the target: with a length that selects the NEON path,
// offsets 1, 2, 3, 5, 6 and 7 all raise SIGBUS while 0, 4 and 8 copy correctly.
// Both functions present the signature of memcpy and are used as drop-in
// replacements for it, and neither said a word about the requirement.
//
// It is not theoretical: printADCHeader/printDACHeader copy
// sizeof(NetworkPackHeader) == 64 bytes, which selects the NEON path, into a
// local NetworkPackHeader whose alignof is 1 because the struct is packed.
inline bool neon_usable(volatile void* dst, volatile const void* src) noexcept {
    const uintptr_t bits = reinterpret_cast<uintptr_t>(dst) | reinterpret_cast<uintptr_t>(src);
    return (bits & 0x3u) == 0;
}
#endif  // ARCH_ARM

}  // namespace

void memcpy_neon(__attribute__((unused)) volatile void* dst, __attribute__((unused)) volatile const void* src, __attribute__((unused)) size_t n) noexcept {
#ifdef ARCH_ARM
    if ((n % 64) || n < 64 || !neon_usable(dst, src)) {
        memcpy((void*)dst, (void*)src, n);
        //std::cout << "Warning: Non-optimal neon copy\n";
        return;
    }
    asm volatile(
        "NEONCopyPLD%=:\n"
        "    PLD [%[src], #0xC0]\n"
        "    VLDM %[src]!,{d0-d7}\n"
        "    VSTM %[dst]!,{d0-d7}\n"
        "    SUBS %[n],%[n],#0x40\n"
        "    BGT NEONCopyPLD%=\n"
        : [dst] "+r"(dst), [src] "+r"(src), [n] "+r"(n)
        :
        : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "cc", "memory");
#else
    memcpy((void*)dst, (void*)src, n);
#endif  // ARCH_ARM
}

void memcpy_stride_8bit_neon(volatile void* dst, volatile const void* src, size_t n) noexcept {
#ifdef ARCH_ARM
    // n == 0 has to be excluded explicitly: 0 % 64 is 0, so a zero-length call
    // used to enter the loop, copy one 32-byte block and only then find the
    // counter negative.
    if (n == 0 || (n % 64) || !neon_usable(dst, src)) {
        stride_8bit_portable(dst, src, n);
        //std::cout << "Warning: Non-optimal neon copy\n";
        return;
    }

    asm volatile(
        "NEONCopyPLD_8bit%=:\n"
        "    PLD [%[src], #0xC0]\n"
        "    VLD2.8 {d0,d1},[%[src]]!\n"
        "    VLD2.8 {d2,d3},[%[src]]!\n"
        "    VLD2.8 {d4,d5},[%[src]]!\n"
        "    VLD2.8 {d6,d7},[%[src]]!\n"

        "    VMOV d2,d3              \n"
        "    VMOV d3,d5              \n"
        "    VMOV d4,d7              \n"
        "    VSTM %[dst]!,{d1-d4}\n"
        "    SUBS %[n],%[n],#0x20\n"
        "    BGT NEONCopyPLD_8bit%=\n"
        : [dst] "+r"(dst), [src] "+r"(src), [n] "+r"(n)
        :
        : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "cc", "memory");
#else
    stride_8bit_portable(dst, src, n);
#endif
}
