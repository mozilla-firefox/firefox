# Getting reviews

Thorough code reviews are one of Mozilla's ways of ensuring code quality.
Every patch must be reviewed by the module owner of the code, or one of their designated peers.

## Commit message syntax

When submitting your commit(s), use the following syntax to request review for your patch:

```{eval-rst}
.. list-table::
   :header-rows: 1

   * - Request
     - Syntax
     - Description
   * - Single reviewer
     - ``r=reviewer``
     - Request a review from a single reviewer (``reviewer``). Historically, the syntax ``r?reviewer`` requested a review, and ``r=reviewer`` marked the reviewer who accepted the patch before landing. Both can now be used interchangeably when requesting a review.
   * - Multiple reviewers
     - ``r=reviewer1,reviewer2``
     - Request reviews from *both* ``reviewer1`` and ``reviewer2``.
   * - Blocking review
     - ``r=reviewer!``
     - Request a *blocking* review from ``reviewer``. This means that ``reviewer`` *must* review the patch before it can be landed.
   * - Both optional and blocking reviews
     - ``r=reviewer1,reviewer2!``
     - Request a *blocking* review from ``reviewer2``, and an *optional* review from ``reviewer1``.
   * - Review groups
     - ``r=#review-group``
     - Request a review from members of ``#review-group``. A full list can be found below.
   * - Blocking review groups
     - ``r=#review-group!``
     - Request a blocking review from a review group. This will require *at least one member* of the group to approve before landing.
```

For example, the commit syntax to request review from group `group-name` or `developer-nickname` would be:

```
Bug xxxx - explain what you are doing and why r?#group-name

or

Bug xxxx - explain what you are doing and why r?developer-nickname
```

Reviews, and review groups, can be selected or adjusted after submission in the phabricator UI.

Sometimes when publishing a patch, groups will automatically be added as blocking reviewers due to the code being touched. In this case, you may want to check that the reviewers you requested review from are set to blocking as well.

## Choosing reviewers

**Prefer a review group over an individual reviewer.** Groups spread the load across
several people, so your patch is far less likely to sit waiting on someone who is on
holiday, sick, or busy with another project. Only request a specific person when no group
covers the code you are touching, or when someone is already the obvious owner of that
change.

- Run `mach file-info reviewers` on the files you changed (see below); this is usually enough to
  find the right group.
