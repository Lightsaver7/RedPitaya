/**
 * File open/close lifecycle - the regression suite for the double-free and the
 * unguarded Reader pointer. Run under -DTDMS_ENABLE_SANITIZERS=ON: the
 * double-free is only visible as a crash without ASan.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "byte_utils.h"
#include "file.h"
#include "golden_bytes.h"

using namespace tdms_test;

TEST(FileLifecycle, CloseThenDestructorDoesNotDoubleFree) {
    // Close() used to `delete m_reader` without nulling it, and ~File calls
    // Close() again.
    TempFile file("reference.tdms");
    file.write(golden::Reference());

    TDMS::File reader;
    auto segments = reader.ReadFileWithoutClose(file.path());
    EXPECT_FALSE(segments.empty());
    EXPECT_TRUE(reader.Close());
    // ~File runs here.
}

TEST(FileLifecycle, CloseIsIdempotent) {
    TempFile file("reference.tdms");
    file.write(golden::Reference());

    TDMS::File reader;
    reader.ReadFileWithoutClose(file.path());
    EXPECT_TRUE(reader.Close());
    EXPECT_FALSE(reader.Close()) << "second Close reports there was nothing open";
    EXPECT_FALSE(reader.Close());
}

TEST(FileLifecycle, CloseWithoutOpenIsSafe) {
    TDMS::File reader;
    EXPECT_FALSE(reader.Close());
}

TEST(FileLifecycle, DestructorWithoutOpenIsSafe) {
    EXPECT_NO_THROW({ TDMS::File reader; });
}

TEST(FileLifecycle, ReadFileWithoutCloseOnAMissingFileLeavesNothingDangling) {
    // The failure path used to `delete m_reader` and return without nulling it.
    TDMS::File reader;
    auto first = reader.ReadFileWithoutClose("/nonexistent/definitely/not/here.tdms");
    EXPECT_TRUE(first.empty());
    auto second = reader.ReadFileWithoutClose("/nonexistent/definitely/not/here.tdms");
    EXPECT_TRUE(second.empty());
    EXPECT_NO_THROW(reader.Close());
}

TEST(FileLifecycle, ReadFileWithoutCloseTwiceOnAGoodFileIsSafe) {
    TempFile file("reference.tdms");
    file.write(golden::Reference());

    TDMS::File reader;
    auto first = reader.ReadFileWithoutClose(file.path());
    ASSERT_EQ(first.size(), 1u);
    auto second = reader.ReadFileWithoutClose(file.path());
    ASSERT_EQ(second.size(), 1u);
    reader.Close();
}

TEST(FileLifecycle, GetMetadataOnAFreshFileReturnsEmptyInsteadOfCrashing) {
    // m_reader is null until ReadFileWithoutClose runs.
    TDMS::File reader;
    auto segment = std::make_shared<TDMS::Segment>();
    EXPECT_TRUE(reader.GetMetadata(segment).empty());
}

TEST(FileLifecycle, GetMetadataAfterReadFileReturnsEmptyInsteadOfCrashing) {
    // ReadFile builds a LOCAL Reader, so m_reader stays null afterwards.
    TempFile file("reference.tdms");
    file.write(golden::Reference());

    TDMS::File reader;
    auto metadata = reader.ReadFile(file.path());
    EXPECT_FALSE(metadata.empty());

    auto segment = std::make_shared<TDMS::Segment>();
    EXPECT_TRUE(reader.GetMetadata(segment).empty());
}

TEST(FileLifecycle, GetMetadataAfterCloseReturnsEmptyInsteadOfUsingAFreedReader) {
    TempFile file("reference.tdms");
    file.write(golden::Reference());

    TDMS::File reader;
    auto segments = reader.ReadFileWithoutClose(file.path());
    ASSERT_EQ(segments.size(), 1u);
    reader.Close();

    EXPECT_TRUE(reader.GetMetadata(segments[0]).empty());
}

TEST(FileLifecycle, GetMetadataWithANullSegmentReturnsEmpty) {
    TempFile file("reference.tdms");
    file.write(golden::Reference());

    TDMS::File reader;
    reader.ReadFileWithoutClose(file.path());
    EXPECT_TRUE(reader.GetMetadata(nullptr).empty());
}

TEST(FileLifecycle, ReadFileAndGetMetadataAgreeOnTheReferenceSegment) {
    TempFile file("reference.tdms");
    file.write(golden::Reference());

    TDMS::File viaReadFile;
    auto all = viaReadFile.ReadFile(file.path());
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0]->PathStr, "/'Group'");
    EXPECT_EQ(all[1]->PathStr, "/'Group'/'CH1'");

    TDMS::File streaming;
    auto segments = streaming.ReadFileWithoutClose(file.path());
    ASSERT_EQ(segments.size(), 1u);
    auto metadata = streaming.GetMetadata(segments[0]);
    ASSERT_EQ(metadata.size(), 2u);
    EXPECT_EQ(metadata[0]->PathStr, "/'Group'");
    EXPECT_EQ(metadata[1]->PathStr, "/'Group'/'CH1'");
    streaming.Close();
}

TEST(FileLifecycle, ReadFileOnAMissingFileReturnsEmpty) {
    TDMS::File reader;
    EXPECT_TRUE(reader.ReadFile("/nonexistent/definitely/not/here.tdms").empty());
}

TEST(FileLifecycle, ClearPrevMetadataIsSafeAtAnyTime) {
    TDMS::File reader;
    EXPECT_NO_THROW(reader.clearPrevMetadata());

    TempFile file("reference.tdms");
    file.write(golden::Reference());
    reader.ReadFileWithoutClose(file.path());
    EXPECT_NO_THROW(reader.clearPrevMetadata());
    reader.Close();
    EXPECT_NO_THROW(reader.clearPrevMetadata());
}

TEST(FileLifecycle, TwoSegmentsAreBothDiscovered) {
    Bytes twice = golden::Reference();
    const Bytes second = golden::Reference();
    twice.insert(twice.end(), second.begin(), second.end());

    TempFile file("two_segments.tdms");
    file.write(twice);

    TDMS::File reader;
    auto segments = reader.ReadFileWithoutClose(file.path());
    EXPECT_EQ(segments.size(), 2u);
    reader.Close();
}
