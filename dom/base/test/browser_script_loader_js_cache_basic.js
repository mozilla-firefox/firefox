// ev, unordered, and runJSCacheTests are defined in head.js

add_task(async function testDiskCache() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.expose_test_interfaces", true],
      ["dom.script_loader.bytecode_cache.enabled", true],
      ["dom.script_loader.bytecode_cache.strategy", 0],
      ["dom.script_loader.experimental.navigation_cache", false],
    ],
  });

  await runJSCacheTests([
    // A small file shouldn't be saved to the disk.
    {
      title: "small file",
      items: [
        {
          file: "file_js_cache_small.js",
          events: [
            ev("load:source", "file_js_cache_small.js"),
            ev("evaluate:classic", "file_js_cache_small.js"),
            ev("diskcache:disabled", "file_js_cache_small.js"),
          ],
        },
        {
          file: "file_js_cache_small.js",
          events: [
            ev("load:source", "file_js_cache_small.js"),
            ev("evaluate:classic", "file_js_cache_small.js"),
            ev("diskcache:disabled", "file_js_cache_small.js"),
          ],
        },
        {
          file: "file_js_cache_small.js",
          events: [
            ev("load:source", "file_js_cache_small.js"),
            ev("evaluate:classic", "file_js_cache_small.js"),
            ev("diskcache:disabled", "file_js_cache_small.js"),
          ],
        },
        {
          file: "file_js_cache_small.js",
          events: [
            ev("load:source", "file_js_cache_small.js"),
            ev("evaluate:classic", "file_js_cache_small.js"),
            ev("diskcache:disabled", "file_js_cache_small.js"),
          ],
        },
      ],
    },

    // A large file should be saved to the disk on the 4th load, and should be
    // used on the 5th load.  Also the 5th load shouldn't overwrite the cache.
    {
      title: "large file",
      items: [
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:source", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:disabled", "file_js_cache_large.js"),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:source", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:disabled", "file_js_cache_large.js"),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:source", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:disabled", "file_js_cache_large.js"),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:source", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:register", "file_js_cache_large.js"),
            ev("diskcache:saved", "file_js_cache_large.js", false),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:diskcache", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:disabled", "file_js_cache_large.js"),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:diskcache", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:disabled", "file_js_cache_large.js"),
          ],
        },
      ],
    },

    // A file with compile error shouldn't be saved to the disk.
    {
      title: "syntax error",
      items: [
        {
          file: "file_js_cache_large_syntax_error.js",
          events: [
            ev("load:source", "file_js_cache_large_syntax_error.js"),
            ev("diskcache:disabled", "file_js_cache_large_syntax_error.js"),
          ],
        },
        {
          file: "file_js_cache_large_syntax_error.js",
          events: [
            ev("load:source", "file_js_cache_large_syntax_error.js"),
            ev("diskcache:disabled", "file_js_cache_large_syntax_error.js"),
          ],
        },
        {
          file: "file_js_cache_large_syntax_error.js",
          events: [
            ev("load:source", "file_js_cache_large_syntax_error.js"),
            ev("diskcache:disabled", "file_js_cache_large_syntax_error.js"),
          ],
        },
        {
          file: "file_js_cache_large_syntax_error.js",
          events: [
            ev("load:source", "file_js_cache_large_syntax_error.js"),
            ev("diskcache:disabled", "file_js_cache_large_syntax_error.js"),
          ],
        },
      ],
    },
  ]);

  await SpecialPowers.popPrefEnv();
});

