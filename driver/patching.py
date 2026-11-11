"""Unified-diff helpers shared by the legacy port and its validator."""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


HUNK_HEADER = re.compile(
    r"^@@ -\d+(?:,(\d+))? \+\d+(?:,(\d+))? @@"
)

# When set, failures are appended to this JSON file and the run continues.
FAILURES_ENV = "MZ_APPLY_FAILURES"


def normalize_unified_diff(text: str) -> str:
    """Normalize historical patch whitespace without changing its meaning.

    Some repository patches contain CRLF and blank context lines without the
    required leading space. GNU patch has historically been permissive here,
    while git apply and other tooling reject the same input. Normalization
    gives both the runtime and CI one deterministic representation.
    """

    lines = text.replace("\r\n", "\n").replace("\r", "\n").splitlines()
    in_hunk = False
    old_remaining = 0
    new_remaining = 0
    normalized: list[str] = []
    for line in lines:
        if line.startswith("diff --git "):
            in_hunk = False
        header = HUNK_HEADER.match(line)
        if header:
            in_hunk = True
            old_remaining = int(header.group(1) or "1")
            new_remaining = int(header.group(2) or "1")
            normalized.append(line)
            continue
        if in_hunk and not line and old_remaining > 0 and new_remaining > 0:
            line = " "
        normalized.append(line)
        if not in_hunk or not line or line.startswith("\\"):
            continue
        if line[0] in {" ", "-"}:
            old_remaining -= 1
        if line[0] in {" ", "+"}:
            new_remaining -= 1
        if old_remaining <= 0 and new_remaining <= 0:
            in_hunk = False
    return "\n".join(normalized) + "\n"


def read_normalized_patch(path: Path) -> bytes:
    text = path.read_text(encoding="utf-8", errors="strict")
    return normalize_unified_diff(text).encode("utf-8")


class PatchError(Exception):
    """A patch could not be applied completely."""


class PatchFormatError(PatchError):
    """A patch is malformed in a way GNU patch would silently tolerate."""


@dataclass(frozen=True)
class Hunk:
    target: str | None  # None when the file is deleted
    line_number: int
    body: tuple[str, ...]


_FILE_HEADER_PREFIXES = (
    "diff --git ",
    "index ",
    "--- ",
    "new file mode ",
    "deleted file mode ",
    "old mode ",
    "new mode ",
    "similarity index ",
    "dissimilarity index ",
    "rename from ",
    "rename to ",
    "copy from ",
    "copy to ",
)


def _parse_target(line: str) -> str | None:
    raw = line[4:].split("\t", 1)[0].strip()
    if raw == "/dev/null":
        return None
    return raw[2:] if raw.startswith(("a/", "b/")) else raw


def parse_hunks(text: str) -> list[Hunk]:
    """Parse a normalized unified diff, rejecting anything outside a well-formed hunk.

    GNU patch treats text after a hunk's declared line count as garbage and
    skips it, together with any later hunk of that file, and still exits 0. A
    single wrong count therefore drops code silently, so the counts are held
    to exactly what the headers declare.
    """

    lines = text.split("\n")
    if lines and not lines[-1]:
        lines.pop()
    hunks: list[Hunk] = []
    target: str | None = None
    in_file = False
    index = 0
    while index < len(lines):
        line = lines[index]
        header = HUNK_HEADER.match(line)
        if header:
            if not in_file:
                raise PatchFormatError(f"line {index + 1}: hunk without a file header")
            start = index
            old_left = int(header.group(1) or "1")
            new_left = int(header.group(2) or "1")
            body: list[str] = []
            index += 1
            while old_left > 0 or new_left > 0:
                if index >= len(lines):
                    raise PatchFormatError(
                        f"line {start + 1}: hunk ends early, still expects "
                        f"{old_left} old and {new_left} new lines"
                    )
                current = lines[index]
                tag = current[:1]
                if tag == " ":
                    old_left -= 1
                    new_left -= 1
                elif tag == "-":
                    old_left -= 1
                elif tag == "+":
                    new_left -= 1
                elif tag != "\\":
                    raise PatchFormatError(
                        f"line {index + 1}: not a hunk line: {current[:80]!r}"
                    )
                if old_left < 0 or new_left < 0:
                    raise PatchFormatError(
                        f"line {start + 1}: hunk has more lines than its header declares"
                    )
                body.append(current)
                index += 1
            while index < len(lines) and lines[index].startswith("\\"):
                body.append(lines[index])
                index += 1
            hunks.append(Hunk(target, start + 1, tuple(body)))
            continue
        if line.startswith("+++ "):
            target = _parse_target(line)
            in_file = True
        elif line.startswith(_FILE_HEADER_PREFIXES):
            in_file = False
        elif not line.strip():
            following = next((l for l in lines[index:] if l.strip()), None)
            if hunks and following is not None and not following.startswith("diff --git "):
                raise PatchFormatError(f"line {index + 1}: blank line inside the diff")
        elif hunks or in_file:
            raise PatchFormatError(
                f"line {index + 1}: text after a hunk's declared length: {line[:80]!r}"
            )
        index += 1
    return hunks


