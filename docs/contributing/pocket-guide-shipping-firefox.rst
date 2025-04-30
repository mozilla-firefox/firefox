Pocket Guide: Shipping Firefox
==============================

*Estimated read time:* 15min


Introduction
------------

The purpose of this document is to provide a high level understanding of
how Mozilla ships Firefox. With the intention of helping new Mozillians
(and those who would like a refresher) understand the basics of our
release process, tools, common terms, and mechanisms employed in
shipping Firefox to our users. Often this document will introduce a
concept, explain how it fits into the process, and then provide a link
to learn more if interested.

Repositories & Channels
-----------------------

Shipping Firefox follows a software release :ref:`train model <train model>`
along 3 primary code :ref:`repositories <repositories>`; mozilla-central
(aka “m-c”), mozilla-beta, and mozilla-release. Each of these repositories are
updated within a defined cadence and built into one of our Firefox
products which are released through what is commonly referred to as
:ref:`Channels <channels>`: Firefox Nightly, Firefox Beta, and Firefox Release.

`Firefox Nightly <https://whattrainisitnow.com/release/?version=nightly>`__ offers access
to the latest feature work still under active development. Released every 12 hours with all
the changes that have :ref:`landed <landing>` on mozilla-central for Desktop and Android.

Every `4 weeks <https://whattrainisitnow.com/calendar/>`__, we :ref:`merge <merge>` the code
for Desktop and Android builds from mozilla-central to our mozilla-beta branch. New code or
features can be added to mozilla-beta outside of this 4 week cadence but are required to land
on mozilla-central before being :ref:`uplifted <uplift>` to mozilla-beta.

`Firefox Beta <https://whattrainisitnow.com/release/?version=beta>`__ is for developers and early
adopters who want to see and test what’s coming next in Firefox. We ship new Desktop and Android
Beta builds three times a week.

.. note::

  The first and second beta builds of a new cycle are shipped to a
  subset of our Beta population. The full Beta population gets updated
  starting with Beta 3 only.

.. note::

  **Firefox Developer Edition** is a separate Desktop-only product based on
  the mozilla-beta branch and is specifically tailored for Web Developers.

`Firefox Release <https://whattrainisitnow.com/release/?version=release>`__ is updated every 4 weeks
when a given version reaches the end of its Beta cycle. This is the primary product we ship to end users.
While a release is live, interim updates (dot releases) are used to ship important bug fixes prior to
the next major release. These can happen on an as-needed basis when there is an important-enough
:ref:`driver <dot release drivers>` to do so (such as a critical bug severely impairing the usability
of the product for some users). In order to provide better predictability, there is also a planned
dot release scheduled for two weeks after the initial go-live for less-critical fixes and other
:ref:`ride-along fixes <ride alongs>` deemed low-risk enough to include.

.. note::
  `Firefox ESR (Extended Support Release) <https://whattrainisitnow.com/release/?version=esr>`__ is
  a separate product intended for enterprise use. Major updates are rolled out once per year to maintain
  stability and predictability. ESR also contains a number of enterprise policy options not available on
  the standard Firefox Release channel. Minor updates are shipped in sync with the Firefox Release
  schedule and generally only contain security and select quality fixes.

Further Reading/Useful links:

-  `Firefox Trains <https://whattrainisitnow.com/>`__
-  `Release Calendar <https://whattrainisitnow.com/calendar/>`__
-  `Firefox Release Process <https://wiki.mozilla.org/Release_Management/Release_Process>`__
-  `Firefox Delivery dashboard <https://mozilla.github.io/delivery-dashboard/>`__

Landing Code and Shipping Features
----------------------------------

Mozillians (those employed by MoCo and the broader community) land lots of code in
the Mozilla repositories: fixes, enhancements, compatibility, new features, etc. which are
managed by git. All code development is tracked in
:ref:`Bugzilla <bugzilla>`, reviewed in :ref:`Phabricator <Phabricator>`, and then checked
into the mozilla-central repository using :ref:`Lando <Lando>`.

.. note::

  Some teams use :ref:`GitHub <github>` during development but will still be required to use
  Phabricator (tracked in Bugzilla) to check their code into the mozilla-central hg repository.

The standard process for code to be delivered to our users is by ‘riding the trains’, meaning that
it’s landed in mozilla-central to ship in Nightly builds while it waits for the next Beta cycle to
begin. After merging to Beta, the code stabilizes over a 4 week period (along with everything else
that merged from mozilla-central from that development cycle). At the end of the Beta cycle, a
release candidate (:ref:`RC <rc>`) build is generated, tested thoroughly, and eventually is released
as the next major version of Firefox.

Further Reading/Useful links:

