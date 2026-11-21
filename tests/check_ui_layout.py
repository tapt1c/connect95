"""Reject overlapping foreground controls that can erase each other on XP."""
from itertools import combinations
from pathlib import Path
import re


def check_layout(source):
    layout = source.split("static void layout(HWND window)", 1)[1].split(
        "static void drawButton", 1
    )[0]
    pane_width = int(re.search(r"#define DEVICE_PANE_WIDTH (\d+)", source)[1])
    # Include client sizes below the outer 800x520 minimum for window borders,
    # caption, menu and status bar, as well as larger manually resized windows.
    for width in (760, 780, 800, 880, 1024, 1920):
        for height in (440, 460, 480, 700, 1000):
            values = {"width": width, "height": height, "DEVICE_PANE_WIDTH": pane_width}
            # Read the real layout calculations so this checks the implementation.
            for name, expression in re.findall(
                r"^    (left|rightX|rightWidth|buttonWidth|buttonY) = ([^;]+);", layout, re.M
            ):
                values[name] = eval(expression.replace("/", "//"), {"__builtins__": {}}, values)
            rectangles = {}
            for name, arguments in re.findall(
                r"MoveWindow\((\w+),\s*(.*?),\s*TRUE\);", layout, re.S
            ):
                x, y, w, h = (
                    eval(expr.strip(), {"__builtins__": {}}, values)
                    for expr in arguments.split(",")
                )
                assert w > 0 and h > 0, (name, width, height, "non-positive size")
                assert 0 <= x < x + w <= width and 0 <= y < y + h <= height, (
                    name, width, height, "outside client area"
                )
                # A group box intentionally encloses sibling text and progress.
                if name not in ("transferBox", "recentBox"):
                    rectangles[name] = (x, y, x + w, y + h)
            assert {"state", "battery", "pairButton"} <= rectangles.keys()
            for (a, first), (b, second) in combinations(rectangles.items(), 2):
                overlap = (
                    max(first[0], second[0]) < min(first[2], second[2])
                    and max(first[1], second[1]) < min(first[3], second[3])
                )
                assert not overlap, (a, b, width, height, "overlapping controls")


if __name__ == "__main__":
    check_layout((Path(__file__).resolve().parents[1] / "src/main.c").read_text())
    print("UI layout checks passed: controls do not overlap across supported sizes")
