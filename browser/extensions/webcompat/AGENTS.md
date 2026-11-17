## Intervention rules

Interventions are **individual JSON files**, one per site. Each file lists the Bugzilla bug numbers, the affected URLs, the type of breakage, and the interventions to apply.

Pick the intervention type that fits the breakage. When more than one type would work, prefer the least-invasive one — for example, hiding an unsupported-browser banner with CSS or `hide_messages` is preferable to shipping a `ua_string` override, even if the UA override would also resolve the issue. UA overrides are powerful but might change behavior across the whole site, so reach for them only when narrower options aren't viable.

### Choosing an intervention type

- **Inline CSS** in the JSON's top-level `"css"` object — for unsupported-browser banners, layout fixes, anything purely visual.
- **`hide_alerts`** — for `alert()`/`confirm()` dialogs warning that Firefox is unsupported.
- **`hide_messages`** — for removing inline DOM warning banners by CSS selector plus text content (when the selector alone would also affect unrelated elements).
- **`modify_meta_viewport`** — for viewport meta tag overrides.
- **A reusable script** from `injections/js/` (e.g. `use_chrome_useragent.js`, `define_window_chrome.js`, `disable_FastClick.js`). List the bare filename(s) in `content_scripts.js`.
- **`ua_string`** UA override — when the site gates on the UA string and shimming `window.chrome` / `navigator.userAgent` from JS isn't enough.
- **A new bug-specific JS file** — when nothing reusable fits.

### Mapping bug signals to a type

Use the bug's symptoms (from Bugzilla comments, user story, keywords, or live reproduction) to narrow down the intervention type:

- Blocked / `impact:blocked`→ reusable UA (`use_chrome_useragent.js`, `use_chrome_vendor.js`, etc.) / `window.chrome` JS shims if the gate is client-side; `ua_string` override if the server blocks Firefox before any JS runs.
- Unsupported-browser warning / `impact:unsupported-warning` → inline CSS, `hide_messages` or `hide_alerts`.
- "Works with Chrome mask" / `autowebcompat-repro-chrome-mask-fixed:true` → `ua_string` override or reusable UA + in some cases `window.chrome` scripts (`use_chrome_useragent.js`, `define_window_chrome.js`)
- Alert recommending Chrome → `hide_alerts: ["chrome"]`.
- Missing API → look for an existing shim in `injections/js/` before writing one.

#### `ua_string` versus the reusable UA scripts

- `ua_string` rewrites the outgoing `User-Agent` HTTP header, so the server sees it and so does `navigator.userAgent`. Use it when the breakage comes from the server.
- The reusable UA scripts (`use_chrome_useragent.js`, `add_chrome_to_useragent.js`, `remove_firefox_from_useragent.js`, …) only change JS-visible properties; the header is unchanged. Use them if ua_string alone is insufficient to fix the breakage.

#### Picking a `ua_string` value

Before trying `["Chrome"]`, try these options one at a time, in this order, and stop at the first one that fixes the site:

1. `["change_Firefox_to_FireFox"]` — for sites with case-sensitive Firefox sniffing.
2. `["add_Chrome"]` — appends a Chrome token, keeping Firefox in the UA.
3. `["Chrome_with_FxQuantum"]` — Chrome UA with an `FxQuantum` token, so the site can still identify Firefox.
4. `["Chrome", "add_Firefox_as_Gecko"]` — Chrome UA with Firefox added back as a Gecko token.

Only fall back to `["Chrome"]` (full Chrome UA) if none of these work.

The full list of values is `ua_change_functions` in `lib/intervention_helpers.js`.

### Combining reusable scripts

Start with the smallest combination that fixes the site, and add shims only if it still detects Firefox. Common pairings:

- `use_chrome_useragent.js` + `use_chrome_vendor.js` — sites checking both `navigator.userAgent` and `navigator.vendor`.
- `use_chrome_useragent.js` + `define_minimal_window_chrome.js` — the site also probes `window.chrome`.
- `define_window_chrome.js` + `use_chrome_vendor.js` + `define_userAgentData.js` — fuller Chrome impersonation.

If a reusable script almost fits, prefer extending it over copying it into a bug-specific file.
Reusable scripts can be mixed with `hide_alerts`, `hide_messages` and `modify_meta_viewport` in one intervention; codegen merges them into one generated script.

