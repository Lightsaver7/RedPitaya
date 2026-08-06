/**
 * Compile-surface guard: the public headers must be usable in a translation
 * unit that never brings std into scope. Before the header cleanup they carried
 * `using namespace std;` at file scope, which silently made this impossible to
 * verify - and made consumers depend on the leak.
 *
 * This file deliberately writes every standard name fully qualified.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <vector>

#include "tdms_lib/file.h"

TEST(ApiCompat, HeadersCompileWithoutUsingNamespaceStd) {
    TDMS::WriterSegment segment;
    std::vector<std::shared_ptr<TDMS::Metadata>> nodes;
    auto root = segment.GenerateRoot();
    root->TableOfContents.HasMetaData = true;
    nodes.push_back(root);
    nodes.push_back(segment.GenerateGroup("Group"));
    segment.LoadMetadata(nodes);

    std::stringstream stream(std::ios_base::in | std::ios_base::out | std::ios_base::binary);
    TDMS::File file;
    file.WriteMemory(stream, segment);
    EXPECT_FALSE(stream.str().empty());
}
