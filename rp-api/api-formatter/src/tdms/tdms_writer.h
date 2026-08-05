/**
 * TDMS 2.0 segment construction and serialisation.
 *
 * A TDMS file is a bare sequence of segments; each one is a 28-byte lead-in, a
 * metadata section describing the objects it carries, and the raw sample data
 * for those objects. Appending a segment is the only write operation this
 * library needs, so that is the whole interface: build a Segment, call
 * WriteSegment().
 *
 * This replaces TDMS::WriterSegment + TDMS::Writer + TDMS::File. What is gone:
 *
 *  - The root "/" object. It was created, pushed into a vector, then skipped
 *    by every loop in the writer and never actually emitted; its only real
 *    jobs (carrying the format version and the table-of-contents flags) are a
 *    constant and a value derived from the segment's contents.
 *  - LoadMetadata()/GetRoot()/IsRootNodePresent(). A Segment owns its objects,
 *    so there is no separate "now hand me the vector you built" step that can
 *    be forgotten or given a vector with no root in it.
 *  - Back-patching. The old writer wrote -1 and 0 for the two lead-in offsets,
 *    then seeked backwards to fix them up, keeping a std::list of stream
 *    positions to do it. Here the metadata section is built into a buffer
 *    first, so both offsets are known before the lead-in is written and go in
 *    correct the first time. WriteSegment() never seeks backwards, and a
 *    failed seek can no longer leave a file with bogus offsets.
 *  - The interleaved / DAQmx / big-endian / string-raw-data code paths, all of
 *    which existed only to throw "not implemented".
 *
 * Raw sample data still streams straight from the caller's buffer to the
 * output; only the metadata section (tens of bytes) is buffered.
 */

#ifndef __RP_TDMS_WRITER_H__
#define __RP_TDMS_WRITER_H__

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "tdms_types.h"

namespace rp_formatter_api::tdms {

// TDMS object paths are single-quote delimited: "/'Group'" for a group,
// "/'Group'/'CH1'" for a channel in it.
auto GroupPath(std::string_view _group) -> std::string;
auto ChannelPath(std::string_view _group, std::string_view _channel) -> std::string;

// One TDMS segment under construction.
//
// OWNERSHIP: property values are copied in and owned by the Segment. Channel
// samples are NOT - they are held as RawView and the buffers they point at
// must outlive the WriteSegment() call.
class Segment {
   public:
    // Opaque handle rather than a reference or pointer into the object
    // storage: that storage is a std::vector, so a reference handed out by
    // AddGroup() would dangle the moment AddChannel() grows it.
    enum class ObjectId : std::size_t {};

    struct Object {
        std::string path;
        // std::map, so properties are written in ascending key order. That is
        // what the previous implementation emitted, and keeping it makes the
        // byte layout reproducible.
        std::map<std::string, Value> properties;
        // nullopt marks an object with no raw data (a group), which is written
        // with a 0xFFFFFFFF raw-data index. A channel with zero samples still
        // has a value here and still gets a full index with count 0.
        std::optional<RawView> samples;
    };

    // Idempotent: a second call with the same name returns the same object.
    auto AddGroup(std::string_view _group) -> ObjectId;

    // Declares _group first if it is not present yet, so a group object always
    // precedes its channels. Throws std::invalid_argument for Type::String,
    // whose raw-data index needs an extra total-size field that this writer
    // does not emit.
    auto AddChannel(std::string_view _group, std::string_view _channel, const RawView& _samples) -> ObjectId;

    auto SetProperty(ObjectId _object, std::string _key, Value _value) -> void;

    // Objects in insertion order. The order is load-bearing: the raw data
    // block is written in the same order, and a reader pairs the two by
    // position.
    auto objects() const -> const std::vector<Object>& { return m_objects; }

    auto rawDataByteCount() const -> std::uint64_t;
    auto hasRawData() const -> bool;

   private:
    auto find(std::string_view _path) const -> std::optional<ObjectId>;

    std::vector<Object> m_objects;
};

// The metadata section on its own: object count followed by one record per
// object. Exposed so the encoding can be asserted directly, including sample
// counts too large to actually allocate.
auto EncodeMetadata(const Segment& _segment) -> std::vector<std::uint8_t>;

// Appends exactly one segment at the end of _out: lead-in, metadata, raw data.
// Little-endian, contiguous (non-interleaved), no DAQmx.
auto WriteSegment(std::ostream& _out, const Segment& _segment) -> void;

}  // namespace rp_formatter_api::tdms

#endif
