#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Generates a preferences navigation map by parsing CONFIG_PANES from
preferences.js and resolving display names and descriptions from .ftl files.

Run from the repo root:
    python3 browser/components/aiwindow/models/scripts/generate_prefs_nav_map.py

Output:
    browser/components/aiwindow/models/data/preferences-navigation.json
"""

import json
import re
from pathlib import Path

TOPSRCDIR = Path(__file__).resolve().parents[5]

PREFS_JS = TOPSRCDIR / "browser/components/preferences/preferences.js"
FTL_FILES = [
    TOPSRCDIR / "browser/locales/en-US/browser/preferences/preferences.ftl",
    TOPSRCDIR / "browser/locales/en-US/browser/aiFeatures.ftl",
]
OUTPUT = TOPSRCDIR / "browser/components/aiwindow/models/PreferencesNavMap.sys.mjs"

FTL_VARIABLE_SUBSTITUTIONS = {
    "-brand-short-name": "Firefox",
    "-brand-product-name": "Firefox",
}

# Panes whose description lives on a sibling FTL entry rather than on the
# pane header l10nId itself. Maps paneId -> l10nId of the description source.
PANE_DESCRIPTION_L10N_IDS = {
    "ai": "preferences-ai-controls-description",
    "dnsOverHttps": "preferences-doh-description2",
    "etp": "preferences-etp-status-header",
    "history": "history-section-header",
    "paneProfiles": "preferences-profiles-section-header",
    "personalizeSmartWindow": "smart-window-model-section",
    "translations": "settings-translations-header",
}

_BLOCK_RE = re.compile(r"^([\w-]+)\s*=[^\n]*\n((?:[ \t]+[^\n]*\n?)*)", re.MULTILINE)


def parse_config_panes(js_text):
    """Extract pane id -> {parent, l10nId} from the CONFIG_PANES block."""
    block_match = re.search(
        r"const CONFIG_PANES\s*=\s*Object\.freeze\(\{(.+?)\}\);",
        js_text,
        re.DOTALL,
    )
    if not block_match:
        raise ValueError("CONFIG_PANES block not found in preferences.js")

    panes = {}
    for m in re.finditer(r"(\w+)\s*:\s*\{([^}]+)\}", block_match.group(1)):
        pane_id, body = m.group(1), m.group(2)
        parent_m = re.search(r'parent\s*:\s*["\'](\w+)["\']', body)
        l10n_m = re.search(r'l10nId\s*:\s*["\']([^"\']+)["\']', body)
        panes[pane_id] = {
            "parent": parent_m.group(1) if parent_m else None,
            "l10nId": l10n_m.group(1) if l10n_m else None,
        }
    return panes


def parse_ftl_strings(ftl_files):
    """Return (labels, descriptions) dicts resolved from .ftl files."""
    raw_labels, raw_descriptions = {}, {}

    for ftl_path in ftl_files:
        text = ftl_path.read_text(encoding="utf-8")

        # Capture single-line values including those with { FTL references }.
        # Exclude lines starting with . (attributes) and [ (variants).
        for m in re.finditer(r"^([\w-]+)\s*=\s*([^\n\[\.][^\n]*)$", text, re.MULTILINE):
            if value := m.group(2).strip():
                raw_labels.setdefault(m.group(1), value)

        for m in _BLOCK_RE.finditer(text):
            l10n_id, attrs = m.group(1), m.group(2)
            if heading := re.search(r"\.heading\s*=\s*(.+)", attrs):
                raw_labels[l10n_id] = heading.group(1).strip()
            if desc := re.search(r"\.description\s*=\s*(.+)", attrs):
                raw_descriptions[l10n_id] = desc.group(1).strip()

    def resolve(value):
        def replacer(ref_m):
            ref = ref_m.group(1).strip()
            return FTL_VARIABLE_SUBSTITUTIONS.get(ref) or raw_labels.get(ref, ref)

        return re.sub(r"\{([^}]+)\}", replacer, value)

    return (
        {k: resolve(v) for k, v in raw_labels.items()},
        {k: resolve(v) for k, v in raw_descriptions.items()},
    )


def label_of(pane_id, panes, labels):
    """Resolve a display name for any pane id, including top-level ones."""
    l10n_id = (panes.get(pane_id) or {}).get("l10nId") or ""
    return labels.get(l10n_id) or labels.get(f"pane-{pane_id}-title", pane_id)


def breadcrumb_for(pane_id, panes, labels):
    """Build 'Settings > Parent > Child' by walking up the parent chain."""
    parts = []
    current = pane_id
    while current:
        parts.append(label_of(current, panes, labels))
        current = (panes.get(current) or {}).get("parent")
    return " > ".join(["Settings", *reversed(parts)])


def description_for(pane_id, l10n_id, descriptions, labels):
    """
    Return a description for a pane:
      1. .description attribute directly on the pane's l10nId
      2. Explicit PANE_DESCRIPTION_L10N_IDS override (description or label)
    """
    if not l10n_id:
        return None
    if l10n_id in descriptions:
        return descriptions[l10n_id]
    override_id = PANE_DESCRIPTION_L10N_IDS.get(pane_id)
    if override_id:
        return descriptions.get(override_id) or labels.get(override_id)
    return None


def main():
    panes = parse_config_panes(PREFS_JS.read_text(encoding="utf-8"))
    labels, descriptions = parse_ftl_strings(FTL_FILES)

    nav_map = {
        "about:preferences": {
            "label": "Settings",
            "breadcrumb": "Settings",
            "description": None,
        }
    }

    for pane_id, cfg in panes.items():
        nav_map[f"about:preferences#{pane_id}"] = {
            "label": label_of(pane_id, panes, labels),
            "breadcrumb": breadcrumb_for(pane_id, panes, labels),
            "description": description_for(
                pane_id, cfg["l10nId"], descriptions, labels
            ),
        }

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    json_str = json.dumps(nav_map, indent=2, ensure_ascii=False)
    OUTPUT.write_text(
        "// This Source Code Form is subject to the terms of the Mozilla Public\n"
        "// License, v. 2.0. If a copy of the MPL was not distributed with this\n"
        "// file, You can obtain one at http://mozilla.org/MPL/2.0/.\n\n"
        "// Auto-generated by browser/components/aiwindow/models/scripts/generate_prefs_nav_map.py\n"
        "// Do not edit manually. Run the script to regenerate.\n\n"
        f"export const PREFERENCES_NAV_MAP = {json_str};\n"
    )

    print(f"Generated {len(nav_map)} entries -> {OUTPUT.relative_to(TOPSRCDIR)}")


if __name__ == "__main__":
    main()
