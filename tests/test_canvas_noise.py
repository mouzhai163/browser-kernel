"""Compile the actual Canvas function with minimal Chromium API stubs.

This checks buffer handling and termination without downloading Chromium. It is
not a replacement for browser integration or rendering tests.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "overlay/fingerprint/sources/simprint/fingerprint_browser_fingerprint.cc"


def _find_compiler() -> tuple[str, str] | None:
    """Return (kind, executable): MSVC ``cl`` on Windows, otherwise a GNU/Clang C++ driver."""
    if shutil.which("cl"):
        return "msvc", "cl"
    for candidate in (os.environ.get("CXX"), "g++", "c++", "clang++"):
        if candidate and shutil.which(candidate):
            return "gnu", candidate
    return None


_COMPILER = _find_compiler()


@unittest.skipUnless(_COMPILER, "MSVC cl.exe or a GNU/Clang C++ compiler is required")
class CanvasNoiseTests(unittest.TestCase):
    def test_native_buffer_boundaries_and_small_multicolour_canvas(self) -> None:
        text = SOURCE.read_text(encoding="utf-8")
        match = re.search(
            r"UNSAFE_BUFFERS\(void BrowserFingerprint::ApplyCanvasNoise\(.*?(?=\n// static)",
            text,
            re.S,
        )
        self.assertIsNotNone(match)
        harness = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>
#define UNSAFE_BUFFERS(...) __VA_ARGS__
namespace base { template <typename T> using span = std::span<T>; }
namespace simprint::config {
std::string mode = "random";
std::string GetCanvasMode() { return mode; }
}
namespace simprint::fingerprint {
class BrowserFingerprint {
 public:
  static void ApplyCanvasNoise(unsigned char*, size_t);
  static uint64_t GetCanvasSeed() { return 123456789; }
};
FUNCTION
}
int main() {
  using simprint::fingerprint::BrowserFingerprint;
  BrowserFingerprint::ApplyCanvasNoise(nullptr, 0);
  BrowserFingerprint::ApplyCanvasNoise(nullptr, 48);
  std::array<unsigned char, 3> incomplete = {1, 2, 3};
  const auto original_incomplete = incomplete;
  BrowserFingerprint::ApplyCanvasNoise(incomplete.data(), incomplete.size());
  assert(incomplete == original_incomplete);
  for (size_t pixels : {size_t(1), size_t(9), size_t(10), size_t(12), size_t(24), size_t(25), size_t(100), size_t(10000)}) {
    std::vector<unsigned char> image(pixels * 4);
    for (size_t i = 0; i < pixels; ++i) {
      image[i * 4] = static_cast<unsigned char>(i % 200 + 20);
      image[i * 4 + 1] = 60;
      image[i * 4 + 2] = 90;
      image[i * 4 + 3] = 255;
    }
    const auto original = image;
    auto repeated = image;
    BrowserFingerprint::ApplyCanvasNoise(image.data(), image.size());
    BrowserFingerprint::ApplyCanvasNoise(repeated.data(), repeated.size());
    assert(image == repeated);
    for (size_t i = 0; i < pixels; ++i) assert(image[i * 4 + 3] == 255);
    if (pixels < 10) assert(image == original);
    simprint::config::mode = "real";
    auto real = original;
    BrowserFingerprint::ApplyCanvasNoise(real.data(), real.size());
    assert(real == original);
    simprint::config::mode = "random";
  }
}
""".replace("FUNCTION", match.group(0))
        with tempfile.TemporaryDirectory(prefix="simprint-canvas-") as directory:
            directory = Path(directory)
            source = directory / "canvas_test.cc"
            executable = directory / ("canvas_test.exe" if _COMPILER[0] == "msvc" else "canvas_test")
            source.write_text(harness, encoding="utf-8")
            kind, compiler = _COMPILER
            if kind == "msvc":
                command = [compiler, "/nologo", "/utf-8", "/std:c++20", "/EHsc", str(source), f"/Fe:{executable}"]
            else:
                command = [compiler, "-std=c++20", "-O1", "-Wall", str(source), "-o", str(executable)]
            compiled = subprocess.run(command, cwd=directory, capture_output=True, timeout=90)
            self.assertEqual(0, compiled.returncode, compiled.stdout + compiled.stderr)
            subprocess.run([str(executable)], check=True, timeout=5)