-  `Phabricator and why we use it <https://wiki.mozilla.org/Phabricator>`__
-  `Firefox Release Notes Process <https://wiki.mozilla.org/Release_Management/Release_Notes>`__
-  `Firefox Release Notes Nomination <https://wiki.mozilla.org/Release_Management/Release_Notes_Nomination>`__

An exception to this process...
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Not all code can simply wait for the normal train model to be included in a Firefox build.
There are a variety of reasons for this; critical fixes, security concerns, stabilizing a feature
that’s already in Beta, shipping high-priority features/enhancements faster, and so on.

In these situations, an uplift can be requested to take a recent landing in mozilla-central and
merge specific bits to the Beta or Release repository outside the standard train model. After the
request is made, :ref:`Release Management <release management>` will assess the potential risk and
make a decision on whether it will be accepted or not.

While uplifts are generally not the preferred way to ship new feature work, it is understood that there
are times when business needs to do so justify the required effort. Our release process is designed to
have the flexibility to accommodate these requests, though in general they need to be handled on a case
by case basis to determine the suitability. Teams are encouraged to reach out to Release Management in
the `#release-coordination` channel on Slack or `@relman` so their specific needs can be assessed.

Factors that will need to be taken into account include:

-  Size and scope of patches to be uplifted
-  QA availability to test prior to shipping and during development
-  Engineering resources to resolve any conflicts between different development branches
-  String additions/changes which may impact available locales

Further Reading/Useful links:

-  `Patch uplifting rules <https://wiki.mozilla.org/Release_Management/Uplift_rules>`__
-  `Requesting an uplift <https://wiki.mozilla.org/Release_Management/Requesting_an_Uplift>`__

Ensuring build stability
~~~~~~~~~~~~~~~~~~~~~~~~

Throughout the process of landing code in mozilla-central to riding the trains to Firefox Release,
there are many milestones and quality checkpoints from a variety of teams. This process is designed
to ensure a quality and compelling product will be consistently delivered to our users with each new
version. See below for a detailed list of those milestones.

===================================================== ================ ================= =======================================================================================
Milestone                                             Week             Day of Week
----------------------------------------------------- ---------------- ----------------- ---------------------------------------------------------------------------------------
QA Request & Feature technical documentation deadline Nightly W0       Friday            QA feature testing should be requested prior to the start of the Nightly cycle
Merge Day                                             Nightly W1       Monday            Day 1 of the new Nightly Cycle
Feature Complete Milestone                            Nightly W2       Friday            Last day to land risky patches and/or enable new features
QA Test Plan approval due                             Nightly W2       Friday            Last day to provide QA with feature Test Plan sign-offs
Nightly features Go/No-Go decisions                   Nightly W4       Wednesday
Beta release notes draft                              Nightly W4       Wednesday
Nightly soft code freeze start                        Nightly W4       Thursday          Stabilization period in preparation to merge to Beta
QA pre-merge regression testing completed             Nightly W4       Friday
String freeze                                         Nightly W4       Friday            Modification or deletion of strings exposed to the end-users is not allowed
Merge Day                                             Beta W1          Monday            Day 1 of the new Beta cycle
User affecting changes identified & provided to SUMO  Beta W1          Friday
End of Early Beta & intended pref state deadline      Beta W2          Friday            Post-B6
Pre-release sign off                                  Beta W3          Wednesday         Final round of QA testing prior to Release
Go/No-Go for features riding train                    Beta W3          Friday
Firefox RC week                                       Beta W4          Monday            Validating Release Candidate builds in preparation for the next Firefox Release
Release Notes ready                                   Beta W4          Tuesday
What’s new page ready                                 Beta W4          Wednesday
Firefox go-live @ 6am PT                              Release W1       Tuesday           Day 1 of the new Firefox Release to 25% of Release users
Firefox Release bump to 100%                          Release W1       Thursday          Increase deployment of new Firefox Release to 100% of Release users
Scheduled dot release approval requests due           Release W2       Friday            All requests required by EOD
Scheduled dot release go-live                         Release W3       Tuesday           By default, ships when ready. Specific time available upon request.
===================================================== ================ ================= =======================================================================================


The Release Management team (aka “Relman”) monitors and enforces this process to protect the
stability of Firefox. Each member of Relman rotates through end-to-end ownership of a given
:ref:`release cycle <release cycle>`. The Relman owner of a cycle will focus on the overall release,
blocker bugs, risks, backout rates, stability/crash reports, etc. Go here for a complete overview of
the `Relman Release Process Checklist <https://wiki.mozilla.org/Release_Management/Release_Process_Checklist_Documentation>`__.

.. note::

  While Relman continually monitors the overall health of each release, it is the responsibility
  of the engineering organization to ensure that the code they are landing is of high quality and the
  potential risks are understood. Every release has an assigned :ref:`Regression Engineering
  Owner <reo>` (REO) to ensure a decision is made about each regression reported in the release.

