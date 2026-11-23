#!/usr/bin/env python3
"""No compiler/build tree needed: compare effective OFF text with the frozen base.

Resolve only SUBREC_HAS_SUBTITLE / SUBREC_BUILD_SUBTITLE branches. Preserve all
other directives/conditions verbatim, including nested unknown branches. This
checks a stronger condition than comparing just the tokens for one platform.
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASE = "8372678"
SOURCES = [
    "main.cpp", "thread/ffaencoderthread.h", "thread/ffaencoderthread.cpp",
    "thread/ffvencoderthread.h", "thread/ffvencoderthread.cpp",
    "recorder/capture_session.h", "recorder/capture_session.cpp",
    "recorder/capture_scheduler.h", "recorder/capture_scheduler.cpp",
    "recorder/ffrecorder.h", "recorder/ffrecoder.cpp",
    "ui/backend/floating_bar_view.h", "ui/backend/floating_bar_view.cpp",
]
CMAKE = ["CMakeLists.txt", "thread/CMakeLists.txt", "recorder/CMakeLists.txt"]
UNTOUCHED = ["event/ffclosesourceevent.cpp", "event/ffopensourceevent.cpp"]


def effective(text, cmake=False):
    # Each frame is (parent_visible, resolved-subtitle-condition, branch-visible).
    stack = []
    visible = True
    result = []
    for line in text.splitlines(keepends=True):
        if cmake:
            directive = re.match(r"\s*(if|elseif|else|endif)\s*\((.*?)\)\s*(?:#.*)?$", line)
        else:
            directive = re.match(r"[\s\ufeff]*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", line)
        if not directive:
            if visible:
                result.append(line)
            continue
        kind, condition = directive.groups()
        if kind in ("if", "ifdef", "ifndef"):
            resolved = (condition.strip() == "SUBREC_BUILD_SUBTITLE" if cmake else
                        kind == "if" and condition.strip() == "defined(SUBREC_HAS_SUBTITLE)")
            stack.append((visible, resolved, not resolved))
            if visible and not resolved:
                result.append(line)
            visible = visible and not resolved
        elif kind in ("else", "elseif", "elif"):
            parent, resolved, branch = stack[-1]
            if resolved:
                if kind != "else":
                    raise AssertionError("Unsupported subtitle elif: " + line)
                branch = not branch
                stack[-1] = (parent, resolved, branch)
            elif parent:
                result.append(line)
            visible = parent and branch
        elif kind == "endif":
            parent, resolved, _ = stack.pop()
            if parent and not resolved:
                result.append(line)
            visible = parent
    assert not stack, "Unbalanced conditional"
    return "".join(result)


def baseline(path):
    return subprocess.check_output(["git", "show", f"{BASE}:{path}"], cwd=ROOT).decode("utf-8")


def main():
    failed = False
    for path in SOURCES + CMAKE + UNTOUCHED:
        old = baseline(path)
        new = (ROOT / path).read_bytes().decode("utf-8")
        if path not in UNTOUCHED:
            old = effective(old, path in CMAKE)
            new = effective(new, path in CMAKE)
        if old == new:
            print(f"PASS byte-identical effective OFF: {path}")
        elif path not in UNTOUCHED and [line for line in old.splitlines() if line.strip()] == [line for line in new.splitlines() if line.strip()]:
            print(f"PASS blank-lines-only effective OFF: {path}")
        else:
            print(f"FAIL effective OFF: {path}")
            failed = True
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