- If your code fits into one of the [review groups](#review-groups) below, request review from that group.
- If you have a mentor assigned on the bug you are fixing, the mentor can usually either also review or find a suitable reviewer on your behalf.
- Otherwise, try looking at the history of the file to see who has modified it recently (for example, `git log <modified-file>`).
- Finally if you are still unable to identify someone, try asking in the [#introduction channel on Matrix](https://chat.mozilla.org/#/room/#introduction:mozilla.org).

### Suggesting reviewers from the command line

`mach file-info reviewers` takes the paths you modified and prints the reviewers to
request:

```shell
./mach file-info reviewers dom/media/mediasink/AudioSink.cpp
```

```
Module reviewer groups (from mots.yaml):
  #media-playback-reviewers (Core: Media Playback)

Herald reviewers (automatically added):
  #media-playback-reviewers (blocking)
```

It reports up to three kinds of suggestion:

- **Module reviewer groups** are the groups of the [modules](/mots/index.md) owning the
  files, taken from the in-tree `mots.yaml`.
- **Herald reviewers** are the groups and individuals that Phabricator's Herald rules add
  by themselves once you submit the patch. Those marked `(blocking)` have to accept the
  patch before it can land, so requesting them up front saves a round trip.
- **Recent reviewers** are the reviewers of recent patches touching the files, read from
  the `r=` lines in the version control history. These are only shown when the two sources
  above have nothing to say, since they are a weaker signal: a file may have last been
  touched by an unrelated cleanup.

Pass several paths at once to get the reviewers for a whole patch, and `--format json` if
you want to consume the output from a script. The Herald rules are downloaded and cached;
use `--offline` to work from the cached copy only.

To see every group name that Phabricator knows about, for example to check the spelling of
one before putting it in a commit message, run:

```shell
./mach file-info reviewer-groups
```

(keeping-track-of-your-review-queue)=

## Keeping track of your review queue

If you review patches, you are strongly encouraged to install the
[MyQOnly add-on](https://addons.mozilla.org/firefox/addon/myqonly/). It
polls Phabricator, Bugzilla and GitHub and shows a badge in the toolbar
with the number of reviews waiting on you. Most delayed reviews are
simply forgotten ones, and this is the easiest way to avoid that.

Phabricator works from your existing session; Bugzilla needs an API key
in the add-on preferences, and GitHub needs your username. In the
Bugzilla preferences, also enable *Count open needinfos too* so that
needinfo requests are included in the badge.

(getting-attention)=

## Getting attention

We expect reviews to happen within a business day or two, and most of them do.
Requesting review from a [group](#review-groups) rather than an individual is the best way
to get a quick answer, as any member of the group can pick the patch up.

All the delays on this page are in business days: weekends and public holidays
do not count, and reviewers are spread across many time zones and countries, so
their non-working days may not be the same as yours.

If a reviewer hasn't responded after two or three business days:

- Ping the review group's channel, or contact the reviewer directly (either via e-mail or on Matrix).
- Join developers on [Mozilla's Matrix server](https://chat.mozilla.org), and ask if anyone knows why a review may be delayed. Please link to the bug too.
- If the review is still not addressed after a business week, request review from a group covering that code, or from another peer or the module owner listed in [mots](/mots/index.md).

Remember that reviewers are human too, and may have complex reasons that prevent them from reviewing your patch in a timely manner. Be confident in reaching out to your reviewer, but be mindful of the [Mozilla Community Participation Guidelines](https://www.mozilla.org/en-US/about/governance/policies/participation/) while doing so.

For simple documentation changes, reviews are not required.

For more information about the review process, see the {doc}`Code_Review_FAQ`.

## Review groups

```{eval-rst}
.. list-table::
   :header-rows: 1

   * - Name
     - Owns
     - Members
   * - #anchor-positioning-reviewers
     - Anchor positioning - related style and layout code
     - `Member list <https://phabricator.services.mozilla.com/project/members/216/>`__
   * - #anti-tracking
     - `Core: Anti-Tracking </mots/index.html#core-anti-tracking>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/157/>`__
   * - #build or #firefox-build-system-reviewers
     - The configure & build system
     - `Member list <https://phabricator.services.mozilla.com/project/members/20/>`__
   * - #cookies
     - `Core: Cookies </mots/index.html#core-cookies>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/177/>`__
   * - #cubeb-reviewers
     - cubeb, Gecko's audio input/output library and associated projects (audioipc, cubeb-rs, rust cubeb backends)
     - `Member list <https://phabricator.services.mozilla.com/project/profile/129/>`__
   * - #desktop-theme-reviewers
     - `User interface CSS </mots/index.html#desktop-theme>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/141/>`__
   * - #devtools-reviewers
     - `Firefox DevTools </mots/index.html#devtools>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/153/>`__
   * - #dom-core
     - `Core: DOM </mots/index.html#core-document-object-model>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/178/>`__
   * - #dom-worker-reviewers
     - DOM Workers
     - `Member list <https://phabricator.services.mozilla.com/project/members/146/>`__
   * - #dom-storage-reviewers
     - DOM Storage
     - `Member list <https://phabricator.services.mozilla.com/project/members/147/>`__
   * - #extension-reviewers
     - `WebExtensions </mots/index.html#webextensions>`__ and `Toolkit::Add-ons Manager </mots/index.html#add-ons-manager>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/130/>`__
   * - #fluent-reviewers
     - Fluent (FTL) files (translation).
     - `Member list <https://phabricator.services.mozilla.com/project/members/105/>`__
   * - #firefox-source-docs-reviewers
     - Documentation files and its build
     - `Member list <https://phabricator.services.mozilla.com/project/members/118/>`__
   * - #firefox-ux-team
     - User experience (UX)
     - `Member list <https://phabricator.services.mozilla.com/project/members/91/>`__
   * - #firefox-svg-reviewers
     - SVG-related changes
     - `Member list <https://phabricator.services.mozilla.com/project/members/97/>`__
   * - #frontend-codestyle-reviewers
     - ESLint, Prettier or Stylelint configurations.
     - `Member list <https://phabricator.services.mozilla.com/project/members/208/>`__
   * - #android-reviewers
     - Fenix, Focus and Android Components.
     - `Member list <https://phabricator.services.mozilla.com/project/members/200/>`__
   * - #geckoview-reviewers
     - GeckoView
     - `Member list <https://phabricator.services.mozilla.com/project/members/92/>`__
   * - #gfx-reviewers
     - Graphics code
     - `Member list <https://phabricator.services.mozilla.com/project/members/122/>`__
   * - #gtk-reviewers
     - GTK/Linux-specific widget code (widget/gtk)
     - `Member list <https://phabricator.services.mozilla.com/project/members/249/>`__
   * - #intermittent-reviewers
     - Test manifest changes
     - `Member list <https://phabricator.services.mozilla.com/project/members/110/>`__
   * - #ipc-reviewers
     - `Core: IPC </mots/index.html#core-ipc>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/145/>`__
   * - #layout-reviewers
     - Layout
     - `Member list <https://phabricator.services.mozilla.com/project/members/126/>`__
   * - #layout-grid-reviewers
     - layout/grid
     - `Member list <https://phabricator.services.mozilla.com/project/members/215/>`__
   * - #linter-reviewers
     - tools/lint/*
     - `Member list <https://phabricator.services.mozilla.com/project/members/119/>`__
   * - #mac-reviewers
     - Mac-specific code
     - `Member list <https://phabricator.services.mozilla.com/project/members/149/>`__
   * - #media-playback-reviewers
     - `Media playback <https://wiki.mozilla.org/Modules/All#Media_Playback>`__
     - `Member list <https://phabricator.services.mozilla.com/project/profile/159/>`__
   * - #mobiletest-reviewers
     - Android UI test/framework and UI test infra code
     - `Member list <https://phabricator.services.mozilla.com/project/members/201/>`__
   * - #mozbase
     - Mozbase
     - `Member list <https://phabricator.services.mozilla.com/project/members/113/>`__
   * - #mozbase-rust
     - Mozbase in Rust
     - `Member list <https://phabricator.services.mozilla.com/project/members/114/>`__
   * - #necko-reviewers
     - network code (aka necko, aka netwerk)
     - `Member list <https://phabricator.services.mozilla.com/project/members/127/>`__
   * - #nss-reviewers
     - Network Security Services (NSS)
     - `Member list <https://phabricator.services.mozilla.com/project/members/156/>`__
   * - #pdfjs-reviewers
     - `Core: PDF </mots/index.html#core-pdf>`__ (the `pdf.js <https://github.com/mozilla/pdf.js>`__ viewer and its Gecko integration)
     - `Member list <https://phabricator.services.mozilla.com/project/members/152/>`__
   * - #perftest-reviewers
     - Perf Tests
     - `Member list <https://phabricator.services.mozilla.com/project/members/102/>`__
   * - #permissions or #permissions-reviewers
     - `Permissions </mots/index.html#core-permissions>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/158/>`__
   * - #places-reviewers
     - `Bookmarks & History (Places) </mots/index.html#bookmarks-history>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/186/>`__
   * - #platform-i18n-reviewers
     - Platform Internationalization
     - `Member list <https://phabricator.services.mozilla.com/project/members/150/>`__
   * - #preferences-reviewers
     - Firefox for Desktop Preferences (Options) user interface
     - `Member list <https://phabricator.services.mozilla.com/project/members/132/>`__
   * - #recomp-reviewers or #reusable-components-reviewers
     - UI Widgets, Design Tokens, Storybook
     - `Member list <https://phabricator.services.mozilla.com/project/members/185/>`__
   * - #remote-debugging-reviewers
     - Remote Debugging UI & tools
     - `Member list <https://phabricator.services.mozilla.com/project/members/108/>`__
   * - #search-reviewers
     - `Search </mots/index.html#search>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/169/>`__
   * - #sessionstore or #sessionstore-reviewers
     - `Firefox: Session Restore </mots/index.html#session-restore>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/179/>`__
   * - #spidermonkey-reviewers
     - SpiderMonkey JS/Wasm Engine
     - `Member list <https://phabricator.services.mozilla.com/project/members/173/>`__
   * - #static-analysis-reviewers
     - Static Analysis
     - `Member list <https://phabricator.services.mozilla.com/project/members/120/>`__
   * - #style or #firefox-style-system-reviewers
     - Firefox style system (servo, layout/style).
     - `Member list <https://phabricator.services.mozilla.com/project/members/90/>`__
   * - #supply-chain-reviewers
     - Third-party audits and vendoring (cargo-vet, supply_chain).
     - `Member list <https://phabricator.services.mozilla.com/project/members/164/>`__
   * - #tabbrowser or #tabbrowser-reviewers
     - `Firefox: Tabbed Browser </mots/index.html#tabbed-browser>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/180/>`__
   * - #theme or #desktop-theme-reviewers
     - `Firefox: Theme and Toolkit: Themes </mots/index.html#desktop-theme>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/141/>`__
   * - #toolkit-telemetry-reviewers-rotation
     - `Toolkit :: Telemetry </mots/index.html#telemetry>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/229/>`__
   * - #translations-reviewers
     - `Firefox: Translation </mots/index.html#translation>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/192/>`__
   * - #urlbar-reviewers
     - `Urlbar (Address Bar) </mots/index.html#address-bar>`__
     - `Member list <https://phabricator.services.mozilla.com/project/members/211/>`__
   * - #view-transitions-reviewers or #view-transitions or #vt
     - View Transitions (dom/view-transitions, and the relevant style / layout / gfx code).
     - `Member list <https://phabricator.services.mozilla.com/project/members/217/>`__
   * - #webcompat-reviewers
     - System addons maintained by the Web Compatibility team
     - `Member list <https://phabricator.services.mozilla.com/project/members/124/>`__
   * - #webdriver-reviewers
     - Marionette and geckodriver (including MozBase Rust), and Remote Protocol with WebDriver BiDi, and CDP.
     - `Member list <https://phabricator.services.mozilla.com/project/members/103/>`__
   * - #webgpu-reviewers
     - WebGPU code
     - `Member list <https://phabricator.services.mozilla.com/project/members/170/>`__
   * - #webidl
     - WebIDL
     - `Member list <https://phabricator.services.mozilla.com/project/members/112/>`__
   * - #xpcom-reviewers
     - XPCOM
     - `Member list <https://phabricator.services.mozilla.com/project/members/125/>`__
```

To create a new group, fill a [new bug in Conduit::Administration](https://bugzilla.mozilla.org/enter_bug.cgi?product=Conduit&component=Administration).
See [bug 1613306](https://bugzilla.mozilla.org/show_bug.cgi?id=1613306) as example.