Further Reading/Useful links:

-  `Release Tracking Rules <https://wiki.mozilla.org/Release_Management/Tracking_rules>`__
-  `Release Owners <https://wiki.mozilla.org/Release_Management/Release_owners>`__
-  `Commonly used Bugzilla queries for all Channels <https://trainqueries.herokuapp.com/>`__

Enabling/Disabling code (Prefs)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Within Firefox we allow the ability to enable/disable bits of code or entire features using
:ref:`Preferences <preferences>`. There are many reasons why this is useful. For example:

-  Continual development over multiple release cycles without exposing partially-completed
   features to our users
-  Provide the ability to quickly disable a feature if there is a problem found during the
   release process
-  Control features which are experimental or not ready to be shown to a specific channel
   population (e.g. enabled for Beta but disabled for Release)
-  A/B testing via :ref:`telemetry <telemetry>` experiments

.. note::

  :ref:`Nimbus <nimbus>` Pref Rollout is a feature that allows Mozilla to change the state of a
  preference for a targeted set of users, without deploying an update to Firefox. This is
  especially useful when conducting experiments or a gradual rollout of high risk features
  to our Release population.

Further Reading/Useful links:

-  `Mozilla preferences documentation <https://firefox-source-docs.mozilla.org/modules/libpref/index.html>`__
-  `Nimbus documentation <https://experimenter.info/>`__

Release & Feature QA
~~~~~~~~~~~~~~~~~~~~

Release QA is performed regularly and throughout the Release Cycle.
Organized in two-week sprints its primary goals are:

-  Qualifying builds for release
-  Feature testing
-  Product Integrity requests
-  Bug work
-  Community engagement

Features that can have significant impact and/or pose risk to the code
base should be nominated for QA support by the :ref:`feature
owner <feature owner>` in its intended release. This process is kicked
off by filing a :ref:`Product Integrity <product integrity>` team request
:ref:`PI request <pi request>`. These are due by the end of week 2
of the Nightly cycle.

.. note::

  Manual QA testing is only required for features as they go
  through the Beta cycle. Nightly Feature testing is always optional.

Further Reading/Useful links:

-  `QA Feature
   Testing <https://wiki.mozilla.org/QA/Feature_Testing_v2>`__
-  `Release QA
   overview <https://docs.google.com/document/d/1ic_3TO9-kNmZr11h1ZpyQbSlgiXzVewr3kSAP5ML4mQ/edit#heading=h.pvvuwlkkvtc4>`__
-  `PI Request template and
   overview <https://mana.mozilla.org/wiki/pages/viewpage.action?spaceKey=PI&title=PI+Request>`__

Experiments
~~~~~~~~~~~

As we deliver new features to our users, we continually ask ourselves about the potential impacts,
both positive and negative. For many new features, we will run an experiment to gather data around
these impacts. A simple definition of an experiment is a way to measure how a change to our product
affects how people use it.

An experiment has three parts:

1. A new feature that can be selectively enabled
2. A group of users to test the new feature
3. Telemetry to measure how people interact with the new feature

Experiments are managed by an in-house tool called `Experimenter <https://experimenter.services.mozilla.com/>`__.

Further Reading/Useful links:

-  `More about experiments and Experimenter <https://github.com/mozilla/experimenter>`__
-  `Requesting a new Experiment <https://experimenter.services.mozilla.com/experiments/new/>`__
   (Follow the ‘help’ links to learn more)
-  `Telemetry <https://wiki.mozilla.org/Telemetry>`__

Definitions
-----------

.. _approval flag:

**Approval Flag** - A flag that represents a security approval or uplift
request on a patch.

.. _bugzilla:

**Bugzilla** - Web-based general purpose bug tracking system and testing
tool.

.. _channel:

**Channel** - Development channels producing concurrent releases of
Firefox for Windows, Mac, Linux, and Android.

.. _chemspill:

**Chemspill** - Short for Chemical Spill. A chemspill is a rapid
security-driven or critical stsbility dot release of our product.

.. _channel meeting:

**Channel Meeting** - A twice weekly time to check in on the status
of the active releases with the release team.

.. _dot release drivers:

**Dot Release Drivers** - Issues/Fixes that are significant enough to
warrant a minor dot release to the Firefox Release Channel. Usually to
fix a stability (top-crash) or Security (Chemspill) issue.

.. _early beta:

**Early Beta** - Beta releases with the features gated by EARLY_BETA_OR_EARLIER
enabled. The first 2 weeks of Beta releases during the cycle are early beta releases.

.. _feature owner:

**Feature Owner** - The person who is ultimately responsible for
developing a high quality feature. This is typically an Engineering
Manager or Product Manager.

