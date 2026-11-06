# Measuring a rem token at each root

## 1. One launch sweeps every platform's root size

Forcing `root.style.fontSize` makes one launch read a token at every platform's
root:

```sh
./mach python --virtualenv wpt .claude/skills/desktop-theme-probing/scripts/firefox-probe.py \
  --binary <objdir>/dist/bin/firefox --context chrome -e '
let root = document.documentElement, p = document.createElement("div");
root.appendChild(p);
let out = {};
for (let base of ["11px", "12px", "15px", "16px"]) {
  root.style.fontSize = base;
  p.style.width = "var(--space-small)";
  out[base] = getComputedStyle(p).width;
}
root.style.fontSize = ""; p.remove();
return out;'
```

Swap `width` and `--space-small` for the property and token under test
(`font-size` for a `--font-size-*` token).

## 2. This desktop's own root

For this desktop's own value read the computed size off an element set to
`font: message-box`, on a display that reads the real system font: the host's
platform file (`desktop-theme-probing`), section FONT, says whether a headless
run does. Before choosing the display for that run, read §1 "Pick
the display rung by what the measurement needs" in `desktop-theme-probing`'s
`references/running-a-probe.md`.
