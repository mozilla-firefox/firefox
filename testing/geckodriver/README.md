# geckodriver

Proxy for using W3C [WebDriver] compatible clients to interact with
Gecko-based browsers.

This program provides the HTTP API described by the [WebDriver
protocol] to communicate with Gecko browsers, such as Firefox.  It
translates calls into the [Marionette remote protocol] by acting
as a proxy between the local- and remote ends.

[WebDriver protocol]: https://w3c.github.io/webdriver/#protocol
[Marionette remote protocol]: https://firefox-source-docs.mozilla.org/remote/marionette/
[WebDriver]: https://developer.mozilla.org/en-US/docs/Web/WebDriver

## Installation

Geckodriver can be installed through various distribution channels:

* You can download pre-built binaries for the most common platforms from our [Releases] page on GitHub.
* Alternatively, you can compile it yourself by using:
  * `cargo install geckodriver`, or
  * Checking out the `release` branch or a specific tag.

For a detailed list of changes included in each release, please refer to the [change log].

[change log]: https://github.com/mozilla/geckodriver/blob/release/CHANGES.md
[Releases]: https://github.com/mozilla/geckodriver/releases/latest

## Documentation

* [WebDriver] (work in progress)
  * [Commands](https://developer.mozilla.org/en-US/docs/Web/WebDriver/Commands)
  * [Errors](https://developer.mozilla.org/en-US/docs/Web/WebDriver/Errors)
  * [Types](https://developer.mozilla.org/en-US/docs/Web/WebDriver/Types)

* [Cross browser testing](https://developer.mozilla.org/en-US/docs/Learn/Tools_and_testing/Cross_browser_testing)

* [Selenium](https://www.selenium.dev/documentation/)
  * [C# API](https://seleniumhq.github.io/selenium/docs/api/dotnet/)
  * [JavaScript API](https://seleniumhq.github.io/selenium/docs/api/javascript/)
  * [Java API](https://seleniumhq.github.io/selenium/docs/api/java/)
  * [Perl API](https://metacpan.org/pod/Selenium::Remote::Driver)
  * [Python API](https://seleniumhq.github.io/selenium/docs/api/py/)
  * [Ruby API](https://seleniumhq.github.io/selenium/docs/api/rb/)

* [geckodriver usage](https://firefox-source-docs.mozilla.org/testing/geckodriver/Usage.html)
  * [Supported platforms](https://firefox-source-docs.mozilla.org/testing/geckodriver/Support.html)
  * [Firefox capabilities](https://firefox-source-docs.mozilla.org/testing/geckodriver/Capabilities.html)
  * [Capabilities example](https://developer.mozilla.org/en-US/docs/Web/WebDriver/Capabilities/firefoxOptions#example)
  * [Enabling trace logs](https://firefox-source-docs.mozilla.org/testing/geckodriver/TraceLogs.html)
  * [Analyzing crash data from Firefox](https://firefox-source-docs.mozilla.org/testing/geckodriver/CrashReports.html)

* [Contributing](https://firefox-source-docs.mozilla.org/testing/geckodriver/#for-developers)
  * [Building](https://firefox-source-docs.mozilla.org/testing/geckodriver/Building.html)
  * [Testing](https://firefox-source-docs.mozilla.org/testing/geckodriver/Testing.html)
  * [Releasing](https://firefox-source-docs.mozilla.org/testing/geckodriver/Releasing.html)
  * [Self-serving an ARM build](https://firefox-source-docs.mozilla.org/testing/geckodriver/ARM.html)

## Source code

geckodriver is made available under the [Mozilla Public License].

Its source code can be found in the [Firefox source tree] under testing/geckodriver.
This GitHub repository is only used for issue tracking and making releases.

[Mozilla Public License]: https://www.mozilla.org/en-US/MPL/2.0/
[Firefox source tree]: https://searchfox.org/firefox-main/source/testing/geckodriver

## Custom release builds

If a binary is not available for your platform, it's possibe to create a custom
build using the [Rust] toolchain. To do this, checkout the release tag for the
version of interest and run `cargo build`. Alternatively the latest version may
be built and installed from `crates.io` using `cargo install geckodriver`.

[Rust]: https://rustup.rs/

## Contact

The mailing list for geckodriver discussion is
<https://groups.google.com/a/mozilla.org/g/dev-webdriver>.

There is also a [Matrix](https://wiki.mozilla.org/Matrix) channel on
chat.mozilla.org to talk about using and developing geckodriver in
[#webdriver](https://chat.mozilla.org/#/room/#webdriver:mozilla.org).
