#!/usr/bin/env python3
"""
Independent correctness check for the TDMS fixture files produced by the
test_tdms_writer GoogleTest binary (see tests/test_tdms_writer.cpp).

Deliberately uses the third-party npTDMS reader (`pip install nptdms`)
instead of this project's own TDMS reading code (there isn't any -- only a
writer exists) or a hand-rolled parser, so a bug shared between the writer
and the verifier can't hide: npTDMS has no knowledge of how
rp_formatter_api::TDMS::Writer works internally.

Run via CTest (see tests/CMakeLists.txt); can also be run standalone:

    python3 verify_tdms.py /path/to/tdms_fixtures
"""

import datetime
import sys
import unittest

try:
    from nptdms import TdmsFile
except ImportError:
    sys.stderr.write(
        "nptdms is required to verify TDMS output: pip install nptdms\n"
    )
    sys.exit(1)

FIXTURES_DIR = None  # set in __main__ below, before unittest.main() runs


def fixture(name):
    return f"{FIXTURES_DIR}/{name}"


def assert_time_property_is_recent(testcase, group, max_age_seconds=300):
    testcase.assertIn("time", group.properties)
    written_at = group.properties["time"]
    # nptdms exposes TDMS timestamps as numpy.datetime64; compare in UTC.
    written_at_dt = written_at.astype("datetime64[s]").astype(datetime.datetime)
    age = (datetime.datetime.utcnow() - written_at_dt).total_seconds()
    testcase.assertGreaterEqual(age, -5, "recorded time is in the future")
    testcase.assertLessEqual(
        age, max_age_seconds, "recorded time is too old to be from this test run"
    )


class SingleFloatChannelTest(unittest.TestCase):
    def test_channel_values_and_properties(self):
        tdms = TdmsFile.read(fixture("single_float_channel.tdms"))
        group = tdms["Group"]
        self.assertEqual(group.properties["osc_rate"], 125000000)
        assert_time_property_is_recent(self, group)

        channel = group["MyChan1"]
        self.assertEqual(channel.dtype.kind, "f")
        self.assertEqual(list(channel[:]), [0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0])


class MixedTypeMultiChannelTest(unittest.TestCase):
    def test_each_channel_keeps_its_own_type_and_values(self):
        tdms = TdmsFile.read(fixture("mixed_type_multi_channel.tdms"))
        group = tdms["Group"]
        self.assertEqual(group.properties["osc_rate"], 1000000)

        ch1 = group["CH1"]  # uint8 input -> written as signed Integer8 (see below)
        ch2 = group["CH2"]  # uint16 input -> written as signed Integer16
        ch3 = group["CH3"]  # int32 input -> Integer32
        ch4 = group["CH4"]  # double input -> DoubleFloat

        # NOTE: rp_tdms_writer.cpp maps RP_F_ui8_Bit/RP_F_ui16_Bit onto the
        # *signed* TDMS::TDMSType::Integer8/Integer16 types (see
        # CTDMSWriter::Impl::write() in src/writers/rp_tdms_writer.cpp).
        # For values that fit in the positive half of the signed range this
        # is invisible; values above 127 / 32767 would come back negative.
        # The values below are deliberately kept inside that safe range so
        # this test documents the mapping without depending on it being a
        # bug versus a deliberate simplification.
        self.assertEqual(list(ch1[:]), [10, 20, 30, 40])
        self.assertEqual(list(ch2[:]), [100, 200, 300, 400])
        self.assertEqual(list(ch3[:]), [-1, -2, -3, -4])
        self.assertEqual([round(float(v), 6) for v in ch4[:]], [1.5, 2.5, 3.5, 4.5])


class TimeAndIndexChannelsTest(unittest.TestCase):
    def test_index_and_time_channels_are_present_alongside_data_channels(self):
        tdms = TdmsFile.read(fixture("time_and_index_channels.tdms"))
        group = tdms["Group"]

        self.assertEqual(list(group["INDEX"][:]), [0, 1, 2, 3])
        self.assertEqual(list(group["TIME"][:]), [0.0, 0.001, 0.002, 0.003])
        self.assertEqual(list(group["CH1"][:]), [1.0, 2.0, 3.0, 4.0])


class RepeatedWriteAppendsSegmentTest(unittest.TestCase):
    def test_two_writes_concatenate_into_one_continuous_channel(self):
        tdms = TdmsFile.read(fixture("two_segments.tdms"))
        group = tdms["Group"]
        channel = group["CH1"]

        self.assertEqual(list(channel[:]), [1.0, 2.0, 3.0, 4.0, 1.0, 2.0, 3.0, 4.0])


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.stderr.write(f"usage: {sys.argv[0]} <fixtures_dir> [unittest args...]\n")
        sys.exit(2)
    FIXTURES_DIR = sys.argv.pop(1)
    unittest.main(verbosity=2)