def _find_run(lines: list[str], run: list[str], start: int) -> int:
    for index in range(start, len(lines) - len(run) + 1):
        if lines[index : index + len(run)] == run:
            return index
    return -1


def verify_hunks_applied(chromium_src: Path, hunks: list[Hunk]) -> list[str]:
    """Return one message for every hunk whose result is not in the tree."""

    problems: list[str] = []
    contents: dict[str, list[str] | None] = {}
    cursor = 0
    previous_target: str | None = None
    for hunk in hunks:
        if hunk.target is None:
            continue
        if hunk.target != previous_target:
            cursor = 0
            previous_target = hunk.target
        if hunk.target not in contents:
            path = chromium_src / hunk.target
            contents[hunk.target] = (
                path.read_bytes().decode("utf-8", "replace").replace("\r\n", "\n").split("\n")
                if path.is_file()
                else None
            )
        lines = contents[hunk.target]
        if lines is None:
            problems.append(f"{hunk.target}: file missing after patching")
            continue
        body = [line for line in hunk.body if not line.startswith("\\")]
        position = 0
        while position < len(body):
            if body[position][:1] == " ":
                position += 1
                continue
            end = position
            while end < len(body) and body[end][:1] in {"+", "-"}:
                end += 1
            added = [line[1:] for line in body[position:end] if line[0] == "+"]
            if added:
                found = _find_run(lines, added, cursor)
                expected = added
            elif position > 0 and end < len(body):
                # A pure deletion leaves its two neighbouring context lines adjacent.
                expected = [body[position - 1][1:], body[end][1:]]
                found = _find_run(lines, expected, cursor)
            else:
                position = end
                continue
            if found < 0:
                problems.append(
                    f"{hunk.target}: hunk at patch line {hunk.line_number} is missing "
                    f"from the tree (looking for {expected[0][:80]!r})"
                )
            else:
                cursor = found + len(expected) - (0 if added else 1)
            position = end
    return problems


def apply_patch_data(chromium_src: Path, data: bytes) -> None:
    """Apply a normalized patch and confirm that every hunk reached the tree."""

    hunks = parse_hunks(data.decode("utf-8"))
    if not hunks:
        raise PatchFormatError("patch has no hunks")
    result = subprocess.run(
        ["patch", "--batch", "--forward", "--no-backup-if-mismatch", "-p1"],
        cwd=chromium_src,
        input=data,
        capture_output=True,
    )
    if result.returncode != 0:
        output = result.stdout.decode("utf-8", errors="replace")
        output += result.stderr.decode("utf-8", errors="replace")
        raise PatchError(f"patch exited with {result.returncode}\n{output}")
    problems = verify_hunks_applied(chromium_src, hunks)
    if problems:
        raise PatchError("\n".join(problems))


def record_failure(label: str, error: str) -> None:
    """Add a failure to MZ_APPLY_FAILURES, or stop when no such list is kept."""

    failures_path = os.environ.get(FAILURES_ENV)
    if not failures_path:
        raise SystemExit(f"{label}: {error}")
    path = Path(failures_path)
    failures = json.loads(path.read_text(encoding="utf-8")) if path.is_file() else []
    failures.append({"label": label, "error": error})
    path.write_text(json.dumps(failures, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"  !! FAILED {label}\n{error}", file=sys.stderr)


def apply_patch(chromium_src: Path, patch_file: Path, label: str) -> None:
    print(f"Applying: {label}")
    try:
        apply_patch_data(chromium_src, read_normalized_patch(patch_file))
    except PatchError as exc:
        record_failure(label, str(exc))