### Writing the JSON

- One file per site: `data/interventions/{bug_id}-{domain}.json`.
- Every intervention entry needs `platforms`, based on the platforms the issue affects:
  - `windows`, `mac`, `linux` and `android` → `["all"]`
  - `windows`, `mac` and `linux` → `["desktop"]`
  - `android` → `["android"]`
  - any other combination → list the values, e.g. `["linux", "mac"]`; for "everywhere except one", existing interventions use `not_platforms` instead, e.g. `"not_platforms": ["linux"]`. Check other interventions files when unsure.
- Pick `issue` from the enum in `intervention_schema.json`.
- Use the narrowest `matches` pattern that covers the breakage; `exclude_matches` can carve out part of a wildcard.
- Every key in the top-level `"css"` object must be referenced by an intervention, and CSS values must be non-empty, or the build fails.

### Bug-specific scripts

Only when nothing in `injections/js/` fits. Name it `injections/js/bug{bug_id}-{domain}-{purpose}.js`, start it with the Mozilla MPL-2.0 header, and reference it by bare filename.

1. Guard so the fix cannot apply twice. Prefer checking whether the fix is already in place (e.g. `if (!window.chrome)`, or the overridden value already matches). Only add a `__firefoxWebCompatFixBug<id>` marker with `Object.defineProperty` when the fixed state can't be told apart from the original, e.g. when wrapping a global function and no quick check shows it is already wrapped:

```js
if (!window.__firefoxWebCompatFixBug1234567) {
  Object.defineProperty(window, "__firefoxWebCompatFixBug1234567", {
    configurable: false,
    value: true,
  });
  // intervention code
}
```

When to guard:

- Overriding global APIs (`setTimeout`, `addEventListener`, etc.)
- Wrapping prototypes that would chain-wrap on re-run
- Anything that errors or misbehaves when executed twice

2. If it changes observable behavior, log it once with `console.info`, like existing bug scripts do. `window.__webcompat` only works in reusable scripts, where codegen appends the logger.

```js
console.info(
  "<what> has been altered for compatibility reasons. See https://bugzil.la/<id> for details."
);
```

## Version bump - skip this step if you are a Firefox web-compatibility intervention agent.

Whenever you create or modify an intervention JSON or script, bump the **middle** component of `"version"` in `browser/extensions/webcompat/manifest.json` (e.g. `159.1.0` → `159.2.0`). Do not change the first or last component.

## Intervention tests

### File and structure

Add a test with every new intervention, unless it is not possible (see "When a test is not possible").

Tests live outside this directory, in `testing/webcompat/interventions/tests/` at the root of the source tree, one file per bug named `test_<bug_id>_<domain_with_underscores>.py`. Run them with `./mach test-interventions --bugs <bug_id>`, which runs each test with interventions disabled and enabled.

A test file has two tests sharing one check:

- `test_disabled`, marked `without_interventions`: the breakage is still there (e.g. a block message appears, the layout is broken).
- `test_enabled`, marked `with_interventions`: the fix works (e.g. the block message is gone, the page loads normally).

Both are also marked `@pytest.mark.asyncio`. Match the platform markers to the intervention's `platforms`, so the test only runs where the intervention applies:

- `["all"]` → no platform marker
- `["desktop"]` → `@pytest.mark.skip_platforms("android")`
- `["android"]` → `@pytest.mark.only_platforms("android")`
- specific OSes → `@pytest.mark.only_platforms(...)` with those OSes, e.g. `only_platforms("linux")`

To wait, use `await client.stall(seconds)` rather than `asyncio.sleep(seconds)`.

### What's possible to test

Most tests load a URL and check that a specific element or text is present or absent. Tests can also:

- Check HTTP requests and responses
- Verify event listeners are added or fired
- Test redirects, scrolling and keyboard input
- Compare before/after screenshots of an element
- Use chrome JS and WebDriver APIs for advanced checks

### When a test is not possible

- The site detects and blocks WebDriver.
- The issue is too intermittent to assert on.
- Reproducing needs a login, captcha, 2FA or VPN requirements.

Also skip the test when one is possible but not worth having:

- It would be fragile, e.g. depending on content, timing or layout details that change often.
- It would be complex, e.g. reimplementing a significant part of platform functionality just to detect the breakage.
