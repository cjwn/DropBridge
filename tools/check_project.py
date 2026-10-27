"""Check the research workspace without hardware access or network calls."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
required = ["README.md", "AGENTS.md", "WORK_LOG.md", "docs/PLAN.md",
            "docs/PHASE0_AUDIT.md", "docs/BUILD_WINDOWS.md",
            "docs/RADIO_CONTRACT.md", "docs/LICENSE_MATRIX.md",
            "docs/HARDWARE_INTAKE.md", "docs/BINDING_RUNBOOK.md",
            "docs/用户任务书.md",
            "evidence/build-configure.log", "evidence/build-8821c-configure.log",
            "evidence/build-8821c-configure-full.log",
            "evidence/build-8821c-build.log", "evidence/build-8821c-ctest.log",
            "evidence/usb-open-baseline.log", "evidence/usb-probe-build.log",
            "evidence/binding-result.log", "evidence/chip-id-read.log",
            "evidence/hardware-identity.json", "evidence/upstream-issues.json",
            "tools/Build-Devourer.ps1", "tools/Setup-BuildDeps.ps1",
            "tools/Build-UsbProbe.ps1", "tools/usb_probe/usb_probe.c",
            "tools/usb_probe/CMakeLists.txt",
            "tools/Inspect-Hardware.ps1", "tools/Check-Binding.ps1"]
for name in required:
    assert (root / name).is_file(), f"Missing: {name}"
hw = json.loads((root / "evidence/hardware-identity.json").read_text(encoding="utf-8-sig"))
assert re.fullmatch(r"[0-9a-f]{4}:[0-9a-f]{4}", hw["usb"]["vidPid"]), hw
chip = hw["chipIdentity"]
assert chip["status"] in ("verified", "not verified"), chip
if chip["status"] == "verified":
    assert chip["value"] == "0x09", "Only the RTL8821C chip id is recorded as verified"
entries = json.loads((root / "docs/sources.lock.json").read_text(encoding="utf-8-sig"))
assert len(entries) == 5
assert len({e["name"] for e in entries}) == 5
for entry in entries:
    assert re.fullmatch(r"[0-9a-f]{40}", entry["commit"]), entry
    assert entry["url"].startswith("https://github.com/")
    readme = root / "upstream" / entry["name"] / "README.md"
    assert readme.exists(), f"Restore upstream checkout: {entry['name']}"
print("PASS: project documents, evidence and five locked upstream references exist.")
print("This check does not prove compilation, radio operation or AirDrop interoperability.")
