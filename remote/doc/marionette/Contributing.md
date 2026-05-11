# Contributing

If you are new to open source or to Mozilla, you might like this
[tutorial for new Marionette contributors](NewContributors.md).

We are delighted that you want to help improve Marionette!
‘Marionette’ means different a few different things, depending
on who you talk to, but the overall scope of the project involves
these components:

* [_Marionette_] is a Firefox remote protocol to communicate with,
  instrument, and control Gecko-based applications such as Firefox
  and Firefox for mobile.  It is built in to the application and
  written in JavaScript.

  It serves as the backend for the geckodriver WebDriver implementation,
  and is used in the context of Firefox UI tests, reftesting,
  Web Platform Tests, test harness bootstrapping, and in many
  other far-reaching places where browser instrumentation is required.

* [_geckodriver_] provides the HTTP API described by the [WebDriver
  protocol] to communicate with Gecko-based applications such as
  Firefox and Firefox for mobile.  It is a standalone executable
  written in Rust, and can be used with compatible W3C WebDriver clients.

* [_webdriver_] is a Rust crate providing interfaces, traits
  and types, errors, type- and bounds checks, and JSON marshaling
  for correctly parsing and emitting the [WebDriver protocol].

By participating in this project, you agree to abide by the Mozilla
[Community Participation Guidelines].  Here are some guidelines
for contributing high-quality and actionable bugs and code.

[_Marionette_]: ./index.rst
[_geckodriver_]: /testing/geckodriver/index.rst
[_webdriver_]: https://searchfox.org/firefox-main/source/testing/webdriver/README.md
[WebDriver protocol]: https://w3c.github.io/webdriver/webdriver-spec.html#protocol
[Community Participation Guidelines]: https://www.mozilla.org/en-US/about/governance/policies/participation/

## Writing code

Because there are many moving parts involved remote controlling
a web browser, it can be challenging to a new contributor to know
where to start.  Please don’t hesitate to [ask questions]!

The canonical source code repository is [mozilla-central].  Bugs are
filed in the [Remote Protocol :: Marionette] component on Bugzilla, or
[Testing :: Marionette Client and Harness] for issues with the Python
client or test harness.  We also
have a curated set of [good first bugs] you may consider attempting first.

We have collected a lot of good advice for working on Marionette
code in our [code style document], which we highly recommend you read.

[ask questions]: index.rst#Communication
[mozilla-central]: https://searchfox.org/firefox-main/source/remote/marionette/
[Remote Protocol :: Marionette]: https://bugzilla.mozilla.org/buglist.cgi?product=Remote%20Protocol&component=Marionette&resolution=---
[Testing :: Marionette Client and Harness]: https://bugzilla.mozilla.org/buglist.cgi?product=Testing&component=Marionette%20Client%20and%20Harness&resolution=---
[good first bugs]: https://codetribute.mozilla.org/projects/automation?project%3DMarionette%2520Client%2520and%2520Harness
[code style document]: CodeStyle.md

## Next steps

* [Building](Building.md)
* [Debugging](Debugging.md)
* [Testing](Testing.md)
* [Patching](Patches.md)

## Other resources

* [Code style](CodeStyle.md)
* [New Contributor Tutorial](NewContributors.md)
