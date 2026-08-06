/**
 * Compile-surface guard mirroring the external consumer exactly.
 *
 * writer_lib/file_helper.cpp has no `using namespace std;` of its own and
 * writes `vector`, `shared_ptr`, `stringstream` and `ios_base` unqualified; it
 * compiled only because the TDMS headers leaked std into it. After the cleanup
 * the consumer qualifies those names itself, but a consumer that legitimately
 * opens the namespace first must still compile - that is what this pins.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <vector>

using namespace std;  // as a consumer is free to do

#include "tdms_lib/file.h"

TEST(ApiCompat, HeadersCompileWhenTheConsumerOpensNamespaceStdFirst) {
    TDMS::WriterSegment segment;
    std::vector<std::shared_ptr<TDMS::Metadata>> nodes;
    auto root = segment.GenerateRoot();
    root->TableOfContents.HasMetaData = true;
    root->TableOfContents.HasRawData = true;
    nodes.push_back(root);
    auto group = segment.GenerateGroup("Group");
    nodes.push_back(group);
    auto channel = segment.GenerateChannel("Group", "ch1");
    nodes.push_back(channel);
    auto buffer = shared_ptr<uint8_t[]>(new uint8_t[4 * sizeof(float)]);
    segment.AddRaw(channel, TDMS::TDMSType::SingleFloat, 4, buffer);
    segment.LoadMetadata(nodes);

    stringstream* memory = new stringstream(ios_base::in | ios_base::out | ios_base::binary);
    TDMS::File outFile;
    outFile.WriteMemory(*memory, segment);
    EXPECT_FALSE(memory->str().empty());
    delete memory;
}
