#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rewrites the long number type names to the short ones from src/core/types.hpp (AGENTS.md, "Number types").

    tools/short-types/short_types.py [--no-format]

Run from the repository root. In src/, app/, tests/, tools/ and fuzz/ it turns std::int8_t ... std::uint64_t
into i8 ... u64, std::size_t into usize, float into f32 and double into f64, adds #include "core/types.hpp"
where a file now uses them, then runs clang-format (CLANGFORMAT overrides the binary) on the files it changed.
Comments, string and character literals and lines marked `// c-api` are left as they are. Running it again
changes nothing, so a branch started before the rename runs it after rebasing instead of merging by hand.
"""
import os
import re
import subprocess
import sys
from pathlib import Path

DIRS = ("src", "app", "tests", "tools", "fuzz")
HEADER = Path("src/core/types.hpp")
INCLUDE = '#include "core/types.hpp"'
LONG = re.compile(r"(?<![\w:])(?:::)?std::(u?int(?:8|16|32|64)_t|size_t)\b|(?<!\w)(float|double)(?!\w)")
SHORT = re.compile(r"(?<![\w:.>])(?:i8|i16|i32|i64|u8|u16|u32|u64|usize|f32|f64)(?!\w)")


def short(match):
    name = match.group(1)
    if name is None:
        return {"float": "f32", "double": "f64"}[match.group(2)]
    return "usize" if name == "size_t" else name.replace("uint", "u").replace("int", "i").removesuffix("_t")


def code_mask(text):
    """The text with comments and string/character literals blanked out, newlines kept, so offsets line up."""
    code = list(text)
    i, n = 0, len(text)

    def blank(start, end):
        for k in range(start, min(end, n)):
            if code[k] != "\n":
                code[k] = " "
        return min(end, n)

    while i < n:
        c = text[i]
        if text.startswith("//", i):
            end = text.find("\n", i)
            i = blank(i, n if end < 0 else end)
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = blank(i, n if end < 0 else end + 2)
        elif c.isdigit():  # A number, with its suffix and digit separators
            while i < n and (text[i].isalnum() or text[i] in "_'."):
                i += 1
        elif c.isalpha() or c == "_":
            start = i
            while i < n and (text[i].isalnum() or text[i] == "_"):
                i += 1
            if i < n and text[i] == '"' and text[start:i] in ("R", "LR", "uR", "UR", "u8R"):
                open_paren = text.find("(", i)
                close = ")" + text[i + 1 : open_paren] + '"'
                end = text.find(close, open_paren)
                i = blank(i, n if end < 0 else end + len(close))
        elif c in "\"'":
            end = i + 1
            while end < n and text[end] != c and text[end] != "\n":
                end += 2 if text[end] == "\\" else 1
            i = blank(i, end + 1)
        else:
            i += 1
    return "".join(code)


def rewrite_text(text):
    code = code_mask(text)
    out, last, line_start = [], 0, 0
    for m in LONG.finditer(code):
        line_start = text.rfind("\n", 0, m.start()) + 1
        line_end = text.find("\n", m.start())
        if "c-api" in text[line_start : len(text) if line_end < 0 else line_end]:
            continue
        out += [text[last : m.start()], short(m)]
        last = m.end()
    out.append(text[last:])
    new = "".join(out)
    if INCLUDE not in new and SHORT.search(code_mask(new)):
        lines = new.split("\n")
        quoted = [k for k, l in enumerate(lines) if l.lstrip().startswith('#include "')]
        angled = [k for k, l in enumerate(lines) if l.lstrip().startswith("#include <")]
        pragma = [k for k, l in enumerate(lines) if l.strip() == "#pragma once"]
        if quoted:
            lines.insert(quoted[0], INCLUDE)
        elif angled:
            lines[angled[-1] + 1 : angled[-1] + 1] = ["", INCLUDE]
        elif pragma:
            lines[pragma[0] + 1 : pragma[0] + 1] = ["", INCLUDE]
        else:
            top = next(k for k, l in enumerate(lines) if not l.startswith("//"))
            lines[top:top] = [INCLUDE, ""]
        new = "\n".join(lines)
    return new


def rewrite(path):
    text = path.read_text(encoding="utf-8")
    new = rewrite_text(text)
    if new == text:
        return False
    path.write_text(new, encoding="utf-8")
    return True


def self_check():
    before = '#include <cstdint>\n\nfloat f(std::size_t n, double float_value) // "a float" in text\n{\n    return static_cast<float>(sizeof(std::uint16_t)) + 1.0f;\n}\nint g(); // c-api, float stays\n'
    after = '#include <cstdint>\n\n#include "core/types.hpp"\n\nf32 f(usize n, f64 float_value) // "a float" in text\n{\n    return static_cast<f32>(sizeof(u16)) + 1.0f;\n}\nint g(); // c-api, float stays\n'
    assert rewrite_text(before) == after, rewrite_text(before)
    assert rewrite_text(after) == after


def main():
    if not HEADER.exists():
        sys.exit(f"run from the repository root ({HEADER} not found)")
    self_check()
    files = sorted(p for d in DIRS if Path(d).is_dir() for p in Path(d).rglob("*") if p.suffix in (".cpp", ".hpp", ".h") and p != HEADER)
    changed = [p for p in files if rewrite(p)]
    print(f"{len(changed)} of {len(files)} files rewritten")
    if changed and "--no-format" not in sys.argv:
        binary = os.environ.get("CLANGFORMAT", "clang-format")
        for k in range(0, len(changed), 200):
            subprocess.run([binary, "--style=file", "-i", *map(str, changed[k : k + 200])], check=True)


if __name__ == "__main__":
    main()
