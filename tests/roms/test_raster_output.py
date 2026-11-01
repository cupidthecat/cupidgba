#!/usr/bin/env python3
"""Check exact raster host encoding without running or changing a cartridge."""
# SPDX-License-Identifier: GPL-3.0-or-later
from contextlib import redirect_stdout
import io
import unittest

from check_raster import check_pixels, rgb


COMPONENT = (0, 8, 16, 24, 33, 41, 49, 57, 66, 74, 82, 90, 99, 107, 115, 123,
             132, 140, 148, 156, 165, 173, 181, 189, 198, 206, 214, 222, 231, 239, 247, 255)
HEADER = b"P6\n240 160\n255\n"


def expected(color):
    return bytes((COMPONENT[color & 31], COMPONENT[(color >> 5) & 31],
                  COMPONENT[(color >> 10) & 31]))


class RasterOutput(unittest.TestCase):
    def test_all_input_words_have_exact_channel_encoding(self):
        for color in range(65536):
            self.assertEqual(rgb(color), expected(color), f"{color:04x}")

    def test_complete_owned_frame_and_strict_negative_controls(self):
        # This owned vector uses the unchanged stable green-swap regions.
        # The real cartridge's source, pixels and timing guards are separate.
        colors = (0x001F, 0x03E0, 0x7C00)
        pixels = bytearray(240 * 160 * 3)
        for y in range(160):
            for x in range(240):
                color = colors[x % 3]
                if y and x < 48:
                    color = (color & ~0x03E0) | (colors[(x ^ 1) % 3] & 0x03E0)
                offset = (y * 240 + x) * 3
                pixels[offset:offset + 3] = expected(color)
        log = io.StringIO()
        with redirect_stdout(log):
            check_pixels("green-swap", HEADER + pixels)
        self.assertEqual(log.getvalue(), "PASS green-swap: 20592 source-derived pixel observations\n")
        for wrong in (254, 248, 0):
            damaged = pixels.copy()
            damaged[0] = wrong
            with self.subTest(red=wrong), self.assertRaises(SystemExit):
                check_pixels("green-swap", HEADER + damaged)
        for bad in (HEADER + pixels[:-1], HEADER + bytes(len(pixels)), b"" + pixels):
            with self.assertRaises(SystemExit):
                check_pixels("green-swap", bad)


if __name__ == "__main__":
    unittest.main()
