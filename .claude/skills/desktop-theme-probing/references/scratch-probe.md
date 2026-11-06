# Scratch browser-chrome probes

A bare `SKILL.md` §N means `desktop-theme-probing`'s SKILL.md.

## 1. The skeleton

```js
add_task(async function dumpGeometry() {
  for (let config of [/* one entry per cell */]) {
    let win = await BrowserTestUtils.openNewBrowserWindow();
    win.document.documentElement.removeAttribute("remotecontrol");
    let browser = win.gBrowser.selectedBrowser;
    await BrowserTestUtils.loadURIString({ browser, uriString: "https://example.com/" });
    // Apply the cell, such as a window height:
    // let resized = BrowserTestUtils.waitForEvent(
    //   win, "resize", false, () => win.innerHeight <= config.height
    // );
    // win.resizeTo(700, config.height);
    // await resized;
    // …drive the UI in `win`, then:
    let m = await SpecialPowers.spawn(browser, [], () => {
      let round = n => Math.round(n * 100) / 100;
      let el = content.document.querySelector(/* … */);
      return {
        viewportH: content.innerHeight,
        h: round(el.getBoundingClientRect().height),
      };
    });
    info(`PROBE ${JSON.stringify(config)} ${JSON.stringify(m)}`);
    Assert.ok(true, `measured ${JSON.stringify(config)}`);
    await BrowserTestUtils.closeWindow(win);
  }
});
```

Then `rg PROBE` the log.

- **Sweep, and emit one JSON blob per configuration**, not a line per number.

## 2. Register and run it

Add the probe's filename to the `browser.toml` beside it, then run the file by
path. A missing entry fails as "could not find any mochitests
under the following test path(s)" with a spelling hint.

```sh
./mach mochitest --headless <path/to/browser_probe.js> > artifacts/probe.log 2>&1
echo "exit=$?"
```

Redirect to a file rather than piping the run through `tail` or `grep`.
`--headless` is the flag the harness takes. `MOZ_HEADLESS=1` in the environment
also reaches the browser, but the manifests' `headless` condition then stays
false.

Confirm the run happened before reading any result. A harness error scores as
zero tests rather than as an error, so a summary of `Passed: 0` with nothing
`TEST-UNEXPECTED-FAIL` is a run that never started.

## 3. Traps

- **Include an `Assert.ok(true, …)`** so the harness does not fail the file
  with "This test contains no passes, no fails and no todos."
- **Use the component's own test utils** where it has ones that drive it across
  the process boundary.
- **A `SpecialPowers.spawn` task runs in a system-principal sandbox**, so `eval`
  and `new Function` throw on any page, under
  `security.allow_eval_with_system_principal`; the error names CSP. Share a
  measurement helper as a closure-free top-level function passed as the spawn
  callback, not as source rebuilt in content.
