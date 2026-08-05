#ifndef __RP_COMMON_H__
#define __RP_COMMON_H__

#include <cstdio>
#include <istream>
#include <map>
#include <string>
#include "rp_formatter.h"

namespace rp_formatter_api {

typedef enum { RP_F_ui8_Bit, RP_F_ui16_Bit, RP_F_ui32_Bit, RP_F_i32_Bit, RP_F_ui64_Bit, RP_F_i64_Bit, RP_F_f32_Bit, RP_F_d64_Bit } rp_bits_t;

// Binds a writer to the first output stream it was handed and rejects any
// later attempt to write to a different one.
//
// Every writer carries state that only makes sense for one particular
// stream: CWaveWriter emitted a RIFF header at offset 0 and keeps patching
// the chunk sizes there on every subsequent write, CCSVWriter emitted the
// column header row once, and CTDMSWriter appends segments relative to the
// end of the stream it already wrote to. Handing a *different* stream to a
// writer that is mid-sequence therefore does not produce a second valid
// file - it produces a file with no header whose leading bytes get
// overwritten by size patching. Failing loudly is the only safe answer.
//
// resetWriter()/resetHeaderInit() clears the binding, which is the
// supported way to start a new file with the same CFormatter instance.
struct SStreamGuard {
    std::iostream* m_stream = NULL;
    bool m_bound = false;

    auto reset() -> void {
        m_stream = NULL;
        m_bound = false;
    }

    // Binds on first use; afterwards returns false for anything but the
    // stream that was bound.
    auto accept(std::iostream* _memory, const char* _writer) -> bool {
        if (!m_bound) {
            m_stream = _memory;
            m_bound = true;
            return true;
        }
        if (m_stream != NULL && m_stream == _memory) {
            return true;
        }
        fprintf(stderr, "[ERROR] %s: output stream replaced without resetWriter(), write rejected\n", _writer);
        return false;
    }

    // Drops the raw pointer once the bound stream is destroyed, so a new
    // stream that happens to be allocated at the same address can never be
    // mistaken for it. The binding itself stays in place: writing on
    // without resetWriter() is still an error.
    auto invalidate(std::iostream* _memory) -> void {
        if (m_bound && m_stream == _memory) {
            m_stream = NULL;
        }
    }
};

struct SBufferPack {
    std::map<rp_channel_t, rp_bits_t> m_bits = {};
    std::map<rp_channel_t, size_t> m_samplesCount = {};
    std::map<rp_channel_t, void*> m_buffer = {};
    std::map<rp_channel_t, std::string> m_name = {};

    auto clear() -> void {
        m_bits.clear();
        m_samplesCount.clear();
        m_buffer.clear();
        m_name.clear();
    };

    static auto getBitsCount(rp_bits_t type) -> uint8_t {
        switch (type) {
            case RP_F_ui8_Bit:
                return 8;
            case RP_F_ui16_Bit:
                return 16;
            case RP_F_ui32_Bit:
            case RP_F_i32_Bit:
            case RP_F_f32_Bit:
                return 32;
            case RP_F_ui64_Bit:
            case RP_F_i64_Bit:
            case RP_F_d64_Bit:
                return 64;
            default:
                return 0;  // Unknown type
        }
    }

    static auto getTypeName(rp_bits_t type) -> const char* {
        switch (type) {
            case RP_F_ui8_Bit:
                return "uint8";
            case RP_F_ui16_Bit:
                return "uint16";
            case RP_F_ui32_Bit:
                return "uint32";
            case RP_F_i32_Bit:
                return "int32";
            case RP_F_f32_Bit:
                return "float32";
            case RP_F_ui64_Bit:
                return "uint64";
            case RP_F_i64_Bit:
                return "int64";
            case RP_F_d64_Bit:
                return "double64";
            default:
                return "unknown";
        }
    }

    static auto getWavSupport(rp_bits_t type) -> uint8_t {
        switch (type) {
            case RP_F_ui32_Bit:
            case RP_F_i32_Bit:
            case RP_F_ui64_Bit:
            case RP_F_i64_Bit:
                return false;
            default:
                return true;
        }
    }

    static const char* getChannelName(rp_channel_t channel) {
        static const char* names[] = {"CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7", "CH8", "CH9", "CH10", "TIME", "INDEX"};

        if (channel >= 0 && channel <= RP_F_INDEX) {
            return names[channel];
        }
        return "UNKNOWN";
    }
};

}  // namespace rp_formatter_api

#endif