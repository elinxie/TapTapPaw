#!/usr/bin/env python3
"""Copy PlatformIO build output into a gh-pages folder for ESP Web Tools.

Used by .github/workflows/firmware.yml. Layout written under <pages_dir>/firmware/:

    channels.json                       list of channels (one per branch), newest first
    <channel>/<env>/bootloader.bin      0x1000
    <channel>/<env>/partitions.bin      0x8000
    <channel>/<env>/boot_app0.bin       0xE000  (otadata: boot app0)
    <channel>/<env>/firmware.bin        0x10000
    <channel>/manifest-<env>.json       ESP Web Tools manifest

Separate parts on purpose. A merged image (esptool merge_bin, offset 0) pads the gap at
0x9000-0xDFFF with 0xFF, which would wipe NVS (settings, touch calibration). Writing the four
parts leaves NVS (0x9000) and LittleFS (0x2D0000) untouched. All three envs share those offsets.

Usage:
    make_flash_manifest.py --pages DIR --channel NAME --branch BRANCH --sha SHA \
        --env esp32dev=PATH_TO_BUILD_DIR [--env ...] --boot-app0 PATH
"""
import argparse
import datetime as dt
import json
import shutil
from pathlib import Path

ENV_LABELS = {
    "esp32dev": "Standard (USB serial)",
    "esp32dev_wifi": "WiFi mode (NTP + weather, no computer)",
    "esp32dev_ble": "Bluetooth keyboard buttons",
}
PARTS = [("bootloader.bin", 0x1000), ("partitions.bin", 0x8000), ("boot_app0.bin", 0xE000), ("firmware.bin", 0x10000)]


def slug(branch: str) -> str:
    return "".join(c if c.isalnum() or c in "-_." else "-" for c in branch).strip("-") or "main"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--pages", required=True, type=Path)
    ap.add_argument("--branch", required=True)
    ap.add_argument("--channel", help="defaults to a slug of --branch")
    ap.add_argument("--sha", required=True)
    ap.add_argument("--boot-app0", required=True, type=Path)
    ap.add_argument("--env", action="append", required=True, help="name=build_dir")
    ap.add_argument("--flags", default="-DINVERT_COLORS_DEFAULT=1")
    a = ap.parse_args()

    channel = a.channel or slug(a.branch)
    root = a.pages / "firmware"
    chan_dir = root / channel
    if chan_dir.exists():
        shutil.rmtree(chan_dir)  # only this channel's old bins; other channels stay
    chan_dir.mkdir(parents=True)

    built = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    version = f"{a.sha[:7]} ({built})"
    envs = []
    for spec in a.env:
        name, _, build = spec.partition("=")
        build = Path(build)
        out = chan_dir / name
        out.mkdir()
        for fname, _ in PARTS:
            src = a.boot_app0 if fname == "boot_app0.bin" else build / fname
            if not src.is_file():
                raise SystemExit(f"missing {src}")
            shutil.copy2(src, out / fname)
        manifest = {
            "name": f"TapTapPaw {ENV_LABELS.get(name, name)}",
            "version": version,
            # No erase option: erasing would delete NVS settings and LittleFS sounds/GIFs.
            "new_install_prompt_erase": False,
            "builds": [{
                "chipFamily": "ESP32",
                "parts": [{"path": f"{name}/{f}", "offset": off} for f, off in PARTS],
            }],
        }
        (chan_dir / f"manifest-{name}.json").write_text(json.dumps(manifest, indent=2) + "\n")
        envs.append({"env": name, "label": ENV_LABELS.get(name, name), "manifest": f"firmware/{channel}/manifest-{name}.json",
                     "app_bytes": (build / "firmware.bin").stat().st_size})

    idx_path = root / "channels.json"
    try:
        idx = json.loads(idx_path.read_text())
    except (FileNotFoundError, ValueError):
        idx = {"channels": []}
    idx["channels"] = [c for c in idx.get("channels", []) if c.get("channel") != channel]
    idx["channels"].insert(0, {"channel": channel, "branch": a.branch, "sha": a.sha, "built": built,
                               "flags": a.flags, "envs": envs})
    idx_path.write_text(json.dumps(idx, indent=2) + "\n")
    print(f"published channel {channel}: {', '.join(e['env'] for e in envs)}")


if __name__ == "__main__":
    main()
