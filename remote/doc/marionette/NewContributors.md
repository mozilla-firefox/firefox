# New contributors

This page is aimed at people who are new to Mozilla and want to contribute
to Mozilla source code related to Marionette Python tests, WebDriver
spec tests and related test harnesses and tools.

If you run into issues or have doubts, check out the [Resources](#resources)
section below and **don't hesitate to ask questions**. :) The goal of these
steps is to make sure you have the basics of your development environment
working. Once you do, we can get you started with working on an
actual bug, yay!

## Accounts, communication

  1. Set up a [Bugzilla] account (and, if you like, a [Mozillians] profile).
     Please include your Element nickname in both of these accounts so we can work
     with you more easily. For example, Eve Smith would set the Bugzilla name
     to "Eve Smith (:esmith)", where "esmith" is the Element nick.

  2. For a direct communication with us it will be beneficial to setup [Element].
     Make sure to also register your nickname as described in the linked document.

  3. Join our [#webdriver:mozilla.org] channel, and introduce yourself to the
     team. :whimboo, :jdescottes, and :jgraham  are all familiar with Marionette.
     We're nice, I promise, but we might not answer right away due to different
     time zones, time off, etc. So please be patient.

  4. When you want to ask a question on Element, just go ahead an ask it even if
     no one appears to be around/responding.
     Provide lots of detail so that we have a better chance of helping you.
     If you don't get an answer right away, check again in a few hours --
     someone may have answered you in the mean time.

  5. If you're having trouble reaching us over Element, you are welcome to send an
     email to our [mailing list](index.md#communication) instead. It's a good
     idea to include your Element nick in your email message.

[Element]: https://chat.mozilla.org
[#webdriver:mozilla.org]: https://chat.mozilla.org/#/room/#webdriver:mozilla.org
[Bugzilla]: https://bugzilla.mozilla.org/
[Mozillians]: https://mozillians.org/

## Getting the code, running tests

Follow the documentation on [Contributing](Contributing.md) to get a sense of
our projects, and which is of most interest for you. The {ref}`Firefox
Contributors' Quick Reference <firefox-contributors-quick-reference>` explains
how to get the Firefox source code, build it, and run the tests.

## Work on bugs and get code review

Once you are familiar with the code of the test harnesses, and the tests you might
want to start with your first contribution. The necessary steps to submit and verify
your patches are laid out in [Patches](Patches.md).

## Resources

* Search Mozilla's code repository with searchfox to find the [code for
  Marionette] and the [Marionette client/harness].

* More general resources are available in this little [guide] :maja_zf wrote
  in 2015 to help a student get started with open source contributions.

* Textbook about general open source practices: [Practical Open Source Software Exploration]

[code for Marionette]: https://searchfox.org/firefox-main/source/remote/marionette/
[Marionette client/harness]: https://searchfox.org/firefox-main/source/testing/marionette/
[guide]: https://gist.github.com/mjzffr/d2adef328a416081f543
[Practical Open Source Software Exploration]: https://quaid.fedorapeople.org/TOS/Practical_Open_Source_Software_Exploration/html/index.html
