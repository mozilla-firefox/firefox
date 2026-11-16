# Media module instructions

Scoped guidance for `dom/media/`. The global `../AGENTS.md` still applies.
These rules do not apply to vendored code, for example
`platforms/ffmpeg/ffmpeg*/`, `platforms/ffmpeg/libav*/` and
`webrtc/transport/third_party/`.

## Read first

Before you change media code, look for a design doc of that area in
`dom/media/docs/` and read it. `dom/media/docs/index.md` lists the docs.

Read `dom/media/docs/Diagnostics.md` when you read a media bug report.

## Review checklists

- Read the checklists in `dom/media/docs/review/` (memory safety, threading,
  async lifecycle, IPC, spec correctness) while you write a change, not only
  when you review one.

## Find the cause

- Fix the cause, not the symptom. If an assertion fails, find why the state
  became reachable. Do not add a guard that tolerates the state.
- For behavior that a web page can see, follow the spec steps. Fix the code
  where Firefox does not match the spec, and keep the assertion.
  `dom/media/docs/Specs.md` lists the spec for each media area.
- If no real flow can break an invariant, assert it with `MOZ_ASSERT`. Do not
  add a clamp or a warning log for it.
- Handle and log a failure that can really occur, for example bad input from
  IPC or a decoder error.

## Threads and task queues

- Most media objects belong to one thread or task queue and assert it, for
  example `MOZ_ASSERT(OnTaskQueue())` in `MediaDecoderStateMachine` and
  `MediaFormatReader`. Keep the change on that thread.
- To reach another thread, dispatch a task or chain a `MozPromise`. Prefer
  this to a lock.
- For state that you share with a thread that cannot take a dispatched task,
  for example the cubeb callback thread in `AudioStream`, use a lock and
  annotate the state with `MOZ_GUARDED_BY`. Follow the rules for the real-time
  audio callback in `dom/media/docs/review/threading.md`.

## Async shutdown

- A promise callback or a dispatched task can run after shutdown. Disconnect
  each `MozPromiseRequestHolder` at shutdown, and check the shutdown state
  before you use the object.

## Process boundaries

- Remote decoders run in the RDD process, the GPU process or a utility
  process. CDMs run in the GMP process or in the MFMediaEngineCDM utility
  process. The code is in `dom/media/ipc/` and `dom/media/gmp/`.
- Do not trust a size, an index or a state that comes from another process.
  Check it where it arrives.

## Platform code

- `platforms/wmf/` builds only on Windows, `platforms/apple/` only on macOS
  and iOS, and `platforms/android/` only on Android.
- `platforms/ffmpeg/ffvpx/` builds on all platforms. The system FFmpeg code
  builds on all platforms except Windows and Android.

## Logging

- Log with the module and the macro that the file already uses, for example
  `gMediaDecoderLog` or `gAudioSinkLog`. Log each new state change. Do not log
  a value that a nearby line already logs.
- `xpcom/docs/logging.md` tells how `MOZ_LOG` and about:logging work.

## Prefs

- Most media prefs are in `modules/libpref/init/StaticPrefList.yaml`. Some are
  in `modules/libpref/init/all.js`. Some desktop-only defaults are in
  `browser/app/profile/firefox.js`, for example `media.gmp-widevinecdm.enabled`.
- A test sets a pref with `SpecialPowers.pushPrefEnv`, or in the `prefs` list
  of its test manifest.

## Tests

- Put a new test next to the existing tests of the code you change. To find
  them, search for tests that use the same API, class or file, and look for a
  `test/`, `tests/`, `gtest/` or `crashtests/` folder in the subsystem.
- Add the test to the manifest of that folder, for example a `mochitest*.toml`,
  `browser.toml`, `chrome.toml`, `crashtests.list`, `reftest.list`, or the
  `moz.build` of a gtest folder.
- Choose the kind of test by what it checks:
  - Behavior that a web page can see: a mochitest, or a WPT when a spec
    defines the behavior.
  - A crash: a crashtest.
  - C++ internals: a gtest.
  - Privileged code or telemetry: a browser or chrome mochitest.
- When no folder of the subsystem fits, put mochitests and crashtests in
  `dom/media/test/` and gtests in `dom/media/gtest/`.
- WPT paths are under `testing/web-platform/tests/`. A Gecko-only WPT goes in
  `testing/web-platform/mozilla/tests/`.
- The media files for the mochitests in `dom/media/test/` are listed in
  `dom/media/test/manifest.js`.
- Do not leave unexplained magic numbers in a test. Name each value that has a
  meaning and say where it comes from, for example a limit in a spec, a field
  size in a codec or container format, a sample rate or block size, or a detail
  of the code under test such as an alignment. Compute the values that depend
  on it from that name instead of repeating literals, and assert the relation
  where it can break, for example with a `static_assert`.
