# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Sphinx extension to auto-generate ETP capability matrix during doc build.

How it works:
1. This extension is loaded by Sphinx via docs/conf.py
2. On the 'builder-inited' event, it parses browser/app/profile/firefox.js
   to extract the `browser.contentblocking.features.strict` pref value
3. It parses modules/libpref/init/StaticPrefList.yaml for pref defaults
4. It generates etp-matrix/index.md in the build staging directory
5. Sphinx then renders it to HTML along with the rest of the docs

The FEATURES list below maps feature codes (like "tp", "fp") to their
corresponding prefs. Pref documentation and defaults are sourced from
StaticPrefList.yaml. If a new feature code is added to firefox.js,
it will appear as "UNKNOWN" in the output until FEATURES is updated.

Output is published to:
https://firefox-source-docs.mozilla.org/toolkit/components/antitracking/anti-tracking/etp-matrix/
"""

import re
from pathlib import Path
from urllib.parse import quote

# Other privacy-related prefs that aren't controlled by the ETP feature string
# but are relevant to privacy. These don't change between Standard/Strict modes.
# Format: {category: [(feature_name, normal_pref, pbmode_pref_or_None, description), ...]}
OTHER_PRIVACY_PREFS = {
    "Safe Browsing": [
        (
            "Malware Protection",
            "browser.safebrowsing.malware.enabled",
            None,
            "Checks URLs against Google Safe Browsing malware list.",
        ),
        (
            "Phishing Protection",
            "browser.safebrowsing.phishing.enabled",
            None,
            "Checks URLs against Google Safe Browsing phishing list.",
        ),
        (
            "Downloads Protection",
            "browser.safebrowsing.downloads.enabled",
            None,
            "Checks downloaded file hashes against Google Safe Browsing download protection lists.",
        ),
        (
            "Block Potentially Unwanted Software",
            "browser.safebrowsing.downloads.remote.block_potentially_unwanted",
            None,
            "Blocks downloads flagged as potentially unwanted programs (PUPs) by Safe Browsing.",
        ),
    ],
    "Private Browsing": [
        (
            "Reset Private Browsing",
            "browser.privatebrowsing.resetPBM.enabled",
            None,
            "Clears all private browsing mode data when last private window is closed.",
        ),
        (
            "Show Reset Confirmation",
            "browser.privatebrowsing.resetPBM.showConfirmationDialog",
            None,
            "Shows confirmation dialog before clearing private browsing data on window close.",
        ),
    ],
    "Cookie Behavior": [
        (
            "CHIPS Support",
            "network.cookie.CHIPS.enabled",
            None,
            "Enables Cookies Having Independent Partitioned State (CHIPS) per draft spec.",
        ),
        (
            "Opt-in Cookie Partitioning",
            "network.cookie.cookieBehavior.optInPartitioning",
            "network.cookie.cookieBehavior.optInPartitioning.pbmode",
            "Changes cookieBehavior=5 from dynamic partitioning to block-by-default with opt-in.",
        ),
        (
            "Social Tracker Cookie Blocking",
            "privacy.socialtracking.block_cookies.enabled",
            None,
            "Treats domains on social tracking list as trackers for cookie blocking.",
        ),
    ],
    "Privacy Headers": [
        (
            "Do Not Track",
            "privacy.donottrackheader.enabled",
            None,
            "Sends DNT: 1 HTTP header with all requests.",
        ),
        (
            "Global Privacy Control",
            "privacy.globalprivacycontrol.enabled",
            "privacy.globalprivacycontrol.pbmode.enabled",
            "Sends Sec-GPC: 1 HTTP header and exposes navigator.globalPrivacyControl=true.",
        ),
        (
            "GPC Functionality",
            "privacy.globalprivacycontrol.functionality.enabled",
            None,
            "Master switch that controls whether GPC signals are sent (requires .enabled to also be true).",
        ),
    ],
    "Fingerprinting Resistance": [
        (
            "Resist Fingerprinting",
            "privacy.resistFingerprinting",
            "privacy.resistFingerprinting.pbmode",
            "Enables comprehensive fingerprinting resistance including canvas noise, reduced timer precision, and spoofed system info.",
        ),
    ],
    "Anti-fraud": [
        (
            "Skip Anti-fraud Resources",
            "privacy.trackingprotection.antifraud.skip.enabled",
            "privacy.trackingprotection.antifraud.skip.pbmode.enabled",
            "Exempts domains with 'fingerprinting' or 'tracking' annotations from blocking if they have 'anti-fraud' annotation.",
        ),
    ],
    "Other Privacy Features": [
        (
            "Strip on Share",
            "privacy.query_stripping.strip_on_share.enabled",
            None,
            "Strips tracking query parameters when copying URLs via context menu 'Copy Link'.",
        ),
    ],
}

# Features controlled by the ETP feature string in firefox.js.
# Each feature has:
# - normal_code: feature code for normal browsing (or None if no separate pref)
# - pb_code: feature code for private browsing (or None if no separate pref)
# - pref_normal: pref name for normal browsing
# - pref_pb: pref name for private browsing (or None if same as normal)
FEATURES = [
    {
        "name": "Tracking Protection",
        "normal_code": "tp",
        "pb_code": "tpPrivate",
        "pref_normal": "privacy.trackingprotection.enabled",
        "pref_pb": "privacy.trackingprotection.pbmode.enabled",
        "desc": "Blocks resources from domains on the Disconnect tracking protection list.",
    },
    {
        "name": "Fingerprinting Blocking",
        "normal_code": "fp",
        "pb_code": None,
        "pref_normal": "privacy.trackingprotection.fingerprinting.enabled",
        "pref_pb": None,
        "desc": "Blocks resources from domains on the Disconnect fingerprinting list.",
    },
    {
        "name": "Fingerprinting Protection",
        "normal_code": "fpp",
        "pb_code": "fppPrivate",
        "pref_normal": "privacy.fingerprintingProtection",
        "pref_pb": "privacy.fingerprintingProtection.pbmode",
        "desc": "Applies fingerprinting protections including canvas randomization and reducing exposed system info.",
    },
    {
        "name": "Cryptominer Blocking",
        "normal_code": "cryptoTP",
        "pb_code": None,
        "pref_normal": "privacy.trackingprotection.cryptomining.enabled",
        "pref_pb": None,
        "desc": "Blocks resources from domains on the Disconnect cryptomining list.",
    },
    {
        "name": "Social Tracker Blocking",
        "normal_code": "stp",
        "pb_code": None,
        "pref_normal": "privacy.trackingprotection.socialtracking.enabled",
        "pref_pb": None,
        "desc": "Blocks resources from domains on the Disconnect social tracking list.",
    },
    {
        "name": "Email Tracking Protection",
        "normal_code": "emailTP",
        "pb_code": "emailTPPrivate",
        "pref_normal": "privacy.trackingprotection.emailtracking.enabled",
        "pref_pb": "privacy.trackingprotection.emailtracking.pbmode.enabled",
        "desc": "Blocks resources from domains on the email tracking list.",
    },
    {
        "name": "Cookie Behavior",
        "normal_code": "cookieBehavior5",
        "pb_code": "cookieBehaviorPBM5",
        "pref_normal": "network.cookie.cookieBehavior",
        "pref_pb": "network.cookie.cookieBehavior.pbmode",
        "desc": "Controls third-party cookie blocking strategy. See [Bug 2016714](https://bugzilla.mozilla.org/show_bug.cgi?id=2016714) for value definitions.",
    },
    {
        "name": "Query Parameter Stripping",
        "normal_code": "qps",
        "pb_code": "qpsPBM",
        "pref_normal": "privacy.query_stripping.enabled",
        "pref_pb": "privacy.query_stripping.enabled.pbmode",
        "desc": "Strips known tracking query parameters from URLs during navigation.",
    },
    {
        "name": "Level 2 Tracking List",
        "normal_code": "lvl2",
        "pb_code": None,
        "pref_normal": "privacy.annotate_channels.strict_list.enabled",
        "pref_pb": None,
        "desc": "Annotates channels with the Level 2 (strict) Disconnect tracking list in addition to Level 1.",
    },
    {
        "name": "Strict Referrer Policy",
        "normal_code": "rp",
        "pb_code": None,
        "pref_normal": "network.http.referer.disallowCrossSiteRelaxingDefault",
        "pref_pb": None,
        "desc": "Prevents referrer policy from being relaxed to unsafe-url for cross-site requests.",
    },
    {
        "name": "Strict Referrer Policy (Top Nav)",
        "normal_code": "rpTop",
        "pb_code": None,
        "pref_normal": "network.http.referer.disallowCrossSiteRelaxingDefault.top_navigation",
        "pref_pb": None,
        "desc": "Applies strict referrer policy to top-level navigation (not just subresources).",
    },
    {
        "name": "Bounce Tracking Protection",
        "normal_code": "btp",
        "pb_code": None,
        "pref_normal": "privacy.bounceTrackingProtection.mode",
        "pref_pb": None,
        "desc": "Clears state for sites used as bounce trackers. See [Bounce Tracking Protection docs](../bounce-tracking-protection) for mode values.",
    },
    {
        "name": "Local Network Access Blocking",
        "normal_code": "lna",
        "pb_code": None,
        "pref_normal": "network.lna.blocking",
        "pref_pb": None,
        "desc": "Blocks public websites from making requests to private IP ranges (RFC1918, loopback, link-local).",
    },
    {
        "name": "Consent Manager Skipping",
        "normal_code": "consentmanagerSkip",
        "pb_code": "consentmanagerSkipPrivate",
        "pref_normal": "privacy.trackingprotection.consentmanager.skip.enabled",
        "pref_pb": "privacy.trackingprotection.consentmanager.skip.pbmode.enabled",
        "desc": "Skips blocking for known consent management platform domains.",
    },
]

# For backwards compatibility and unknown feature detection
KNOWN_CODES = set()
for f in FEATURES:
    if f["normal_code"]:
        KNOWN_CODES.add(f["normal_code"])
    if f["pb_code"]:
        KNOWN_CODES.add(f["pb_code"])


def parse_static_pref_list(yaml_path):
    """
    Parse StaticPrefList.yaml to extract pref defaults and comments.

    Returns dict of {pref_name: {"value": ..., "ifdef_block": str_or_None, "comment": str}}.
    When an #ifdef block wraps the value, the release default is stored in "value"
    and the full raw #ifdef..#endif text is stored in "ifdef_block".
    When the value uses a macro like @IS_NIGHTLY_BUILD@, the macro name is stored
    in "macro" and the release default in "value".
    """
    content = yaml_path.read_text(encoding="utf-8")
    prefs = {}
    lines = content.split("\n")

    for i, line in enumerate(lines):
        match = re.match(r"^-\s*name:\s*(.+)$", line)
        if not match:
            continue

        pref_name = match.group(1).strip()

        # Collect comment from preceding lines
        comment_lines = []
        for j in range(i - 1, -1, -1):
            prev_line = lines[j].strip()
            if prev_line.startswith("#") and not prev_line.startswith("#ifdef"):
                if re.match(r"^#-+$", prev_line):
                    break
                comment_lines.insert(0, prev_line.lstrip("# "))
            elif prev_line == "":
                pass
            else:
                break
        comment = " ".join(comment_lines).strip()

        # Parse the pref block to find value
        value = None
        ifdef_block = None
        macro = None
        in_ifdef = False
        in_else = False
        ifdef_lines = []
        ifdef_nightly_value = None
        ifdef_else_value = None

        for k in range(i + 1, len(lines)):
            pref_line = lines[k]

            if re.match(r"^-\s*name:", pref_line):
                break

            # Track ifdef/else/endif blocks
            ifdef_start = re.match(r"^#ifdef\s+(\w+)", pref_line)
            if ifdef_start:
                in_ifdef = True
                in_else = False
                ifdef_lines = [pref_line]
            elif "#else" in pref_line and in_ifdef:
                in_else = True
                ifdef_lines.append(pref_line)
            elif "#endif" in pref_line and in_ifdef:
                ifdef_lines.append(pref_line)
                ifdef_block = "\n".join(ifdef_lines)
                in_ifdef = False
                in_else = False
            elif in_ifdef:
                ifdef_lines.append(pref_line)

            # Parse value line
            value_match = re.match(r"^\s*value:\s*(.+)$", pref_line)
            if value_match:
                raw_value = value_match.group(1).strip()

                # Check for macros
                if raw_value.startswith("@") and raw_value.endswith("@"):
                    macro = raw_value
                    value = raw_value
                elif in_ifdef and not in_else:
                    ifdef_nightly_value = raw_value.strip('"')
                elif in_ifdef and in_else:
                    ifdef_else_value = raw_value.strip('"')
                elif value is None:
                    value = raw_value.strip('"')

        # For ifdef blocks: the #else value is the release default
        if ifdef_block is not None and ifdef_else_value is not None:
            value = ifdef_else_value
        elif (
            ifdef_block is not None
            and ifdef_nightly_value is not None
            and value is None
        ):
            # ifdef with no else: only defined on nightly
            value = ifdef_nightly_value

        prefs[pref_name] = {
            "value": value,
            "ifdef_block": ifdef_block,
            "macro": macro,
            "comment": comment,
        }

    return prefs


def make_searchfox_link(pref_name, path):
    """Generate a Searchfox URL that links to the pref in the given source file."""
    escaped_pref = re.escape(pref_name)
    if path.endswith(".yaml"):
        query = f"^- name: {escaped_pref}$"
    else:
        # For .js files, search for pref("pref.name",
        query = f'pref\\("{escaped_pref}"'
    url = (
        f"https://searchfox.org/mozilla-central/search"
        f"?q={quote(query)}&path={quote(path)}&case=true&regexp=true"
    )
    return url


def _build_pref_links(pref_name, pref_info, firefox_js_overrides, all_js_prefs):
    """Build Searchfox links for all source files where a pref is found."""
    links = []
    if pref_name in pref_info:
        path = "modules/libpref/init/StaticPrefList.yaml"
        links.append(
            f'<a href="{make_searchfox_link(pref_name, path)}">StaticPrefList.yaml</a>'
        )
    if pref_name in all_js_prefs:
        path = "modules/libpref/init/all.js"
        links.append(f'<a href="{make_searchfox_link(pref_name, path)}">all.js</a>')
    if pref_name in firefox_js_overrides:
        path = "browser/app/profile/firefox.js"
        links.append(f'<a href="{make_searchfox_link(pref_name, path)}">firefox.js</a>')
    if not links:
        return f"`{pref_name}`"
    return f"`{pref_name}`: " + ", ".join(links)


def parse_firefox_js_overrides(firefox_js_path):
    """
    Parse a .js pref file to extract pref values, handling #if/#ifdef blocks.

    Returns dict of {pref_name: {"value": ..., "ifdef_block": str|None}}.
    For prefs inside #if/#ifdef blocks with #else, the #else value is used
    as the release default.
    """
    content = firefox_js_path.read_text(encoding="utf-8")
    overrides = {}

    pref_pattern = re.compile(r'pref\("([^"]+)",\s*([^)]+)\)')

    in_ifdef = False
    in_else = False
    ifdef_lines = []
    ifdef_prefs = {}
    else_prefs = {}

    for line in content.split("\n"):
        stripped = line.strip()

        if re.match(r"^#if\b|^#ifdef\b|^#ifndef\b", stripped) and not in_ifdef:
            in_ifdef = True
            in_else = False
            ifdef_lines = [stripped]
            ifdef_prefs = {}
            else_prefs = {}
            continue
        elif stripped == "#else" and in_ifdef:
            in_else = True
            ifdef_lines.append(stripped)
            continue
        elif stripped.startswith("#endif") and in_ifdef:
            ifdef_lines.append(stripped)
            ifdef_block_text = "\n".join(ifdef_lines)

            all_names = set(ifdef_prefs) | set(else_prefs)
            for pref_name in all_names:
                if pref_name in else_prefs:
                    value = else_prefs[pref_name]
                else:
                    value = ifdef_prefs[pref_name]
                overrides[pref_name] = {
                    "value": value,
                    "ifdef_block": ifdef_block_text,
                }

            in_ifdef = False
            in_else = False
            continue

        if in_ifdef:
            ifdef_lines.append(stripped)

        match = pref_pattern.search(line)
        if match:
            pref_name = match.group(1)
            raw_value = match.group(2).strip()
            stripped_value = raw_value.strip('"')

            if in_ifdef and not in_else:
                ifdef_prefs[pref_name] = stripped_value
            elif in_ifdef and in_else:
                else_prefs[pref_name] = stripped_value
            else:
                overrides[pref_name] = {"value": stripped_value, "ifdef_block": None}

    return overrides


def _get_pref_value(pref_name, pref_info, firefox_js_overrides, all_js_prefs):
    """Get the effective value of a pref, checking sources in override order."""
    # firefox.js overrides have highest priority
    if pref_name in firefox_js_overrides:
        return firefox_js_overrides[pref_name]["value"]
    # all.js overrides come next
    if pref_name in all_js_prefs:
        return all_js_prefs[pref_name]["value"]
    # StaticPrefList.yaml has lowest priority
    if pref_name in pref_info:
        return pref_info[pref_name]["value"]
    return None


def get_standard_defaults(pref_info, firefox_js_overrides, all_js_prefs):
    """
    Compute standard mode defaults from pref sources.

    Returns dict of {feature_code: raw_value}.
    """
    defaults = {}

    for feature in FEATURES:
        normal_code = feature["normal_code"]
        pb_code = feature["pb_code"]
        pref_normal = feature["pref_normal"]
        pref_pb = feature.get("pref_pb")

        if normal_code:
            defaults[normal_code] = _get_pref_value(
                pref_normal, pref_info, firefox_js_overrides, all_js_prefs
            )

        if pb_code and pref_pb:
            defaults[pb_code] = _get_pref_value(
                pref_pb, pref_info, firefox_js_overrides, all_js_prefs
            )

    return defaults


def extract_strict_features(firefox_js_path):
    """Extract the strict mode feature string from firefox.js."""
    content = firefox_js_path.read_text(encoding="utf-8")
    pattern = r'pref\("browser\.contentblocking\.features\.strict",\s*"([^"]+)"\)'
    match = re.search(pattern, content)
    if not match:
        raise ValueError(
            "Could not find browser.contentblocking.features.strict in firefox.js"
        )
    return match.group(1)


def parse_feature_string(feature_str):
    """Parse feature string into dict of {code: enabled}."""
    features = {}
    for raw_token in feature_str.split(","):
        token = raw_token.strip()
        if not token:
            continue
        if token.startswith("-"):
            features[token[1:]] = False
        else:
            features[token] = True
    return features


def parse_content_blocking_prefs(mjs_path):
    """
    Parse the switch in ContentBlockingPrefs.sys.mjs to discover, for each
    feature-string token, which pref(s) it sets and to what value.

    Returns dict of {token: [{"pref": str, "value_expr": str, "gate_pref": str|None}, ...]}.
    The value_expr is the raw RHS as written in the source (e.g.
    "Ci.nsIBounceTrackingProtection.MODE_ENABLED", "true", "5"), so callers can
    render it verbatim without re-encoding the mapping.
    """
    content = mjs_path.read_text(encoding="utf-8")
    cases = {}

    case_re = re.compile(r'^\s*case\s+"([^"]+)"\s*:\s*$', re.MULTILINE)
    matches = list(case_re.finditer(content))

    for idx, m in enumerate(matches):
        token = m.group(1)
        body_start = m.end()
        body_end = matches[idx + 1].start() if idx + 1 < len(matches) else len(content)
        # Stop at default: which terminates the switch
        default_match = re.search(
            r"^\s*default\s*:", content[body_start:body_end], re.MULTILINE
        )
        if default_match:
            body_end = body_start + default_match.start()
        body = content[body_start:body_end]

        # Each assignment looks like:
        #   this.CATEGORY_PREFS[type]["pref.name"] = <expr>;
        # The pref name and expr can wrap across lines, so collapse whitespace
        # before matching.
        flat = re.sub(r"\s+", " ", body)
        assign_re = re.compile(
            r'this\.CATEGORY_PREFS\[type\]\[\s*"([^"]+)"\s*\]\s*=\s*([^;]+?)\s*;'
        )
        # Detect a getBoolPref gate around the assignments.
        gate_re = re.compile(r"Services\.prefs\.getBoolPref\(\s*this\.([A-Z_]+)\s*,")
        gate_match = gate_re.search(flat)
        gate_pref = None
        if gate_match:
            # Resolve this.PREF_FOO to the literal pref string by looking up
            # the class field declaration. Match either `FOO = "..."` (class
            # field) or `FOO: "...",` (object literal) form.
            field_name = gate_match.group(1)
            field_re = re.compile(rf'\b{re.escape(field_name)}\s*[=:]\s*"([^"]+)"')
            field_match = field_re.search(content)
            if field_match:
                gate_pref = field_match.group(1)

        entries = []
        for assign in assign_re.finditer(flat):
            entries.append({
                "pref": assign.group(1),
                "value_expr": assign.group(2).strip(),
                "gate_pref": gate_pref,
            })
        if entries:
            cases[token] = entries

    return cases


def _switch_overrides(code, pref, strict_features, switch_cases):
    """Whether the strict feature string causes ContentBlockingPrefs.sys.mjs to
    assign `pref` for `code`. If so, the strict cell should show the assigned
    value instead of the static default."""
    if not code or not pref or code not in strict_features:
        return False
    token = code if strict_features[code] else f"-{code}"
    return any(entry["pref"] == pref for entry in switch_cases.get(token, []))


def _resolve_strict_value(feature, strict_features, standard_value, switch_cases):
    """Resolve the strict mode value for a feature given the feature string overrides."""
    code = feature["normal_code"]
    if not code:
        return standard_value

    if code not in strict_features:
        return standard_value

    enabled = strict_features[code]

    # Source-of-truth path: ContentBlockingPrefs.sys.mjs assigns the value for
    # this token. Use the raw RHS as written so it stays in sync with the code.
    pref_normal = feature.get("pref_normal")
    if enabled and code in switch_cases:
        for entry in switch_cases[code]:
            if entry["pref"] == pref_normal:
                return entry["value_expr"]
    disable_token = f"-{code}"
    if not enabled and disable_token in switch_cases:
        for entry in switch_cases[disable_token]:
            if entry["pref"] == pref_normal:
                return entry["value_expr"]

    pref_value = standard_value

    # For boolean prefs, the feature string directly sets true/false
    if pref_value in ("true", "false"):
        return "true" if enabled else "false"

    # If the feature string disables it, use the standard default
    if not enabled:
        return standard_value

    return pref_value


def _find_ifdef_block(pref, pref_info, firefox_js_overrides, all_js_prefs):
    """Find ifdef block for a pref across all sources."""
    if not pref:
        return None
    for source in [firefox_js_overrides, all_js_prefs]:
        src_info = source.get(pref, {})
        if src_info.get("ifdef_block"):
            return src_info["ifdef_block"]
    yaml_info = pref_info.get(pref, {})
    return yaml_info.get("ifdef_block")


def _get_footnote_ref(pref, ifdef_block, footnotes):
    """Get or create a footnote for a pref, return the reference string."""
    for idx, (existing_pref, _) in enumerate(footnotes):
        if existing_pref == pref:
            return f"[{idx + 1}]"
    footnotes.append((pref, ifdef_block))
    return f"[{len(footnotes)}]"


def _render_footnotes(footnotes, start_idx):
    """Render footnotes from start_idx to end of list as markdown lines."""
    if start_idx >= len(footnotes):
        return []
    lines = [""]
    for idx in range(start_idx, len(footnotes)):
        pref_name, ifdef_block = footnotes[idx]
        lines.append(f"[{idx + 1}] `{pref_name}` has build-specific defaults:")
        lines.append("")
        lines.append("```")
        for ifdef_line in ifdef_block.split("\n"):
            lines.append(ifdef_line)
        lines.append("```")
        lines.append("")
    return lines


def generate_markdown(
    strict_features,
    standard_defaults,
    pref_info,
    firefox_js_overrides,
    all_js_prefs,
    switch_cases,
):
    """Generate Markdown tables from parsed features."""

    nav_links = [
        "- [Enhanced Tracking Protection](#enhanced-tracking-protection-etp)",
    ]
    for category in OTHER_PRIVACY_PREFS.keys():
        anchor = category.lower().replace(" ", "-").replace("&", "")
        nav_links.append(f"- [{category}](#{anchor})")

    lines = [
        "# Privacy Capabilities Overview",
        "",
        "```{note}",
        "This page is auto-generated by `docs/_addons/etp_matrix.py` from",
        "`browser/app/profile/firefox.js`, `modules/libpref/init/all.js`, and",
        "`modules/libpref/init/StaticPrefList.yaml` during the documentation build.",
        "To modify the content, update the extension or the source files.",
        "```",
        "",
        "This page documents Firefox desktop privacy features and their default configurations.",
        "",
        "**Quick Navigation:**",
        "",
    ]
    lines.extend(nav_links)
    lines.extend([
        "",
        "---",
        "",
        "```{note}",
        "An empty **Private** cell means the feature has no separate private browsing pref ",
        "and inherits the value shown in the corresponding **Normal** cell.",
        "```",
        "",
        "## Enhanced Tracking Protection (ETP)",
        "",
        "Enhanced Tracking Protection features that change between **Standard** and **Strict** modes. ",
        "Users select their ETP mode in Firefox Settings.",
        "",
        "The **Normal** and **Private** columns indicate whether each feature is enabled in normal ",
        "browsing and private browsing modes, respectively.",
        "",
        "Pref defaults are sourced from ",
        "[StaticPrefList.yaml](https://searchfox.org/firefox-main/source/modules/libpref/init/StaticPrefList.yaml), ",
        "[all.js](https://searchfox.org/firefox-main/source/modules/libpref/init/all.js), and ",
        "[firefox.js](https://searchfox.org/firefox-main/source/browser/app/profile/firefox.js) ",
        "(applied in that order). **ETP Strict** additionally enables features based on the ",
        "[`browser.contentblocking.features.strict`](https://searchfox.org/mozilla-central/search?q=%22browser.contentblocking.features.strict%22&path=%5Ebrowser%2Fapp%2Fprofile%2Ffirefox.js%24&case=true&regexp=false) string in firefox.js.",
        "",
    ])

    # Combined Standard + Strict table
    footnotes = []

    lines.extend([
        "| Feature | Standard Normal | Standard Private | Strict Normal | Strict Private |",
        "|:--------|:--------------:|:----------------:|:-------------:|:--------------:|",
    ])

    for feature in FEATURES:
        name = feature["name"]
        normal_code = feature["normal_code"]
        pb_code = feature["pb_code"]
        pref_normal = feature["pref_normal"]
        pref_pb = feature.get("pref_pb")

        # Standard mode values
        std_normal_val = standard_defaults.get(normal_code)
        if pb_code and pref_pb:
            std_pb_val = standard_defaults.get(pb_code)
        else:
            std_pb_val = None

        # Strict mode values
        strict_normal_val = _resolve_strict_value(
            feature, strict_features, std_normal_val, switch_cases
        )
        if pb_code and pref_pb:
            strict_pb_val = _resolve_strict_value(
                {**feature, "normal_code": pb_code, "pref_normal": pref_pb},
                strict_features,
                std_pb_val,
                switch_cases,
            )
        else:
            strict_pb_val = None

        # Format cell values, putting footnote refs in cells for ifdef prefs
        normal_ifdef = _find_ifdef_block(
            pref_normal, pref_info, firefox_js_overrides, all_js_prefs
        )
        pb_ifdef = _find_ifdef_block(
            pref_pb, pref_info, firefox_js_overrides, all_js_prefs
        )

        # If ContentBlockingPrefs.sys.mjs assigns this pref in strict mode, the
        # strict cell shows the assigned value rather than the ifdef footnote,
        # since the assignment overrides whatever the static default would be.
        normal_overridden = _switch_overrides(
            normal_code, pref_normal, strict_features, switch_cases
        )
        pb_overridden = _switch_overrides(
            pb_code, pref_pb, strict_features, switch_cases
        )

        if normal_ifdef:
            fn_ref = _get_footnote_ref(pref_normal, normal_ifdef, footnotes)
            std_normal_status = fn_ref
            strict_normal_status = (
                f"`{strict_normal_val}`" if normal_overridden else fn_ref
            )
        else:
            std_normal_status = f"`{std_normal_val}`"
            strict_normal_status = f"`{strict_normal_val}`"

        if pref_pb:
            if pb_ifdef:
                fn_ref = _get_footnote_ref(pref_pb, pb_ifdef, footnotes)
                std_pb_status = fn_ref
                strict_pb_status = f"`{strict_pb_val}`" if pb_overridden else fn_ref
            else:
                std_pb_status = f"`{std_pb_val}`"
                strict_pb_status = f"`{strict_pb_val}`"
        else:
            std_pb_status = ""
            strict_pb_status = ""

        display_name = f"**{name}**"

        # Build pref links for all source files
        pref_links = []
        if pref_normal:
            pref_links.append(
                _build_pref_links(
                    pref_normal, pref_info, firefox_js_overrides, all_js_prefs
                )
            )
        if pref_pb:
            pref_links.append(
                _build_pref_links(
                    pref_pb, pref_info, firefox_js_overrides, all_js_prefs
                )
            )
        # Surface any gate pref discovered in ContentBlockingPrefs.sys.mjs
        # (e.g. LNA's network.lna.etp.enabled gates network.lna.blocking).
        gate_prefs_seen = set()
        for code in (normal_code, pb_code):
            if not code:
                continue
            for entry in switch_cases.get(code, []):
                gate = entry.get("gate_pref")
                if gate and gate not in gate_prefs_seen:
                    gate_prefs_seen.add(gate)
                    pref_links.append(
                        _build_pref_links(
                            gate, pref_info, firefox_js_overrides, all_js_prefs
                        )
                    )
        pref_text = "<br/>".join(pref_links)

        desc = feature.get("desc", "")
        if desc:
            cell_name = f"{display_name}<br/><small>{desc}<br/>{pref_text}</small>"
        else:
            cell_name = f"{display_name}<br/><small>{pref_text}</small>"

        lines.append(
            f"| {cell_name} | {std_normal_status} | {std_pb_status} "
            f"| {strict_normal_status} | {strict_pb_status} |"
        )

    # ETP table footnotes (placed directly under the table)
    lines.extend(_render_footnotes(footnotes, 0))

    # Unknown features
    unknown = set(strict_features.keys()) - KNOWN_CODES
    if unknown:
        lines.extend([
            "",
            "### Unknown Features",
            "",
            "The following feature codes were found but are not mapped:",
            "",
        ])
        for code in sorted(unknown):
            status = "enabled" if strict_features[code] else "disabled"
            lines.append(f"- `{code}` ({status})")

    # Other privacy prefs tables (footnotes continue global count)
    lines.extend(
        generate_other_privacy_table(
            pref_info, firefox_js_overrides, all_js_prefs, footnotes
        )
    )

    lines.extend([
        "",
        "---",
        "",
        "*Sources: `browser/app/profile/firefox.js`, `modules/libpref/init/StaticPrefList.yaml`, `modules/libpref/init/all.js`*",
    ])

    return "\n".join(lines)


def generate_other_privacy_table(
    pref_info, firefox_js_overrides, all_js_prefs, footnotes
):
    """Generate tables for other privacy-related prefs not controlled by ETP."""
    lines = []

    for category, features in OTHER_PRIVACY_PREFS.items():
        footnote_start = len(footnotes)

        lines.extend([
            "",
            f"## {category}",
            "",
            "| Feature | Normal | Private |",
            "|:--------|:------:|:-------:|",
        ])

        for feature_name, normal_pref, pb_pref, description in features:
            normal_value = _get_pref_value(
                normal_pref, pref_info, firefox_js_overrides, all_js_prefs
            )

            # Put footnote refs in value cells for ifdef prefs
            normal_ifdef = _find_ifdef_block(
                normal_pref, pref_info, firefox_js_overrides, all_js_prefs
            )
            if normal_ifdef:
                normal_status = _get_footnote_ref(normal_pref, normal_ifdef, footnotes)
            else:
                normal_status = f"`{normal_value}`"

            if pb_pref:
                pb_value = _get_pref_value(
                    pb_pref, pref_info, firefox_js_overrides, all_js_prefs
                )
                pb_ifdef = _find_ifdef_block(
                    pb_pref, pref_info, firefox_js_overrides, all_js_prefs
                )
                if pb_ifdef:
                    pb_status = _get_footnote_ref(pb_pref, pb_ifdef, footnotes)
                else:
                    pb_status = f"`{pb_value}`"
            else:
                pb_status = ""

            display_name = f"**{feature_name}**"

            # Build pref links for all source files
            pref_links = []
            for pref in [normal_pref, pb_pref]:
                if pref:
                    pref_links.append(
                        _build_pref_links(
                            pref, pref_info, firefox_js_overrides, all_js_prefs
                        )
                    )
            pref_text = "<br/>".join(pref_links)

            if description:
                lines.append(
                    f"| {display_name}<br/><small>{description}<br/>{pref_text}</small> | {normal_status} | {pb_status} |"
                )
            else:
                lines.append(
                    f"| {display_name}<br/><small>{pref_text}</small> | {normal_status} | {pb_status} |"
                )

        # Footnotes directly under this category's table
        lines.extend(_render_footnotes(footnotes, footnote_start))

    return lines


def validate_prefs_exist(pref_info):
    """
    Validate that all prefs used in FEATURES exist in StaticPrefList.yaml.

    Raises an error with instructions if any are missing.
    """
    missing_prefs = []

    for feature in FEATURES:
        pref_normal = feature["pref_normal"]
        pref_pb = feature.get("pref_pb")

        if pref_normal not in pref_info:
            missing_prefs.append(pref_normal)
        if pref_pb and pref_pb not in pref_info:
            missing_prefs.append(pref_pb)

    if missing_prefs:
        prefs_list = "\n  - ".join(missing_prefs)
        raise ValueError(
            f"ETP feature prefs not found in StaticPrefList.yaml:\n  - {prefs_list}\n\n"
            "To fix this:\n"
            "1. If the pref was renamed, update docs/_addons/etp_matrix.py FEATURES list\n"
            "2. If the pref is new, add it to modules/libpref/init/StaticPrefList.yaml\n"
            "   with a descriptive comment above the entry"
        )


def generate_etp_matrix(app):
    """Generate ETP matrix markdown file during Sphinx build."""
    import sphinx.util.logging

    logger = sphinx.util.logging.getLogger(__name__)

    topsrcdir = Path(app.confdir).parent

    firefox_js = topsrcdir / "browser" / "app" / "profile" / "firefox.js"
    if not firefox_js.exists():
        raise FileNotFoundError(
            f"Could not find {firefox_js}, cannot generate ETP matrix"
        )

    all_js = topsrcdir / "modules" / "libpref" / "init" / "all.js"
    if not all_js.exists():
        raise FileNotFoundError(f"Could not find {all_js}, cannot generate ETP matrix")

    static_pref_list = (
        topsrcdir / "modules" / "libpref" / "init" / "StaticPrefList.yaml"
    )
    if not static_pref_list.exists():
        raise FileNotFoundError(
            f"Could not find {static_pref_list}, cannot generate ETP matrix"
        )

    content_blocking_prefs = (
        topsrcdir
        / "browser"
        / "components"
        / "protections"
        / "ContentBlockingPrefs.sys.mjs"
    )
    if not content_blocking_prefs.exists():
        raise FileNotFoundError(
            f"Could not find {content_blocking_prefs}, cannot generate ETP matrix"
        )

    output_dir = (
        Path(app.outdir)
        / "_staging"
        / "toolkit"
        / "components"
        / "antitracking"
        / "anti-tracking"
        / "etp-matrix"
    )
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / "index.md"

    pref_info = parse_static_pref_list(static_pref_list)

    validate_prefs_exist(pref_info)

    all_js_prefs = parse_firefox_js_overrides(all_js)

    firefox_js_overrides = parse_firefox_js_overrides(firefox_js)

    standard_defaults = get_standard_defaults(
        pref_info, firefox_js_overrides, all_js_prefs
    )

    feature_str = extract_strict_features(firefox_js)
    strict_features = parse_feature_string(feature_str)

    switch_cases = parse_content_blocking_prefs(content_blocking_prefs)

    markdown = generate_markdown(
        strict_features,
        standard_defaults,
        pref_info,
        firefox_js_overrides,
        all_js_prefs,
        switch_cases,
    )
    output_path.write_text(markdown, encoding="utf-8")
    logger.info(f"Generated ETP matrix: {output_path}")


def setup(app):
    """Sphinx extension setup."""
    app.connect("builder-inited", generate_etp_matrix)
    return {
        "version": "1.0",
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
