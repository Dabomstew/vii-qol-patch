"""Regression guard for non-16:9 aspect selection in the native port."""
from pathlib import Path

source = (Path(__file__).parents[1] / "src" / "neptasm.cpp").read_text(encoding="utf-8")
assert "s.windowControl && std::abs(configuredAspect - 16.0f / 9.0f) > 0.0001f" in source
assert "state->settings.fitWindow || state->settings.ultrawide" in source
assert "effectiveAspect - 16.0f / 9.0f" in source
print("neptasm aspect path: window-control and runtime-fit/ultrawide modes are guarded")
