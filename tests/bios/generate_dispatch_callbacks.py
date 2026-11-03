#!/usr/bin/env python3
# @file generate_dispatch_callbacks.py
# @brief Reproduce owned callback fixture words from their assembled ARM image.
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
    text = ('/* SPDX-License-Identifier: GPL-3.0-or-later */\n'
            '/** @file dispatch-callbacks.inc\n'
            ' * @brief Generated owned ARM callback and interrupt-handler words.\n'
            ' */\n')
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
