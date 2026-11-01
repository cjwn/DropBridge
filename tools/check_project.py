"""Check the research workspace without hardware access or network calls."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
required = ["README.md", "AGENTS.md", "WORK_LOG.md", "docs/PLAN.md",
            "docs/STATUS.md",
            "docs/TCP_DIAGNOSTICS.md",
            "docs/TCP_PROXY_CLEANUP.md",
            "docs/LWIP_INPUT_QUEUE.md",
            "evidence/lwip-input-queue-2026-10-28.log",
            "evidence/lwip-input-queue-2026-10-28.json",
            "evidence/lwip-window-2026-10-28.log",
            "evidence/netd-mdns-soak-2026-10-28.log",
            "evidence/netd-churn-2026-10-28.log",
            "evidence/q31-phone-fixed-j0-2026-10-28.log",
            "evidence/netd-input-backpressure-2026-10-28.log",
            "evidence/q38-phone-fixed-j0-2026-10-28.log",
            "evidence/q38-phone-fixed-merge-on-2026-10-29.log",
            "evidence/q40-phone-merge-ab-2026-10-29.log",
            "evidence/q41-phone-social-merge-2026-10-29.log",
            "evidence/sfx-q38-package-2026-10-29.log",
            "docs/DISTRIBUTION.md",
            "evidence/distributable-folder-2026-10-29.log",
            "evidence/shipped-folder-smallfile-2026-10-29.log",
            "evidence/shipped-folder-video-2026-10-29.log",
            "packaging/README.txt",
            "assets/dropbridge.ico",
            "assets/dropbridge-tray-white.ico",
            "src/supervisor/tray_icon_test.cs",
            "src/supervisor/settings_test.cs",
            "evidence/settings-device-name-2026-11-01.log",
            "evidence/received-copy-popup-2026-11-01.log",
            "evidence/toast-button-2026-11-01.log",
            "evidence/still-copy-2026-11-02.log",
            "tools/toast.ps1",
            "tools/register.ps1",
            "tools/icon_frames.py",
            "evidence/icon-refresh-2026-11-01.log",
            "tools/Package-Distributable.ps1",
            "tools/test_analyze_session.py",
            "tools/Collect-Session.ps1",
            "evidence/tcp-proxy-cleanup-2026-10-28.log",
            "evidence/tcp-proxy-cleanup-cases-2026-10-28.log",
            "evidence/tcp-proxy-cleanup-2026-10-28.json",
            "evidence/tcp-proxy-package-2026-10-28.log",
            "evidence/tcp-diag-offline-2026-10-28.log",
            "evidence/tcp-diag-offline-2026-10-28.json",
            "evidence/tcp-proxy-pre-fix-2026-10-28.log",
            "docs/PHASE0_AUDIT.md", "docs/BUILD_WINDOWS.md",
            "docs/RADIO_CONTRACT.md", "docs/LICENSE_MATRIX.md",
            "docs/HARDWARE_INTAKE.md", "docs/BINDING_RUNBOOK.md",
            "docs/ARCHITECTURE.md", "docs/RADIO_USB.md",
            "docs/PHASE3_AWDL.md", "docs/USERSPACE_DATAPATH.md",
            "docs/PORTABLE.md",
            "docs/用户任务书.md",
            "evidence/build-configure.log", "evidence/build-8821c-configure.log",
            "evidence/build-8821c-configure-full.log",
            "evidence/build-8821c-build.log", "evidence/build-8821c-ctest.log",
            "evidence/usb-open-baseline.log", "evidence/usb-probe-build.log",
            "evidence/binding-result.log", "evidence/chip-id-read.log",
            "evidence/init-first.log", "evidence/rx-first.log",
            "evidence/rx-second-5g.log", "evidence/rx-repeat-summary.log",
            "evidence/tx-witness-summary.json",
            "evidence/airdrop-first-transfer.txt",
            "evidence/airdrop-userspace-tun-on.txt",
            "evidence/airdrop-userspace-tuned.txt",
            "evidence/airdrop-userspace-width40.txt",
            "evidence/airdrop-userspace-width80-attempt.txt",
            "evidence/esp32-witness-tx2.log", "evidence/esp32-witness-tx3.log",
            "evidence/esp32-witness-tx4.log", "evidence/esp32-witness-tx5.log",
            "evidence/esp32-witness-tx6.log",
            "evidence/esp32-witness-build.log", "evidence/esp32-witness-upload.log",
            "evidence/hardware-identity.json", "evidence/upstream-issues.json",
            "tools/Build-Devourer.ps1", "tools/Setup-BuildDeps.ps1",
            "tools/Build-UsbProbe.ps1", "tools/usb_probe/usb_probe.c",
            "tools/usb_probe/CMakeLists.txt",
            "tools/esp32_witness/platformio.ini",
            "tools/esp32_witness/src/main.cpp",
            "tools/esp32_witness/capture_serial.py",
            "tools/esp32_witness/README.md",
            "tools/radio_link_client.py", "tools/radio_link_client.dart",
            "src/radio_usb/CMakeLists.txt", "src/radio_usb/main.cpp",
            "src/radio_usb/radio_link.h", "src/radio_usb/radio_pipe_bridge.cpp",
            "tools/Build-Awdl.ps1", "tools/Run-AwdlAdmin.ps1",
            "tools/Package-Headless.ps1", "tools/Build-Sfx.ps1",
            "tools/Build-Supervisor.ps1", "tools/Start-Chain.ps1",
            "src/sfx/Program.cs", "src/supervisor/Program.cs",
            "tools/Apply-AwdlPatches.ps1",
            "patches/awdl-ipc-host.patch",
            "patches/owl-airbridge.patch",
            "src/awdl_netd/CMakeLists.txt", "src/awdl_netd/main.cpp",
            "src/awdl_netd/lwipopts.h", "src/awdl_netd/local_io.h",
            "src/awdl_netd/local_io.cpp", "src/awdl_netd/lwip_proxy.cpp",
            "src/awdl_netd/tcp_diag.h", "src/awdl_netd/tcp_diag.cpp",
            "src/awdl_netd/tcp_diag_test.cpp", "tools/test_netd_tcp.py",
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
