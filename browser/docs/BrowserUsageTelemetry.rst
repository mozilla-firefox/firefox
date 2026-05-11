.. _browserusagetelemetry:

=======================
Browser Usage Telemetry
=======================

The :searchfox:`BrowserUsageTelemetry.sys.mjs <browser/modules/BrowserUsageTelemetry.sys.mjs>` module is the main module for measurements regarding the browser usage (e.g. tab and window counts, search counts, ...).

The measurements recording begins right after the ``SessionStore`` has finished restoring the session (i.e. restoring tabs/windows after Firefox starts).

Tab and window interactions
===========================
The usage telemetry module currently measures these interactions with the browser's tabs and windows:

- *tab and window engagement*: counts the number of non-private tabs and windows opened in a subsession, after the session is restored (see e.g. ``browser.engagement.max_concurrent_tab_count``);
- *URI loads*: counts the number of page loads (doesn't track and send the addresses, just the counts) directly triggered by the users (see ``browser.engagement.total_uri_count``);
- *navigation events*: at this time, this only counts the number of time a page load is triggered by a particular UI interaction (e.g. by searching through the URL bar, see ``browser.engagement.navigation.urlbar``).


Please see :searchfox:`Scalars.yaml <toolkit/components/telemetry/Scalars.yaml>` for the full list of tracked interactions.

Customizable UI
===============

This telemetry records information about the positions of toolbar items and when
the user interacts with them. It is submitted as scalar values along with the
normal telemetry ping. There are a number of different parts to this telemetry:

UI Areas
--------

For the purposes of this telemetry a set of areas are defined:

* In the main browser UI:

  * ``menu-bar`` - The main menu.
  * ``menu-toolbar`` - The normally hidden toolbar that holds the main menu.
  * ``titlebar`` - The optional title bar.
  * ``tabs-bar`` - The area where tabs are displayed.
  * ``vertical-tabs-container`` - The area where vertical tabs are displayed.
  * ``bookmarks-bar`` - The bookmarks toolbar.
  * ``app-menu`` - The main application (hamburger) menu.
  * ``tabs-context`` - The context menu shown from right-clicking a tab.
  * ``content-context`` - The context menu shown from right-clicking the web page.
  * ``widget-overflow-list`` - Items that have overflowed the available space.
  * ``pinned-overflow-menu`` - Items that the user has pinned to the toolbar overflow menu.
  * ``pageaction-urlbar`` - Page actions buttons in the address bar.
  * ``pageaction-panel`` - The page action (meatball) menu.
  * ``nav-bar-start`` - The area of the navigation toolbar before the address bar.
  * ``nav-bar-end`` - The area of the navigation toolbar after the address bar.
  * ``unified-extensions-area`` - The unified extensions panel.

* In ``about:preferences`` the different categories are used:

  * ``preferences-paneDownloads``
  * ``preferences-paneGeneral``
  * ``preferences-paneHome``
  * ``preferences-panePrivacy``
  * ``preferences-paneSearch``
  * ``preferences-paneSearchResults``
  * ``preferences-paneSync``
  * ``preferences-paneContainers``
  * ``preferences-paneAccessibility``
  * ``preferences-paneAppearance``
  * ``preferences-paneLanguages``
  * ``preferences-paneAbout``
  * ``preferences-panePermissionsData``
  * ``preferences-paneTabsBrowsing``

Widget Identifiers
------------------

In order to uniquely identify a visual element a set of heuristics are used:

#. If the element is one of the customizable toolbar items then that item's ID
   is used.
#. If the DOM element has an ID set then that is used.
#. If the DOM element's class contains one of ``bookmark-item``,
   ``tab-icon-sound`` or ``tab-close-button`` then that is used.
#. If the DOM element has a ``preference`` ``key``, ``command``, ``observes`` or
   ``data-l10n-id`` attribute then that is used.
#. If there is still no identifier then this is repeated for the DOM element's
   parent element.

Widget Locations
----------------

The keyed scalar ``browser.ui.toolbar_widgets`` records the position of widgets in
the UI. At startup the positions of widgets are collected and recorded by
setting the scalar key ``<widget id>_pinned_<area>`` to true. The widget ID are
the IDs of the elements in the DOM. The area is one of the areas listed above
from the browser UI that can be customised.

For the areas that can be controlled the scalar keys ``<area>_<off/on/newtab>`` are set.
``newtab`` is special to the Bookmarks Toolbar and is used when the toolbar will only
be shown on the New Tab page.

Widget Customization
--------------------

The scalar ``browser.ui.customized_widgets`` records whenever the user moves a
widget around the toolbars or shows or hides some of the areas. When a change
occurs the scalar with the key ``<widget id>_<action>_<old area>_<new area>_<reason>``
is incremented. The action can be one of ``move``, ``add`` or ``remove``. Old
area and new area are the previous and now locations of the widget. In the case
of ``add`` or ``remove`` actions one of the areas will be ``na``. For areas that
can be shown or hidden the areas will be ``off`` or ``on``. The reason is a simple
string that indicates what caused the move to happen (drag, context menu, etc.).

UI Interactions
---------------

The scalars ``browser.ui.interaction.<area>`` record how often the use
interacts with the browser. The area is one of those above with the addition of
``keyboard`` for keyboard shortcuts.

When an interaction occurs the widget's identifier is used as the key and the
scalar is incremented. If the widget is provided by an add-on then the add-on
identifier is dropped and an identifier of the form ``addonX`` is used where X
is a number. The number used is stable for a single session. Every time the user
moves or interacts with an add-on the same number is used but then the numbers
for each add-on may change after Firefox has been restarted.

Profile Count
=============

The scalar ``browser.engagement.profile_count`` records how many profiles have
been used by the current Firefox installation. It reports a bucketed result,
which will be 0 if there is an error. The raw value will be reported for 1-10,
but above that, it will report 10 for 10-99, 100 for 100-999, 1000 for
1000-9999, and 10000 for any values greater than that.

The profile count data for an installation is stored in the root of the
update directory in a file called ``profile_count_<install hash>.json``. The
full path to the file will typically look something like
``C:\ProgramData\Mozilla\profile_count_5A9E6E2F272F7AA0.json``.

This value is meant to be resilient to re-installation, so that file will not
be removed when Firefox is uninstalled.

Context menu entrypoints
========================

Some context menus are re-used in multiple places. By default, we simply count
the number of interactions per item within a context menu, and do not record
the entrypoint that caused the context menu to open.

It is possible to opt-in to recording the entrypoint that caused the context
menu to open. This is done by adding an entry to
``ENTRYPOINT_TRACKED_CONTEXT_MENU_IDS``, mapping the ID of the context menu
to a keyed Scalar under ``browser.ui.interaction.``. This scalar is recorded
only if an interaction is recorded within the context menu itself.

When the keyed scalar is recorded, the key will be a unique ID for the
trigger node that caused the context menu to open. The value is the count
of openings from that trigger node.