add_task(async function testMemoryCache() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.expose_test_interfaces", true],
      ["dom.script_loader.bytecode_cache.enabled", true],
      ["dom.script_loader.bytecode_cache.strategy", 0],
      ["dom.script_loader.experimental.navigation_cache", true],
      ["dom.script_loader.disk_cache_delay_ms", 0],
    ],
  });

  // If in-memory cache is enabled, the disk cache is handled by the
  // SharedScriptCache, and following differences happen:
  //  * diskcache:disabled and diskcache:register are not notified for
  //    each script
  //  * diskcache:noschedule is notified without associated script
  //    if there's no script to be saved

  await runJSCacheTests([
    // A small file should be saved to the memory on the 1st load, and used on
    // the 2nd load.  But it shouldn't be saved to the disk cache.
    {
      title: "small file",
      items: [
        {
          file: "file_js_cache_small.js",
          events: [
            ev("load:source", "file_js_cache_small.js"),
            ev("memorycache:saved", "file_js_cache_small.js"),
            ev("evaluate:classic", "file_js_cache_small.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_small.js",
          events: [
            ev("load:memorycache", "file_js_cache_small.js"),
            ev("evaluate:classic", "file_js_cache_small.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_small.js",
          events: [
            ev("load:memorycache", "file_js_cache_small.js"),
            ev("evaluate:classic", "file_js_cache_small.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_small.js",
          events: [
            ev("load:memorycache", "file_js_cache_small.js"),
            ev("evaluate:classic", "file_js_cache_small.js"),
            ev("diskcache:noschedule"),
          ],
        },
      ],
    },

    // A large file should be saved to the memory on the 1st load, and used on
    // the 2nd load.  Also it should be saved to the disk on the 4th load.
    // Also the 5th load shouldn't overwrite the cache.
    // Once the memory cache is purged, it should be populated from the disk
    // cache response.
    {
      title: "large file",
      items: [
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:source", "file_js_cache_large.js"),
            ev("memorycache:saved", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:memorycache", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:memorycache", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:memorycache", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:saved", "file_js_cache_large.js", false),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:memorycache", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:memorycache", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:noschedule"),
          ],
        },

        {
          clearMemory: true,
          file: "file_js_cache_large.js",
          events: [
            ev("load:diskcache", "file_js_cache_large.js"),
            ev("memorycache:saved", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:memorycache", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:noschedule"),
          ],
        },
      ],
    },

    // A file with compile error shouldn't be saved to any cache.
    {
      title: "syntax error",
      items: [
        {
          file: "file_js_cache_large_syntax_error.js",
          events: [
            ev("load:source", "file_js_cache_large_syntax_error.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_large_syntax_error.js",
          events: [
            ev("load:source", "file_js_cache_large_syntax_error.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_large_syntax_error.js",
          events: [
            ev("load:source", "file_js_cache_large_syntax_error.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_large_syntax_error.js",
          events: [
            ev("load:source", "file_js_cache_large_syntax_error.js"),
            ev("diskcache:noschedule"),
          ],
        },
      ],
    },
  ]);

  await SpecialPowers.popPrefEnv();
});

add_task(async function testMemoryCache_timer() {
  // Do the same large file test as above, with non-zero delay.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.expose_test_interfaces", true],
      ["dom.script_loader.bytecode_cache.enabled", true],
      ["dom.script_loader.bytecode_cache.strategy", 0],
      ["dom.script_loader.experimental.navigation_cache", true],
      ["dom.script_loader.disk_cache_delay_ms", 1000],
    ],
  });

  await runJSCacheTests([
    {
      title: "large file",
      items: [
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:source", "file_js_cache_large.js"),
            ev("memorycache:saved", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:memorycache", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:memorycache", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:memorycache", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:saved", "file_js_cache_large.js", false),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:memorycache", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:memorycache", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:noschedule"),
          ],
        },

        {
          clearMemory: true,
          file: "file_js_cache_large.js",
          events: [
            ev("load:diskcache", "file_js_cache_large.js"),
            ev("memorycache:saved", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:noschedule"),
          ],
        },
        {
          file: "file_js_cache_large.js",
          events: [
            ev("load:memorycache", "file_js_cache_large.js"),
            ev("evaluate:classic", "file_js_cache_large.js"),
            ev("diskcache:noschedule"),
          ],
        },
      ],
    },
  ]);

  await SpecialPowers.popPrefEnv();
});
