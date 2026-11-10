"use strict";

// A moz-newtab-wallpaper URI is only loadable by privileged content, so this
// loads one from the process about:newtab runs in. The xpcshell test covers
// the parent side; this covers the child branch and the stream it asks for.

const SAVED = "v1-custom-dark-center-1-550e8400-e29b-41d4-a716-446655440000";

// A 1x1 PNG. Saved wallpapers are served as image/jpeg whatever they are, and
// a raster is decoded from its own bytes, so this still loads.
const PNG_BASE64 =
  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

// Resolves either way, so a load that never happens fails as a wrong answer
// rather than as a timeout with nothing to read.
async function loadInContent(browser, spec) {
  return SpecialPowers.spawn(browser, [spec], async src => {
    return new Promise(resolve => {
      let image = new content.Image();
      image.addEventListener(
        "load",
        () => resolve({ loaded: true, width: image.naturalWidth }),
        { once: true }
      );
      image.addEventListener("error", () => resolve({ loaded: false }), {
        once: true,
      });
      image.src = src;
    });
  });
}

add_task(async function test_library_paths_from_content() {
  // asan and tsan open about:newtab well outside the default budget.
  requestLongerTimeout(2);

  await SpecialPowers.pushPrefEnv({
    set: [
      // A preloaded about:newtab has already fired its load event, so opening
      // one would wait for an event that never comes.
      ["browser.newtab.preload", false],
      ["browser.newtabpage.activity-stream.feeds.topsites", false],
      ["browser.newtabpage.activity-stream.feeds.system.topstories", false],
      ["browser.newtabpage.activity-stream.discoverystream.enabled", false],
    ],
  });

  let libraryDir = PathUtils.join(PathUtils.profileDir, "wallpaper", "library");
  await IOUtils.makeDirectory(libraryDir, { createAncestors: true });

  let savedPath = PathUtils.join(libraryDir, SAVED);
  await IOUtils.write(
    savedPath,
    Uint8Array.from(atob(PNG_BASE64), c => c.charCodeAt(0))
  );
  registerCleanupFunction(() =>
    IOUtils.remove(savedPath, { ignoreAbsent: true })
  );

  await BrowserTestUtils.withNewTab("about:newtab", async browser => {
    let saved = await loadInContent(
      browser,
      `moz-newtab-wallpaper://library/${SAVED}`
    );
    Assert.ok(saved.loaded, "a file in the library folder loads in content");
    Assert.equal(saved.width, 1, "the image decoded");

    // A backslash arrives still encoded, so this is one the handler refuses
    // rather than one URI parsing has already collapsed.
    let escaped = await loadInContent(
      browser,
      "moz-newtab-wallpaper://library/sub%5cdir"
    );
    Assert.ok(!escaped.loaded, "a component with a separator does not load");
  });
});
