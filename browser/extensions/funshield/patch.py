# Rebrand a extracted uBlock Origin tree as FUNSHIELD (GPLv3).
import json
import os
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "_src"
XPI = ROOT / "funshield@funxplorer.xpi"

SHIELD_SVG = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128">
  <path fill="#7c5cfc" d="M64 8 20 28v36c0 28 18 48 44 56 26-8 44-28 44-56V28Z"/>
  <path fill="#f4f0ff" d="M64 28c-16 0-28 12-28 28 0 18 12 28 28 36 16-8 28-18 28-36 0-16-12-28-28-28zm0 14c8 0 14 6 14 14s-6 14-14 14-14-6-14-14 6-14 14-14z"/>
</svg>
"""


def patch_manifest():
    path = SRC / "manifest.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    data["author"] = "FUNCOMPUTER Labs (based on uBlock Origin by Raymond Hill)"
    data["name"] = "FUNSHIELD"
    data["short_name"] = "FUNSHIELD"
    data["browser_action"]["default_title"] = "FUNSHIELD"
    data["browser_action"]["default_icon"] = {
        "16": "img/funshield.svg",
        "32": "img/funshield.svg",
        "64": "img/funshield.svg",
    }
    data["icons"] = {str(s): "img/funshield.svg" for s in (16, 32, 48, 64, 96, 128)}
    gecko = data.setdefault("browser_specific_settings", {}).setdefault("gecko", {})
    gecko["id"] = "funshield@funxplorer"
    gecko.pop("update_url", None)
    data["content_scripts"] = [
        cs
        for cs in data.get("content_scripts", [])
        if "js/scriptlets/updater.js" not in cs.get("js", [])
    ]
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def patch_locales():
    for messages in SRC.glob("_locales/*/messages.json"):
        data = json.loads(messages.read_text(encoding="utf-8"))
        if "extName" in data:
            data["extName"]["message"] = "FUNSHIELD"
        if "extShortDesc" in data:
            data["extShortDesc"]["message"] = (
                "Funxplorer shield. Blocks ads, trackers, annoyances, and extra noise."
            )
        for key in (
            "dashboardName",
            "statsPageName",
            "assetViewerPageName",
        ):
            if key in data:
                data[key]["message"] = data[key]["message"].replace(
                    "uBlock₀", "FUNSHIELD"
                ).replace("uBlock Origin", "FUNSHIELD").replace(
                    "uBlock0", "FUNSHIELD"
                )
        messages.write_text(
            json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )


def patch_background():
    path = SRC / "js" / "background.js"
    text = path.read_text(encoding="utf-8")
    text = text.replace(
        "webrtcIPAddressHidden: false,", "webrtcIPAddressHidden: true,"
    )
    text = text.replace("uiAccentCustom: false,", "uiAccentCustom: true,")
    text = text.replace("uiAccentCustom0: '#aca0f7',", "uiAccentCustom0: '#7c5cfc',")
    text = text.replace(
        """    netWhitelistDefault: [
        'chrome-extension-scheme',
        'moz-extension-scheme',
    ],""",
        """    netWhitelistDefault: [
        'chrome-extension-scheme',
        'moz-extension-scheme',
        'funsearchapp.netlify.app',
        'uczomhfuxnvmgccyyztv.supabase.co',
    ],""",
    )
    path.write_text(text, encoding="utf-8")


def patch_assets():
    path = SRC / "assets" / "assets.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    enable = {
        "ublock-annoyances",
        "ublock-cookies-easylist",
        "easylist-annoyances",
        "easylist-chat",
        "easylist-newsletters",
        "easylist-notifications",
    }
    for key, entry in data.items():
        if key in enable and isinstance(entry, dict) and entry.get("off") is True:
            entry.pop("off", None)
    path.write_text(json.dumps(data, indent="\t") + "\n", encoding="utf-8")


def patch_about():
    path = SRC / "about.html"
    text = path.read_text(encoding="utf-8")
    text = text.replace("<title>uBlock — About</title>", "<title>FUNSHIELD — About</title>")
    path.write_text(text, encoding="utf-8")


def write_notice():
    (SRC / "FUNSHIELD.txt").write_text(
        "FUNSHIELD is Funxplorer's bundled content blocker.\n"
        "It is based on uBlock Origin 1.74.0 by Raymond Hill and contributors.\n"
        "uBlock Origin is licensed under the GNU General Public License v3.\n"
        "See LICENSE.txt. Source: https://github.com/gorhill/uBlock\n",
        encoding="utf-8",
    )
    (SRC / "img" / "funshield.svg").write_text(SHIELD_SVG, encoding="utf-8")


def pack_xpi():
    if XPI.exists():
        XPI.unlink()
    with zipfile.ZipFile(XPI, "w", zipfile.ZIP_DEFLATED) as zf:
        for dirpath, dirnames, filenames in os.walk(SRC):
            dirnames[:] = [d for d in dirnames if d != "META-INF"]
            for name in filenames:
                full = Path(dirpath) / name
                rel = full.relative_to(SRC).as_posix()
                if rel.startswith("META-INF/"):
                    continue
                zf.write(full, rel)


def main():
    if not (SRC / "manifest.json").exists():
        raise SystemExit("Extract uBlock into _src first")
    patch_manifest()
    patch_locales()
    patch_background()
    patch_assets()
    patch_about()
    write_notice()
    pack_xpi()
    print("wrote", XPI, "size", XPI.stat().st_size)


if __name__ == "__main__":
    main()
