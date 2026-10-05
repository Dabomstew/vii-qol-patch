"""Small source/config regression check for the opt-in neptasm layer."""
from configparser import ConfigParser
from pathlib import Path

root = Path(__file__).parents[1]
parser = ConfigParser()
parser.read(root / "vii-patches.ini", encoding="utf-8")
assert parser.getint("Patches", "Neptasm") == 0
for key in ("FPSUnlock", "CameraUnlock", "Resolution", "FitWindow", "WindowControl", "Ultrawide"):
    assert parser.getint("Neptasm", key) == 0, key
assert parser.get("Neptasm", "ResolutionScale") == "1.0"
print("neptasm defaults: all opt-in controls are present and disabled")
