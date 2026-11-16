#!/usr/bin/env python3
# @file      generate_dispatch_callbacks.py
# @author    cupidthecat
# @brief     Reproduce owned callback fixture words from their assembled ARM image.
# @date      2026-11-16
#
# @copyright Copyright (c) 2026 cupidthecat
#
# This file is part of CupidGBA.
#
# CupidGBA is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# CupidGBA is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with CupidGBA.  If not, see <https://gnu.org>.
"""Reproduce owned callback fixture words from their assembled ARM image."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--write', action='store_true')
    action.add_argument('--check', action='store_true')
    args = parser.parse_args()
    data = args.image.read_bytes()
    if len(data) != 0x438:
        raise SystemExit('Callback image span changed')
    text = (
        '/* SPDX-License-Identifier: GPL-3.0-or-later */\n'
        '/**\n'
        ' * @file      dispatch-callbacks.inc\n'
        ' * @author    cupidthecat\n'
        ' * @brief     Generated owned ARM callback and interrupt-handler words.\n'
        ' * @date      2026-11-16\n'
        ' *\n'
        ' * @copyright Copyright (c) 2026 cupidthecat\n'
        ' *\n'
        ' * This file is part of CupidGBA.\n'
        ' *\n'
        ' * CupidGBA is free software: you can redistribute it and/or modify\n'
        ' * it under the terms of the GNU General Public License as published by\n'
        ' * the Free Software Foundation, either version 3 of the License, or\n'
        ' * (at your option) any later version.\n'
        ' *\n'
        ' * CupidGBA is distributed in the hope that it will be useful,\n'
        ' * but WITHOUT ANY WARRANTY; without even the implied warranty of\n'
        ' * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n'
        ' * GNU General Public License for more details.\n'
        ' *\n'
        ' * You should have received a copy of the GNU General Public License\n'
        ' * along with CupidGBA.  If not, see <https://gnu.org>.\n'
        ' */\n'
    )
    for name, start, size in [('plain', 0, 52), ('nested', 0x100, 72),
                              ('dma', 0x200, 96), ('timer', 0x300, 84),
                              ('handler', 0x400, 56)]:
        words = struct.unpack_from('<' + str(size // 4) + 'I', data, start)
        text += f'static const uint32_t {name}[] = {{\n'
        rows = (len(words) + 7) // 8
        width = (len(words) + rows - 1) // rows
        for offset in range(0, len(words), width):
            text += '    ' + ', '.join(f'0x{word:08X}' for word in words[offset:offset+width]) + ',\n'
        text += '};\n'
    output = Path(__file__).with_name('dispatch-callbacks.inc')
    if args.write:
        output.write_text(text, encoding='utf-8', newline='\n')
    elif output.read_text() != text:
        raise SystemExit('Callback fixture words are stale')
    print('Verified five owned ARM callback/IRQ instruction streams')


if __name__ == '__main__':
    main()
