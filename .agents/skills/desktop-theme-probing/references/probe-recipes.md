# Probe recipes

Recipes for setting up a probe and for finding what a rule does, one heading
per task. `SKILL.md` §2 routes here by heading.

## 1. Open the surface the way its tests do

Before probing any surface, read the feature's browser-chrome tests and the
helpers they import (e.g. the directory's `head.js`, a `*TestUtils.sys.mjs`
module): they show how the product's own tests open the surface, fill it and
wait for it to settle, and a probe can do the same once it undoes what a probe
launch changes (`SKILL.md` §2 "A probe launch is not a user's profile"). The
helpers themselves do not load in a probe: `resource://testing-common/` exists
only under a test harness, so copy the steps a helper takes into the probe (e.g.
`PlacesUtils.history.insert` or `insertMany` in place of
`PlacesTestUtils.addVisits`).

A panel view can live in a template until it is shown: an app menu item stays
out of `document.getElementById` right after `PanelUI.show()`,
which returns undefined, so awaiting it waits for nothing. Reach it
with `PanelMultiView.getViewNode(document, id)` (from
`moz-src:///browser/components/customizableui/PanelMultiView.sys.mjs`), then
poll until `getComputedStyle(el).display` is non-empty before reading.

## 2. Read a part inside a shadow root

A `part` attribute is a list of names: select it with
`shadowRoot.querySelector('[part~="name"]')`, since `[part=name]` misses a
part that lists a second name. A climb by
`parentElement` from a slotted child skips the shadow tree; climb through
`assignedSlot`, as `pair-walk.py` and `contrast.py` do.

## 3. Install a theme in the probe profile

**An add-on theme.** A theme hosted on addons.mozilla.org is not in the tree, so
install its XPI from `--chrome-setup-async`:
`AddonManager.installTemporaryAddon(new FileUtils.File(path))` (both modules
import from `resource://gre/modules/`), which takes an unsigned XPI too, then
wait for `lwtheme` on the root before resolving. The temporary add-on goes with
the throwaway profile. Pick the variant with `ui.systemUsesDarkTheme`: a theme
with a `dark_theme` applies it when the chrome is dark. When the theme's XPI is
out of reach, zip a `manifest.json` with `"theme": {"colors": {...}}` holding
the keys under test into a stand-in; installTemporaryAddon takes it the same
way. A copy of the real manifest with the proposed colors tests a fix that
belongs in the theme.

**A built-in theme.** A probe profile holds only the default theme until idle
startup installs the others, so enable one from `--chrome-setup-async`: first
`await BuiltInThemes.ensureBuiltInThemes()`
(`resource:///modules/BuiltInThemes.sys.mjs`), then
`(await AddonManager.getAddonByID(id)).enable()` with an id from
`browser/themes/BuiltInThemeConfig.sys.mjs`, then wait for the root's
`theme-effective-id` to name it before resolving. Which token layer each kind
of theme paints is in `desktop-theme-tokens`' `references/token-layers.md` §2.

## 4. Test another platform's rule

