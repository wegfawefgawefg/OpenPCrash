#!/usr/bin/env python3
"""Measure the ranges the fixed-point world has to hold, from the converted data/ (docs/numbers.md).

Prints, per stage: the collision triangles' extents, the stage model's vertex extents, the grid's
cell sizes; then the largest coordinate, distance and squared distance in raw Q16.16 units.
Run after pc_extract: tools/measure_numbers.py [data dir]
"""
import json
import math
import re
import sys
from pathlib import Path

data = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "data")
NUMBER = re.compile(r"-?\d+(?:\.\d+)?(?:e[-+]?\d+)?")


def collision_extents(path):
    text = path.read_text()
    low, high = [1e30] * 3, [-1e30] * 3
    for section in ("(triangles", "(floor-triangles"):
        start = text.index(section)
        end = text.index("\n  (", start + 5)
        for line in text[start:end].splitlines()[1:]:
            values = [float(v) for v in NUMBER.findall(line)]
            if len(values) < 13:
                continue
            for k in range(9):
                low[k % 3] = min(low[k % 3], values[k])
                high[k % 3] = max(high[k % 3], values[k])
    cell = [float(v) for v in NUMBER.findall(re.search(r"\(cell ([^)]*)\)", text).group(1))]
    return low, high, cell


def model_extents(path):
    gltf = json.loads(path.read_text())
    low, high = [1e30] * 3, [-1e30] * 3
    for accessor in gltf.get("accessors", []):
        if "min" in accessor and len(accessor["min"]) == 3:
            for k in range(3):
                low[k] = min(low[k], accessor["min"][k])
                high[k] = max(high[k], accessor["max"][k])
    return low, high


def show(name, low, high):
    print(f"  {name}: x {low[0]:9.2f} .. {high[0]:8.2f}  y {low[1]:8.2f} .. {high[1]:7.2f}  z {low[2]:9.2f} .. {high[2]:8.2f}")


largest = 0.0
widest = 0.0
for grid in sorted((data / "collision").glob("*.sexp")):
    low, high, cell = collision_extents(grid)
    print(grid.stem, f"cells {cell[0]:.2f} x {cell[1]:.2f} x {cell[2]:.2f}")
    show("collision", low, high)
    largest = max(largest, *map(abs, low), *map(abs, high))
    widest = max(widest, math.dist(low, high))
for model in sorted((data / "models" / "stages").glob("*.gltf")):
    low, high = model_extents(model)
    print(model.stem)
    show("model", low, high)
    largest = max(largest, *map(abs, low), *map(abs, high))
    widest = max(widest, math.dist(low, high))

one = 65536
print(f"largest |coordinate| {largest:.1f} units, raw Q16.16 {largest * one:.0f} of {2**31 - 1}")
print(f"widest diagonal {widest:.1f} units, raw {widest * one:.0f}")
print(f"its square {widest * widest:.0f} units^2, raw (Q32.32 product) {widest * widest * one * one:.3e} of {2**63 - 1:.3e}")
print(f"a Q16.16 fraction step is {1 / one:.2e} units; per 60 Hz step that is {60 / one:.2e} units a second")
