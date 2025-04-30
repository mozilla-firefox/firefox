.. _components/sidebar:

=========
Sidebar
=========

The new sidebar builds on existing legacy sidebar code treating ``browser-sidebar.js`` as a `SidebarController`. As part of the new sidebar and vertical tabs project, we've added new components including top-level system module `SidebarManager.sys.mjs` and a per window state manager, `SidebarState.sys.ms`. We've added new UI components that use a combination of moz components and custom lit components. The sidebar team maintains the existing synced tabs and history panels.

Introducing a new panel
-----------------------

Every panel that is registered and enabled in ``browser-sidebar.js``` and the ``toolsNameMap`` map will show as an option in the Customize Sidebar menu (which is a sidebar panel that contains settings).

The launcher is a container for tools (ie, icons that when clicked open or close the associated panel). Registering a panel - which should be behind a pref until it is ready to be introduced - does not automatically add a new icon to the launcher.

A tool can be added once for all users by adding it to the designated pref branch ``sidebar.newTool.migration.`` in ``profile/firefox.js``. So an example would be ``pref("sidebar.newTool.migration.bookmarks", '{}')``.  The pref suffix (``bookmarks`` in this example) is the ``toolID`` that should match what you added as the value portion of the relevant entry in the ``toolsNameMap`` map in ``browser-sidebar.js``. It's important to note that if you have a pref governing the visibility of your sidebar panel, it will need to be enabled at the same time in order to be shown in a user's launcher - either via a nimbus rollout or in-tree.

If you only want to add this item if the pref governing visibility is true, you can pass the pref you want to observe, e.g. ``pref("sidebar.newTool.migration.reviewchecker", '{ "visibilityPref": "browser.shopping.experience2023.integratedSidebar"}')`` where ``browser.shopping.experience2023.integratedSidebar`` is the pref controlling the visibility of the review checker panel.

In both cases, the tool will be introduced to the launcher one time (appended to a user's customized list of tools) and any customization after that (ie, removing it) takes precedence. If it's not removed, it will persist after that session.

If you only want to introduce a tool to new users, you can do so by adding it to the ``DEFAULT_LAUNCHER_TOOLS`` list in ``SidebarManager`` and the ``toolsNameMap``. You can do this even if you have previously introduced a tool via a pref branch migration as there is logic that will prevent a tool from being added twice, however the expectation is that when adding it to ``defaultTools`` the pref governing panel visibility is also enabled in-tree.

State Management
----------------

The sidebar includes a variety of options that users can customize, such as:

- Sidebar visibility (e.g., expand on hover, hide tabs and sidebar)
- Sidebar position (left or right)
- Tab strip orientation (vertical or horizontal)
- Available tools (which can be enabled or disabled)

These options are stored internally as preference values, meaning they persist across
browser windows. For example, if you have two windows open and move the sidebar to the
right in one of them, the other window's sidebar will also move to the right.

In addition to these preferences, there are also *state* values that depend on user
interaction, such as:

- Whether the launcher is expanded, collapsed, or hidden
- Which panel is currently open
- The width of the launcher and panel

These state values are usually stored in ``SessionStore``. However, if the user has
disabled session restore (e.g., via permanent private browsing mode), they are serialized
and stored under the ``sidebar.backupState`` preference instead.

State values are per-window and do not carry over between windows. For example, if you
load a panel in one window, other windows will not display that same panel.

SidebarState: Per-Window State
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``SidebarState`` tracks and updates sidebar-related UI state within a specific browser
window. It acts as the single source of truth for the sidebar's current state in that
window. In practice, it functions as a "reactive controller," handling user interactions,
maintaining internal state values, and determining the appropriate DOM updates.

When state values are changed, ``SidebarState`` immediately applies corresponding
adjustments to the UI. For example:

- When ``launcherVisible`` is set to ``false``, the launcher is hidden, and the sidebar's
  inline padding is adjusted accordingly.
- When ``launcherWidth`` or ``panelWidth`` are updated, inline CSS is modified to ensure
  that the sidebar does not occupy more than 75% of the browser's width.

SidebarManager: Global State
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``SidebarManager`` handles listening to preference values, global events, and loading the
"backup state" if necessary. When such handlers are triggered, ``SidebarManager`` delegates
tasks to each instance of ``SidebarController`` (a per-window module).

Use cases for ``SidebarManager`` include:

- Updating visibility preferences when the tab orientation changes.
- Managing the default set of tools for new sidebar users.
- Detecting when the sidebar button is removed from ``CustomizableUI``, thereby signaling
  all ``SidebarController`` instances to close the sidebar if it is open.

.. note::
  ``SidebarManager`` should also be responsible for updating the customize panel when
  preferences are changed from another source, but that is currently not the case. This
  should be addressed in `Bug 1945530 <https://bugzil.la/1945530>`_.

Example Workflows
~~~~~~~~~~~~~~~~~

Per-Window State Change
^^^^^^^^^^^^^^^^^^^^^^^

Suppose a user clicks the toolbar button to show the sidebar. This is how the interaction
is handled:

1. ``SidebarController.handleToolbarButtonClick()`` is called, which sets ``state.launcherVisible``.
2. ``SidebarState`` calls the setter for ``launcherVisible``, removing the ``hidden``
   attribute from the launcher element.

Global State Change
^^^^^^^^^^^^^^^^^^^

Suppose a user removes the toolbar button. Since ``CustomizableUI`` changes are synced
across windows, this is treated as a global state change. The interaction is handled as
follows:

1. ``CustomizableUI`` notifies listeners (including ``SidebarManager``) about a widget
   removal.
2. ``SidebarManager.onWidgetRemoved()`` is called to handle the event.
3. ``onWidgetRemoved`` calls the ``hide()`` function on every ``SidebarController`` instance.