A `ui.*` look-and-feel override
in `widget/nsXPLookAndFeel.cpp` makes a platform media feature match anywhere
(`ui.windowsMica=1` for `(-moz-windows-mica)`), but the platform's own sheet
(`browser/themes/windows/`, `osx/`, `linux/`) builds only on that platform. To
test the shipped rule text, read its block from the source file with
`IOUtils.readUTF8` (a chrome global, awaited; there is no module to import),
inject it into the document that loads the sheet (`browser.xhtml` or another
window's document) as
`doc.documentElement.appendChild(doc.createElementNS("http://www.w3.org/1999/xhtml", "style"))`, since a XUL document has no `head`, set the root attribute it
selects on (`customtitlebar`, for one),
read the values, then remove both and read again. When you inject a whole file
whose chrome URL the host platform also ships, disable the host's sheet at that
URL first (`node.sheet.disabled = true`, the node being its `<link>` or its
`<?xml-stylesheet?>` processing instruction), or
both cascade. That checks the cascade, not
the platform's paint; report the paint as environment-limited.

## 5. Lock a pseudo-class

`InspectorUtils.addPseudoClassLock(el, pc)`, then `clearPseudoClassLocks(el)` in
the same call. Confirm each lock with `InspectorUtils.hasPseudoClassLock(el,
pc)` (`SKILL.md` §2). `el.matches(pc)` is no substitute, since it can match
natively (`:-moz-window-inactive` in a headless window) and a bare `:hover`
never matches a non-link element in a quirks-mode page, locked or not (`*:hover`
does). Where it is reachable:

- the Marionette chrome sandbox, as `window.InspectorUtils`;
- a privileged content page such as about:preferences
  (`firefox-probe.py --context content`);
- any page, a web page included, under `firefox-probe.py --sandbox system`;
- a `SpecialPowers.spawn` task.

The devtools MCP's `evaluate_script` runs in a web page's own realm and lacks
it; there `hover_by_uid` delivers a real pointer.

## 6. Match rules to an element

`prop` is a longhand; a shorthand reads `""` off a rule that sets only a
longhand. The filter keeps rules that set `prop` or a custom property its value
references, since a state rule often sets the token rather than the property; a
token set on an ancestor shows only in that ancestor's rules, so run it there
too. The same holds for an inherited property such as `color` or `fill`: an
empty list on an icon or label whose value is set means the rule sits on an
ancestor, such as the button that holds it. Never filter on `cssText`: an outer
rule's `cssText` contains every nested rule, so a hit there alone means the
nested rule did not apply, and some entries have no `cssText` at all. A nested
rule's `selectorText` is the bare `&:hover`, so the snippet walks `parentRule`
for the outer selectors.

```js
const rules = [...InspectorUtils.getMatchingCSSRules(el)];
const names = new Set([prop]);
for (const r of rules) {
  const v = r.style?.getPropertyValue(prop) ?? "";
  for (const [, n] of v.matchAll(/var\((--[\w-]+)/g)) names.add(n);
}
rules
  .filter(r => [...names].some(n => r.style?.getPropertyValue(n)))
  .map(r => {
    const sel = [];
    for (let x = r; x; x = x.parentRule) {
      sel.unshift(x.selectorText ?? x.conditionText ?? "");
    }
    return `${r.parentStyleSheet?.href?.split("/").pop()}: ${sel.join(" ")}`;
  });
```

## 7. Find the layer a declaration sits in

This probe checks `desktop-theme-tokens` §3's rule that an unlayered
redefinition outranks every token layer at the live document. It runs in the
chrome context, or in content through `firefox-probe.py --sandbox system`
(`InspectorUtils` and `ChromeUtils` need a system principal). Set `el` and
`name`.

```js
const cls = o => ChromeUtils.getClassName(o);
const layerOf = rule => {
  for (let r = rule; r; ) {
    if (cls(r) == "CSSLayerBlockRule") return r.name || "(anonymous)";
    if (r.parentRule) { r = r.parentRule; continue; }
    const owner = r.parentStyleSheet?.ownerRule;
    if (owner && cls(owner) == "CSSImportRule" && owner.layerName !== null) return owner.layerName || "(anonymous)";
    r = owner;
  }
  return null; // unlayered
};
// The nearest element that declares `name` decides the inherited value.
const declarers = (el, name) => {
  for (let n = el; n; n = n.parentElement || n.getRootNode().host || null) {
    const found = InspectorUtils.getMatchingCSSRules(n)
      .filter(r => r.style?.getPropertyValue(name))
      .map(r => ({ rule: r, layer: layerOf(r), value: r.style.getPropertyValue(name).trim() }));
    if (n.style?.getPropertyValue(name)) found.push({ rule: null, layer: null, value: n.style.getPropertyValue(name) });
    if (found.length) return { element: n, found };
  }
  return null;
};
const d = declarers(el, name);
const read = () => getComputedStyle(el).getPropertyValue(name).trim();
const before = read();
const saved = d.found.filter(f => f.rule && f.layer === null)
  .map(f => [f.rule, f.rule.style.getPropertyValue(name), f.rule.style.getPropertyPriority(name)]);
for (const [r] of saved) r.style.removeProperty(name);
const without = read();
for (const [r, v, p] of saved) r.style.setProperty(name, v, p);
return { element: d.element.localName, found: d.found.map(f => [f.layer, f.value]), before, without, restored: read() };
```

A `without` that snaps to the layered value means the unlayered redefinition is
what paints. `getMatchingCSSRules` lists the rules in cascade order, so the last
normal declaration is the winner unless an earlier one is `!important`, which
reverses the layer order. Removing the property edits the live sheet of that
document only; `restored` should equal `before`. The probe finds the layer of a
`@layer` block, of a `@layer` block around `@media` and of an `@import …
layer()` rule.

## 8. Measure what a narrowed selector drops

When a patch narrows an existing selector (e.g. a compound added or a rule moved
under an ancestor), enumerate the elements that fall out of it and measure one
of each: `[...document.querySelectorAll(oldSelector)].filter(el =>
!el.closest(newScope))`. A result of zero or one in a fresh window means the
population is state-dependent (e.g. a panel not yet populated or a mode not
entered), not that nothing falls out; create it through the product's own path
first, then enumerate.
