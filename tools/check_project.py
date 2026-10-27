"""Check the research workspace without hardware access or network calls."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
required = ["README.md", "AGENTS.md", "WORK_LOG.md", "docs/PLAN.md",
            "docs/PHASE0_AUDIT.md", "docs/用户任务书.md", "evidence/build-configure.log",
            "evidence/upstream-issues.json"]
for name in required:
    assert (root / name).is_file(), f"Missing: {name}"
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