.. _github:

**Github** - Web-based version control and collaboration platform for
software developers

.. _gtb:

**GTB** - Acronym for Go to build.  Mostly used in the release schedule
communication ("Go to build on March 18"), this means that we initiate the
building of a specific release.

.. _landing:

**Landing** - A general term used for when code is merged into a
particular source code repository

.. _lando:

**Lando** - Automated code lander for Mozilla. It is integrated with
our `Phabricator instance <https://phabricator.services.mozilla.com>`__
and can be used to land revisions to various repositories.

.. _merge:

**Merge** - General term used to describe the process of integrating and
reconciling file changes within the mozilla repositories

.. _nightly soft code freeze:

**Nightly Soft Code Freeze** - Last week of the nightly cycle on mozilla-central
just before the merge to beta during which landing risky or experimental code
in the repository is discouraged.

.. _nimbus:

**Nimbus** - Nimbus is a collection of servers, workflows, and
Firefox components that enables Mozilla to remotely control Firefox
clients in the wild based on precise criteria

.. _nucleus:

**Nucleus** - Name of the internal application used by release managers
to prepare and publish release notes. The data in this application is
fetched by mozilla.org.

.. _orange_factor:

**Orange** - Also called flaky or intermittent tests. Describes a state
when a test or a testsuite can intermittently fail.

.. _phabricator:

**Phabricator** - Mozilla’s instance of the web-based software
development collaboration tool suite. Read more about `Phabricator as a
product <https://phacility.com/phabricator/>`__.

.. _pi request:

**PI Request** - Short for Product Integrity Request is a form
submission request that’s used to engage the PI team for a variety of
services. Most commonly used to request Feature QA it can also be used
for Security, Fuzzing, Performance, and many other services.

.. _preferences:

**Preferences** - A preference is any value or defined behavior that can
be set (e.g. enabled or disabled). Preference changes via user interface
usually take effect immediately. The values are saved to the user’s
Firefox profile on disk (in prefs.js).

.. _rc:

**Release Candidate** - Beta version with potential to be a final
product, which is ready to release unless significant bugs emerge.

.. _rc week:

**RC Week** - The week prior to release go-live is known as RC week.
During this week an RC is produced and tested.

.. _release cycle:

**Release Cycle** - The sum of stages of development and maturity for
the Firefox Release Product.

.. _reo:

**Regression Engineering Owner** - A partner for release management
assigned to each release. They both keep a mental state of how we are
doing and ensure a decision is made about each regression reported in
the release. AKA *REO*.

.. _release engineering:

**Release engineering** - Team primarily responsible for maintaining
the build pipeline, the signature mechanisms, the update servers, etc. aka *releng*

.. _release management:

**Release Management** - Team primarily responsible for the process of
managing, planning, scheduling and controlling a software build through
different stages and environments. aka *relman*.

.. _relnotes:

**Relnotes** - Short for release notes. Firefox Nightly, Beta, and Release each ship
with release notes.

.. _Repository:

**Repository** - a collection of stored data from existing databases
merged into one so that it may be shared, analyzed or updated throughout
an organization.

.. _ride alongs:

**Ride Alongs** - Bug fixes that are impacting release users but not
considered severe enough to ship without an identified dot release
driver.

.. _rollout:

**Rollout** - Shipping a release to a percentage of the release population.

.. _status flags:

**Status Flags** - A flag that represents the status of the bug with
respect to a Firefox release.

.. _string freeze:

**String Freeze** - Period during which the introduction, modification, or
deletion of strings exposed to the end-users is not allowed so as to allow our
localizers to translate our product.

.. _taskcluster:

**taskcluster** - Our execution framework to build, run tests on multiple
operating system, hardware and cloud providers.

.. _telemetry:

**Telemetry** - Firefox measures and collects non-personal information,
such as performance, hardware, usage and customizations. This
information is used by Mozilla to improve Firefox.

.. _train model:

**Train model** - a form of software release schedule in which a number
of distinct series of versioned software releases are released as a
number of different "trains" on a regular schedule.

.. _tracking flags:

**Tracking Flags** - A Bugzilla flag that shows whether a bug is being investigated
for possible resolution in a Firefox release. Bugs marked tracking-Firefox XX are
bugs that must be resolved one way or another before a particular release ship.

.. _throttle unthrottle:

**Throttle/Unthrottle a rollout** - Throttle is restricting a release rollout to 0%
of the release population, users can still choose to update but are not updated
automatically. Unthrottle is removing the release rollout restriction.

.. _uplift:

**Uplift** - the action of taking parts from a newer version of a
software system (mozilla-central or mozilla-beta) and porting them to an
older version of the same software (mozilla-beta, mozilla-release or ESR)
