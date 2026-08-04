#!/usr/bin/env python3
"""
Unit tests for the SWIG-generated `rp_formatter` Python module.

These exercise the exact same CFormatter functionality as the C++
GoogleTest suite (test_wav_writer.cpp / test_csv_writer.cpp /
test_tdms_writer.cpp), but going through the SWIG/numpy typemaps in
src/rp_formatter.i instead of calling into C++ directly -- the two
suites cover different failure modes (writer logic vs. the marshalling
layer) and are intentionally kept separate.

Run via CTest (see tests/CMakeLists.txt), or standalone once the module
is built and on PYTHONPATH:

    PYTHONPATH=/path/to/build/output python3 -m unittest test_rp_formatter_swig -v

NOTE: TDMS/WAV/CSV correctness on the *writer* side is already covered by
the C++ suite and verify_tdms.py, so scenarios that would be redundant
here are kept intentionally small. Two writer defects documented in
test_wav_writer.cpp / test_csv_writer.cpp (BUG-WAV-1, BUG-WAV-2,
BUG-CSV-1) reproduce through this wrapper too, since it is the exact same
C++ code underneath -- see the comments at the matching assertions below.
"""

import os
import tempfile
import unittest

import numpy as np

from rp_formatter import (
    CFormatter,
    RP_F_CH1,
    RP_F_CH2,
    RP_F_CSV,
    RP_F_TDMS,
    RP_F_WAV,
)


def read_wav_header(data: bytes):
    def u16(off):
        return int.from_bytes(data[off : off + 2], "little")

    def u32(off):
        return int.from_bytes(data[off : off + 4], "little")

    return {
        "riff_id": data[0:4],
        "chunk_size": u32(4),
        "wave_id": data[8:12],
        "audio_format": u16(20),
        "num_channels": u16(22),
        "sample_rate": u32(24),
        "byte_rate": u32(28),
        "block_align": u16(32),
        "bits_per_sample": u16(34),
        "data_chunk_size": u32(40),
    }


class TempFileMixin:
    def setUp(self):
        self._tmpdir = tempfile.TemporaryDirectory()

    def tearDown(self):
        self._tmpdir.cleanup()

    def temp_path(self, name):
        return os.path.join(self._tmpdir.name, name)


class WavWriterSwigTest(TempFileMixin, unittest.TestCase):
    def test_float_channel_via_numpy_typemap(self):
        formatter = CFormatter(RP_F_WAV, 44100)
        samples = np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.float32)
        formatter.setChannelFNP(RP_F_CH1, samples)

        path = self.temp_path("test.wav")
        self.assertTrue(formatter.openFile(path))
        self.assertTrue(formatter.writeToFile())
        self.assertTrue(formatter.closeFile())

        with open(path, "rb") as fh:
            data = fh.read()
        header = read_wav_header(data)

        self.assertEqual(header["riff_id"], b"RIFF")
        self.assertEqual(header["wave_id"], b"WAVE")
        self.assertEqual(header["num_channels"], 1)
        self.assertEqual(header["sample_rate"], 44100)

        # See BUG-WAV-1 in src/writers/rp_wav_writer.cpp: bit depth always
        # collapses to 6, so no PCM payload is ever written.
        expected_data_size = samples.size * 4  # 4 bytes per float32 sample
        self.assertEqual(
            header["data_chunk_size"], expected_data_size, "see BUG-WAV-1"
        )
        self.assertEqual(
            len(data), 44 + expected_data_size, "see BUG-WAV-1"
        )


class CsvWriterSwigTest(TempFileMixin, unittest.TestCase):
    def test_two_channels_with_custom_and_default_names(self):
        formatter = CFormatter(RP_F_CSV, 1000)
        ch1 = np.array([0, 1, 2], dtype=np.float32)
        ch2 = np.array([10, 11, 12], dtype=np.uint8)
        formatter.setChannelFNP(RP_F_CH1, ch1, "Voltage")
        formatter.setChannelUI8NP(RP_F_CH2, ch2)

        path = self.temp_path("test.csv")
        self.assertTrue(formatter.openFile(path))
        self.assertTrue(formatter.writeToFile())
        self.assertTrue(formatter.closeFile())

        with open(path, "rb") as fh:
            text = fh.read().decode("ascii")

        lines = text.split("\r\n")
        self.assertEqual(lines[0], "Voltage,CH2")
        self.assertEqual(lines[1], "0.000000,10")
        self.assertEqual(lines[2], "1.000000,11")
        self.assertEqual(lines[3], "2.000000,12")


class TdmsWriterSwigTest(TempFileMixin, unittest.TestCase):
    def test_file_is_created_and_readable_by_nptdms(self):
        try:
            from nptdms import TdmsFile
        except ImportError:
            self.skipTest("nptdms is not installed")

        formatter = CFormatter(RP_F_TDMS, 5000000)
        ch1 = np.array([1, 2, 3, 4], dtype=np.float32)
        formatter.setChannelFNP(RP_F_CH1, ch1, "SwigChannel")

        path = self.temp_path("test.tdms")
        self.assertTrue(formatter.openFile(path))
        self.assertTrue(formatter.writeToFile())
        self.assertTrue(formatter.closeFile())

        tdms = TdmsFile.read(path)
        group = tdms["Group"]
        self.assertEqual(group.properties["osc_rate"], 5000000)
        channel = group["SwigChannel"]
        self.assertEqual(list(channel[:]), [1.0, 2.0, 3.0, 4.0])


class FormatterLifecycleSwigTest(unittest.TestCase):
    def test_write_to_file_without_open_file_returns_false(self):
        formatter = CFormatter(RP_F_CSV, 1000)
        ch1 = np.array([1, 2, 3], dtype=np.float32)
        formatter.setChannelFNP(RP_F_CH1, ch1)
        self.assertFalse(formatter.writeToFile())

    def test_is_open_file_reflects_open_close_state(self):
        formatter = CFormatter(RP_F_CSV, 1000)
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "lifecycle.csv")
            self.assertFalse(formatter.isOpenFile())
            self.assertTrue(formatter.openFile(path))
            self.assertTrue(formatter.isOpenFile())
            self.assertTrue(formatter.closeFile())
            self.assertFalse(formatter.isOpenFile())

    def test_open_file_twice_without_closing_fails(self):
        formatter = CFormatter(RP_F_CSV, 1000)
        with tempfile.TemporaryDirectory() as tmp:
            path1 = os.path.join(tmp, "a.csv")
            path2 = os.path.join(tmp, "b.csv")
            self.assertTrue(formatter.openFile(path1))
            self.assertFalse(formatter.openFile(path2))
            formatter.closeFile()


if __name__ == "__main__":
    unittest.main(verbosity=2)
