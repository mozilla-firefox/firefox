# JavaScript Language Feature Checklist
So you're working on a new JavaScript feature in SpiderMonkey: Congratulations! Here's a set of checklists and guidelines to help you on your way.

## High Level Feature Ship Checklist.
(Note: Some of these pieces can happen in parallel, so it's not necessary to
work directly top-down)

-  ☐ File a meta bug for the feature (for TC39 proposals, when it reaches Stage 2.7).
-  ☐ Leave a needinfo on the meta bug for the DevTools team to check if work is required on their side for this feature. File a DevTools bug if needed.
-  ☐ Send an `Intent to prototype` email to `dev-platform`. This is part of the
  [Exposure Guidelines](https://wiki.mozilla.org/ExposureGuidelines) process. We
  historically haven't been amazing at sending intent-to-prototype emails, but
  we can always get better.
-  ☐ Stage 2 or earlier proposals should be developed under compile time guards,
  disabled by default. Please add a configuration to taskcluster to ensure that
  the code compiles and the tests pass. Here is a [recent example](https://phabricator.services.mozilla.com/D279720).
-  ☐ Create a preference for the feature in `modules/libpref/init/StaticPrefList.yaml`
     and a command line option in `js/src/shell/js.cpp`.
-  ☐ Implement the Feature.
-  ☐ Land feature disabled by pref.
-  ☐ Import the test262 test cases for the feature, or enable them if they're
  already imported. (See [js/src/tests/README.txt](https://searchfox.org/firefox-main/source/js/src/tests/README.txt) for guidance)
-  ☐ Add the `fuzzing:needed` label to the proposal epic in the FFXP project in JIRA to request fuzzing for the feature.
    -  ☐ If the feature introduces a new syntax, it may require a lot of work in the fuzzing engine to support it. Notify the fuzzing team early with the details.
-  ☐ File a bug to ship the feature. Set the `dev-doc-needed` flag so that the MDN team is aware that documentation will be needed.
-  ☐ Send an `Intent to ship` email to `dev-platform`. This is also part of the
  [Exposure Guidelines](https://wiki.mozilla.org/ExposureGuidelines) process.
   -  If we didn't previously send an `Intent to prototype`, use the `Intent to prototype and ship` template instead.
   -  If the feature was implemented by a external contributor, please thank them in th      email.
-  ☐ Ping the fuzzing team to make sure sufficient fuzzing has occurred.
-  ☐ Ship the feature:
    -  ☐ Default the preference to true.
    -  ☐ Double check for any code that is currently `NIGHTLY_BUILD` that should now be built unconditionally, e.g. in `js/src/vm/JSObject.cpp`.
         You may want to run a [central as beta simulation](https://wiki.mozilla.org/Sheriffing/How_To/Beta_simulations#TRUNK_AS_EARLY_BETA)
         to make sure you've caught any lingering `NIGHTLY_BUILD` parts.
    -  ☐ Update `js/xpconnect/tests/chrome/test_xrayToJS.xhtml` for any changes to existing global objects. This is a mochitest, you need to run `mach test js/xpconnect/tests/chrome/test_xrayToJS.xhtml`.
    -  ☐ For new globals, the following tests also need updates:
          `dom/serviceworkers/test/test_serviceworker_interfaces.js`, `tests/mochitest/general/test_interfaces.js`, and `dom/workers/test/test_worker_interfaces.js`. These are mochitests, you need to run, e.g. `mach test dom/serviceworkers/test/test_serviceworker_interfaces.html`.
    -  ☐ Once the feature is shipped, request a release note using the `relnote-firefox` flag in Bugzilla.
-  ☐ Open a followup bug to later remove the preference.


## Supplemental Checklists
### Shipping Consideration Checklist

-  ☐ If it seems possible that the feature will cause webcompat issues,
  consider shipping `NIGHTLY_ONLY` for a cycle or two, to use nightly as an
  attempt to shake out potential webcompat issues. Please set `dev-doc-needed`
  on the bug, so that MDN team can document that the feature is available in
  Nightly.

### Web Platform Integration Checklist

_Sometimes Complexity of the web-platform leaks into JS Feature work_

-  ☐ Ensure the appropriate web-platform tests exist, and are being run.
-  ☐ Is your feature correctly enabled inside of Workers? (They have different
  option set than main thread, and it's easy to forget them!) You may want to
  write a mochitest.

### Syntax Features Checklist

-  ☐ Does `Reflect.parse` correctly parse and return results for your new syntax?
  - `Reflect.parse` tests are interesting as well, because they can be written
    for new syntax before bytecode emission is done.
  -  ☐ Are the locations correct for the new syntax entries in the parse tree?
-  ☐ Are your errors emitted with sensible location info?

### Testing Consideration Checklist

_There's lots of complexity in SpiderMonkey that isn't always captured by the
specification, so the below is some useful guidance to behaviour to pay
attention to that may not be tested by a feature's test262 tests_

-  ☐ How does your feature interact with multiple compartments? What happens if
  references happen across compartments, or if `this` is a
  `CrossCompartmentWrapper`?
-  ☐ Are your error messages being emitted in the correct realm, with the
  correct prototype?
-  ☐ If async functions or promises are involved, are user-code objects
  resolved? If so, does the feature correctly handle [the `.then` property
  behaviour of promise
  resolution?](https://www.stefanjudis.com/today-i-learned/promise-resolution-with-objects-including-a-then-property/)
-  ☐ Have you written some OOM tests for your feature to ensure your OOM
  handling is correct?

#### Web Platform Testing Considerations
-  ☐ Does the feature have to handle exotic objects specially? Consider what
  happens when your feature interacts with the very exotic objects on the web
  platform, such as `WindowProxy`, `Location` (cross-origin objects).
-  ☐ What happens when your feature interacts with
  [X-rays](/dom/scriptSecurity/xray_vision.md)?
