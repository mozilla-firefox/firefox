This directory contains a Gradle plugin that wraps Nimbus FML. It knows how to process feature manifest definitions and generate Kotlin bindings for the configured features.

It was moved into mozilla-firefox from application-services, which removed its copy in
[#7184](https://github.com/mozilla/application-services/pull/7184).

This is mostly a thin wrapper around the actual code generator whose implementation lives in either:

* hopefully in third_party/application-services/components/support/nimbus-fml and built by mach.
* otherwise from [the application-services repo](https://github.com/mozilla/application-services/tree/main/components/support/nimbus-fml), published to archive.mozilla.org (releases) or the Taskcluster index (nightlies) and resolved by Gradle.
