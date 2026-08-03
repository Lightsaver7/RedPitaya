#pragma once
#include <zlib.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

inline bool GzipToVector(const void* _in, size_t _size, std::vector<uint8_t>& _out, int _level) {
    _out.clear();

    z_stream s;
    memset(&s, 0, sizeof(s));

    if (deflateInit2(&s, _level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }

    _out.resize(deflateBound(&s, (uLong)_size) + 18);

    s.next_in = (Bytef*)_in;
    s.avail_in = (uInt)_size;
    s.next_out = _out.data();
    s.avail_out = (uInt)_out.size();

    const int rc = deflate(&s, Z_FINISH);
    const size_t produced = _out.size() - s.avail_out;
    deflateEnd(&s);

    if (rc != Z_STREAM_END) {
        _out.clear();
        return false;
    }

    _out.resize(produced);
    return true;
}

inline void Gziping(const std::string& in, std::vector<unsigned char>& out) {
    GzipToVector(in.data(), in.size(), out, 1);
}

inline void GzipingBin(const uint8_t* in_data, size_t in_size, std::vector<uint8_t>& out) {
    GzipToVector(in_data, in_size, out, Z_DEFAULT_COMPRESSION);
}
