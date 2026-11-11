# Pairing system colors per state and per selector

The recipe behind `SKILL.md` §4 "Pair the system colors per state and per
selector".

## 1. Two names can be one pair

Resolve a name the way Windows does, since the
forced-colors pairing is judged there: in `NativeGetColor` of
`widget/windows/nsLookAndFeel.cpp`, the `IsHighlightColor()` and
`IsHighlightTextColor()` predicates list the IDs that collapse onto
`COLOR_HIGHLIGHT` and `COLOR_HIGHLIGHTTEXT`, and the `switch` below them maps
the rest; there is no ID to `COLOR_*` table to grep. `desktop-theme-probing`'s
`platform-windows.md`, section CONTRAST, lists the names that resolve to one
color under high contrast, so compare states by resolved value, not by name.

## 2. Per state

For each state the surface has (e.g. default, hover, active, focus, selected,
checked, disabled, open), including the states the patch leaves to an existing
rule, list the background, the text, the icon fill and the outline that resolve
together under forced colors, and every descendant the surface shows only in
some modes or states (e.g. a label, a badge). A row that mixes pairs is a
finding, and so is a state that recolors the text but not the icon fill, or the
reverse.

## 3. Per descendant, with pair-walk.py

Walk every descendant a state repaints. A patch that moves a state's
background onto a new pair leaves each descendant with its own `color` or
`fill` on the old one, often in a rule the diff never touched.

**In chrome on Linux and macOS, pass a `--setup-file` that sets SKILL.md §2's
override** (`forced-color-filter.md` §2 "Putting chrome under forced colors").
The prefs reach only the `prefers-contrast` regime there. Without the override
the walk still judges a surface whose resolved color equals a system color, and
exits 1 for an entry that judged nothing (`checked: 0`), so read
`regime.forcedColorsActive` before taking a clean walk as a forced-colors check.

`scripts/pair-walk.py` locks the state, maps each
visible descendant's surface to its system colors and lists every foreground
outside that pair; its `--help` has what it checks and its flags. It locks a
pseudo-class state only in chrome, so a `--context content` run takes
`--state default` and exits 1 on any other state. The walk matches surfaces to
pairs by this platform's values, so two pairs that share a value here pass a
mix of them, and a clean walk is a verdict on this palette only.

## 4. Per selector

Pair by selector as well as by state. A new declaration and the one it pairs
with must match the same elements, so compare their selectors' scope; a member
of a selector list that can never be in the state (not hit-testable, or the
state attribute never lands on it) pairs nothing. Copy the replaced rule's own
conditions (e.g. a `:not()` exclusion, an attribute or ancestor scope) into the
new selector, or the new value paints in the mode the old one skipped. An
element that a more specific rule exempts from the declaration you replace (the
old value reset in the same state) needs the same exemption from the new one, so
search the sheets for other rules in that state that set the property.
