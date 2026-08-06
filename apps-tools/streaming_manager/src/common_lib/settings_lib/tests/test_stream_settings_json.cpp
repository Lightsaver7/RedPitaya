/**
 * The JSON layer: toJson / parseJson / writeToFile / readFromFile.
 *
 * The round trip through the library's own output is the contract that has to
 * hold; the interesting part is what happens to input the library did NOT
 * write, because that is where a reader drifts away from its writer. Each ADC
 * channel key is therefore fed to parseJson on its own, and a document that is
 * valid JSON but not an object is checked to come back as false rather than as
 * an exception.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "settings_lib/stream_settings.h"

namespace {

using AC_DC = CStreamSettings::AC_DC;
using ADCCaptureTime = CStreamSettings::ADCCaptureTime;
using Attenuator = CStreamSettings::Attenuator;
using DACGain = CStreamSettings::DACGain;
using DACPassMode = CStreamSettings::DACPassMode;
using DACRepeat = CStreamSettings::DACRepeat;
using DataFormat = CStreamSettings::DataFormat;
using DataType = CStreamSettings::DataType;
using PassMode = CStreamSettings::PassMode;
using Resolution = CStreamSettings::Resolution;
using State = CStreamSettings::State;

// Out parameter, not a return value: CStreamSettings deletes its move
// constructor, so a factory returning one by value does not compile.
auto FillFullySetSettings(CStreamSettings& s) -> void {
    s.setADCSamples(4096);
    s.setADCFormat(DataFormat::TDMS);
    s.setADCType(DataType::VOLT);
    s.setADCCaptureTime(ADCCaptureTime::OFF);
    s.setADCPassMode(PassMode::FILE);
    s.setADCResolution(Resolution::BIT_8);
    s.setADCDecimation(64);
    s.setADCCalibration(State::OFF);
    for (uint8_t ch = 1; ch <= 4; ch++) {
        s.setADCChannels(ch, State::ON);
        s.setADCAttenuator(ch, Attenuator::A_1_20);
        s.setADCAC_DC(ch, AC_DC::AC);
    }
    s.setDACSpeed(1000000);
    s.setDACFile("/tmp/some.wav");
    s.setDACFileType(DataFormat::TDMS);
    s.setDACPassMode(DACPassMode::DAC_FILE);
    s.setDACRepeat(DACRepeat::DAC_REP_INF);
    s.setDACRepeatCount(7);
    s.setDACGain(1, DACGain::X5);
    s.setDACGain(2, DACGain::X5);
    s.setMemoryBlockSize(0x20000);
    s.setADCSize(111);
    s.setDACSize(222);
    s.setGPIOSize(333);
}

// A directory removed on destruction, so a failing test cannot leave files
// behind that make the next run pass or fail for the wrong reason.
class ScratchDir {
   public:
    ScratchDir() {
        static int counter = 0;
        m_path = std::filesystem::temp_directory_path() /
                 ("settings_lib_test_" + std::to_string(++counter) + "_" + std::to_string(static_cast<long>(::getpid())));
        std::error_code ec;
        std::filesystem::create_directories(m_path, ec);
    }
    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;

    auto file(const std::string& name) const -> std::string { return (m_path / name).string(); }
    auto path() const -> std::string { return m_path.string(); }

   private:
    std::filesystem::path m_path;
};

}  // namespace

TEST(StreamSettingsJson, TheDocumentHasTheThreeRequiredSections) {
    const std::string json = CStreamSettings().toJson();
    EXPECT_NE(json.find("\"adc_streaming\""), std::string::npos);
    EXPECT_NE(json.find("\"dac_streaming\""), std::string::npos);
    EXPECT_NE(json.find("\"memory_manager\""), std::string::npos);
    // Enum fields are written as the enumerator identifier, which is the only
    // form parseJson accepts back.
    EXPECT_NE(json.find("\"BIN\""), std::string::npos);
    EXPECT_NE(json.find("\"WAV\""), std::string::npos);
}

TEST(StreamSettingsJson, EveryFieldSurvivesToJsonThenParseJson) {
    CStreamSettings source;
    FillFullySetSettings(source);

    CStreamSettings target;
    ASSERT_TRUE(target.parseJson(source.toJson()));
    EXPECT_EQ(target.toJson(), source.toJson());

    EXPECT_EQ(target.getADCSamples(), 4096u);
    EXPECT_EQ(target.getADCFormat(), DataFormat::TDMS);
    EXPECT_EQ(target.getADCType(), DataType::VOLT);
    EXPECT_EQ(target.getADCCaptureTime(), ADCCaptureTime::OFF);
    EXPECT_EQ(target.getADCPassMode(), PassMode::FILE);
    EXPECT_EQ(target.getADCResolution(), Resolution::BIT_8);
    EXPECT_EQ(target.getADCDecimation(), 64u);
    EXPECT_EQ(target.getADCCalibration(), State::OFF);
    for (uint8_t ch = 1; ch <= 4; ch++) {
        EXPECT_EQ(target.getADCChannels(ch), State::ON) << "channel " << static_cast<int>(ch);
        EXPECT_EQ(target.getADCAttenuator(ch), Attenuator::A_1_20);
        EXPECT_EQ(target.getADCAC_DC(ch), AC_DC::AC);
    }
    EXPECT_EQ(target.getDACSpeed(), 1000000u);
    EXPECT_EQ(target.getDACFile(), "/tmp/some.wav");
    EXPECT_EQ(target.getDACFileType(), DataFormat::TDMS);
    EXPECT_EQ(target.getDACPassMode(), DACPassMode::DAC_FILE);
    EXPECT_EQ(target.getDACRepeat(), DACRepeat::DAC_REP_INF);
    EXPECT_EQ(target.getDACRepeatCount(), 7u);
    EXPECT_EQ(target.getDACGain(1), DACGain::X5);
    EXPECT_EQ(target.getDACGain(2), DACGain::X5);
    EXPECT_EQ(target.getMemoryBlockSize(), 0x20000u);
    EXPECT_EQ(target.getADCSize(), 111u);
    EXPECT_EQ(target.getDACSize(), 222u);
    EXPECT_EQ(target.getGPIOSize(), 333u);
}

TEST(StreamSettingsJson, TheDefaultsAlsoSurviveTheRoundTrip) {
    const CStreamSettings defaults;
    CStreamSettings target;
    FillFullySetSettings(target);
    ASSERT_TRUE(target.parseJson(defaults.toJson()));
    EXPECT_EQ(target.toJson(), defaults.toJson());
}

TEST(StreamSettingsJson, ParsingIsIdempotent) {
    CStreamSettings full;
    FillFullySetSettings(full);
    const std::string json = full.toJson();
    CStreamSettings s;
    ASSERT_TRUE(s.parseJson(json));
    const std::string once = s.toJson();
    ASSERT_TRUE(s.parseJson(json));
    EXPECT_EQ(s.toJson(), once);
}

TEST(StreamSettingsJson, AbsentKeysKeepTheCurrentValue) {
    CStreamSettings s;
    ASSERT_TRUE(s.setADCChannels(1, State::ON));
    ASSERT_TRUE(s.setADCDecimation(32));

    // All three sections present, none of them carrying any of those keys.
    ASSERT_TRUE(s.parseJson(R"({"adc_streaming":{},"dac_streaming":{},"memory_manager":{}})"));
    EXPECT_EQ(s.getADCChannels(1), State::ON) << "parseJson merges, it does not reset";
    EXPECT_EQ(s.getADCDecimation(), 32u);
}

TEST(StreamSettingsJson, AMissingSectionIsRejected) {
    CStreamSettings s;
    const std::string before = s.toJson();

    EXPECT_FALSE(s.parseJson(R"({"dac_streaming":{},"memory_manager":{}})"));
    EXPECT_FALSE(s.parseJson(R"({"adc_streaming":{},"memory_manager":{}})"));
    EXPECT_FALSE(s.parseJson(R"({"adc_streaming":{},"dac_streaming":{}})"));
    EXPECT_FALSE(s.parseJson("{}"));
    EXPECT_EQ(s.toJson(), before) << "a rejected document must not change anything";
}

TEST(StreamSettingsJson, MalformedInputIsRejectedWithoutThrowing) {
    CStreamSettings s;
    const std::string before = s.toJson();

    EXPECT_FALSE(s.parseJson(""));
    EXPECT_FALSE(s.parseJson("not json"));
    EXPECT_FALSE(s.parseJson("{"));
    EXPECT_FALSE(s.parseJson("null")) << "a null document has no members, so the section check rejects it";
    // Trailing content after a complete document is ACCEPTED: the reader is
    // built with the default settings, which do not set failIfExtra.
    EXPECT_TRUE(s.parseJson(R"({"adc_streaming":{},"dac_streaming":{},"memory_manager":{}} trailing garbage)"));
    EXPECT_EQ(s.toJson(), before) << "and the document itself carried no keys, so nothing changed";
}

// Json::Value::isMember throws on a value that is neither an object nor null,
// and the section checks run before the try block - so a document that was
// valid JSON but not an object used to leave parseJson as a Json::LogicError
// instead of as false, and readFromFile handed that to its caller. parseJson now
// rejects a non-object root up front.
TEST(StreamSettingsJson, ANonObjectDocumentIsRejectedWithFalse) {
    CStreamSettings s;
    const std::string before = s.toJson();

    EXPECT_FALSE(s.parseJson("[]"));
    EXPECT_FALSE(s.parseJson("[1,2,3]"));
    EXPECT_FALSE(s.parseJson("123"));
    EXPECT_FALSE(s.parseJson("\"a string\""));
    EXPECT_FALSE(s.parseJson("true"));
    EXPECT_EQ(s.toJson(), before);
}

TEST(StreamSettingsJson, ANonObjectFileIsRejectedWithFalse) {
    const ScratchDir dir;
    const std::string path = dir.file("array.json");
    { std::ofstream(path) << "[]"; }

    CStreamSettings s;
    EXPECT_FALSE(s.readFromFile(path));
}

TEST(StreamSettingsJson, AnUnknownEnumValueIsRejected) {
    CStreamSettings s;
    EXPECT_FALSE(s.parseJson(R"({"adc_streaming":{"format_sd":"XML"},"dac_streaming":{},"memory_manager":{}})"));
    // The display string is not the identifier, so it is rejected as well.
    EXPECT_FALSE(s.parseJson(R"({"adc_streaming":{"format_sd":"tdms"},"dac_streaming":{},"memory_manager":{}})"));
}

// Every per-channel key is guarded by its own isMember, so any subset of them is
// a valid document. The AC/DC guard used to test the ATTENUATOR key instead,
// which made an ac_dc-only document parse "successfully" while dropping the
// value, and made an attenuator-only document read a missing member - which
// threw into the catch-all and returned false after the attenuator had already
// been written. Documents this library writes itself always carry both keys,
// which is why neither direction was ever noticed. Both are checked here.
TEST(StreamSettingsJson, EachAdcChannelKeyIsAppliedOnItsOwn) {
    CStreamSettings acDcOnly;
    ASSERT_EQ(acDcOnly.getADCAC_DC(1), AC_DC::DC);
    EXPECT_TRUE(acDcOnly.parseJson(R"({"adc_streaming":{"channel_ac_dc_1":"AC"},"dac_streaming":{},"memory_manager":{}})"));
    EXPECT_EQ(acDcOnly.getADCAC_DC(1), AC_DC::AC);
    EXPECT_EQ(acDcOnly.getADCAttenuator(1), Attenuator::A_1_1) << "and nothing else moved";

    CStreamSettings attenuatorOnly;
    ASSERT_EQ(attenuatorOnly.getADCAttenuator(1), Attenuator::A_1_1);
    EXPECT_TRUE(attenuatorOnly.parseJson(R"({"adc_streaming":{"channel_attenuator_1":"A_1_20"},"dac_streaming":{},"memory_manager":{}})"))
        << "a missing channel_ac_dc_1 must not be read at all";
    EXPECT_EQ(attenuatorOnly.getADCAttenuator(1), Attenuator::A_1_20);
    EXPECT_EQ(attenuatorOnly.getADCAC_DC(1), AC_DC::DC);

    CStreamSettings stateOnly;
    EXPECT_TRUE(stateOnly.parseJson(R"({"adc_streaming":{"channel_state_2":"ON"},"dac_streaming":{},"memory_manager":{}})"));
    EXPECT_EQ(stateOnly.getADCChannels(2), State::ON);
    EXPECT_EQ(stateOnly.getADCChannels(1), State::OFF);
}

// The same, per channel: a document naming only channel 4 must leave 1..3 alone.
TEST(StreamSettingsJson, APerChannelKeyTouchesOnlyThatChannel) {
    CStreamSettings s;
    ASSERT_TRUE(s.parseJson(R"({"adc_streaming":{"channel_ac_dc_4":"AC","channel_attenuator_4":"A_1_20"},"dac_streaming":{},"memory_manager":{}})"));
    for (uint8_t ch = 1; ch <= 3; ch++) {
        EXPECT_EQ(s.getADCAC_DC(ch), AC_DC::DC) << "channel " << static_cast<int>(ch);
        EXPECT_EQ(s.getADCAttenuator(ch), Attenuator::A_1_1) << "channel " << static_cast<int>(ch);
    }
    EXPECT_EQ(s.getADCAC_DC(4), AC_DC::AC);
    EXPECT_EQ(s.getADCAttenuator(4), Attenuator::A_1_20);
}

// BIN reaches setDACFileType, which refuses it. parseJson deliberately keeps
// ignoring that result: one unusable field should not invalidate a whole
// settings file, so the document parses and the field keeps its previous value.
// setValue("file_type_sd", "BIN") does report false - see the keys suite.
TEST(StreamSettingsJson, ADacFileTypeOfBinIsAcceptedAndThenIgnored) {
    CStreamSettings s;
    ASSERT_TRUE(s.setDACFileType(DataFormat::TDMS));
    EXPECT_TRUE(s.parseJson(R"({"adc_streaming":{},"dac_streaming":{"file_type_sd":"BIN"},"memory_manager":{}})"));
    EXPECT_EQ(s.getDACFileType(), DataFormat::TDMS);
}

TEST(StreamSettingsJson, WriteToFileAndReadFromFileRoundTrip) {
    const ScratchDir dir;
    const std::string path = dir.file("settings.json");
    CStreamSettings source;
    FillFullySetSettings(source);

    ASSERT_TRUE(source.toJson().size() > 0);
    CStreamSettings writable = source;
    ASSERT_TRUE(writable.writeToFile(path));
    ASSERT_TRUE(std::filesystem::exists(path));

    std::ifstream check(path);
    const std::string onDisk((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    EXPECT_EQ(onDisk, source.toJson()) << "the file holds exactly toJson()";

    CStreamSettings target;
    ASSERT_TRUE(target.readFromFile(path));
    EXPECT_EQ(target.toJson(), source.toJson());
}

TEST(StreamSettingsJson, WriteToFileCreatesMissingParentDirectories) {
    const ScratchDir dir;
    const std::string path = dir.file("a/b/c/settings.json");
    CStreamSettings s;
    EXPECT_TRUE(s.writeToFile(path));
    EXPECT_TRUE(std::filesystem::exists(path));
}

TEST(StreamSettingsJson, ReadFromFileReportsAMissingOrUnreadableFile) {
    const ScratchDir dir;
    CStreamSettings s;
    EXPECT_FALSE(s.readFromFile(dir.file("does_not_exist.json")));
    EXPECT_FALSE(s.readFromFile(dir.path())) << "a directory is not a settings file";
}

// readFromFile resets to the defaults BEFORE parsing, so a file that fails to
// parse leaves the object at the defaults rather than at its previous contents.
TEST(StreamSettingsJson, AFailedReadLeavesTheDefaultsNotThePreviousValues) {
    const ScratchDir dir;
    const std::string path = dir.file("broken.json");
    { std::ofstream(path) << "{ this is not json"; }

    CStreamSettings s;
    FillFullySetSettings(s);
    ASSERT_NE(s.toJson(), CStreamSettings().toJson());

    EXPECT_FALSE(s.readFromFile(path));
    EXPECT_EQ(s.toJson(), CStreamSettings().toJson()) << "resetDefault() runs before the parse attempt";
}

TEST(StreamSettingsJson, AnEmptyFileIsRejected) {
    const ScratchDir dir;
    const std::string path = dir.file("empty.json");
    { std::ofstream create(path); }

    CStreamSettings s;
    EXPECT_FALSE(s.readFromFile(path));
}

TEST(StreamSettingsJson, ExtraMembersAreIgnored) {
    CStreamSettings s;
    EXPECT_TRUE(s.parseJson(R"({"adc_streaming":{"unknown_key":42},"dac_streaming":{},"memory_manager":{},"extra_section":{"x":1}})"));
    EXPECT_EQ(s.toJson(), CStreamSettings().toJson());
}
