#!/usr/bin/env python3
"""Check the experimental PicoMEM 2 link map; no hardware access."""
import argparse
import re
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("map", type=Path)
args = p.parse_args()
text = args.map.read_text()

def symbol(name):
    m = re.search(r"^\s*(0x[0-9a-fA-F]+)\s+" + re.escape(name) + r"\s*=", text, re.M)
    if not m:
        raise SystemExit("Missing map symbol: " + name)
    return int(m[1], 16)

bottom, top = symbol("__StackBottom"), symbol("__StackTop")
one_bottom, one_top = symbol("__StackOneBottom"), symbol("__StackOneTop")
if (bottom, top) != (0x20081000, 0x20082000):
    raise SystemExit("Core 0 must reserve exactly the 4 KiB SCRATCH_Y bank")
if (one_bottom, one_top) != (0x20080800, 0x20081000):
    raise SystemExit("Core 1 must retain its 2 KiB reservation")
if symbol("__scratch_y_end__") > bottom or symbol("__scratch_x_end__") > one_bottom:
    raise SystemExit("Scratch code/data overlaps a reserved stack")
print("PASS core 0 4096 bytes; core 1 2048 bytes; no scratch code/data overlap")
