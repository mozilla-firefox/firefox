=========================================
Firefox DevTools MCP for AI Agent Testing
=========================================

Overview
========

The ``firefox-devtools-mcp`` is a Model Context Protocol (MCP) server that
enables AI assistants (such as Claude Code) to automate and interact with
Firefox through WebDriver protocols (BiDi and Classic). This tool is
particularly useful for:

- Automating browser interactions for testing and debugging
- Attempting to reproduce issues on live websites
- Capturing Firefox debug logs (MOZ_LOG) while controlling the browser
- Dynamically changing Firefox configuration without restarting

The MCP server provides programmatic access to a Firefox build, including
DevTools capabilities such as page navigation, DOM inspection, network
monitoring, console logs, user input simulation, and Firefox logging. It isn't
complete yet, but can already be useful.

This documentation refers to the official Mozilla MCP for
Firefox development: https://github.com/mozilla/firefox-devtools-mcp

Key Features
------------

- **WebDriver BiDi Protocol**: Uses the modern WebDriver BiDi protocol via
  Selenium WebDriver
- **WebDriver Classic Fallback**: Uses WebDriver Classic (Marionette) for
  features not yet ported to BiDi (notably Chrome context usage)
- **Local Firefox Support**: Can connect to custom Firefox builds (e.g., local
  development builds), or typical release/beta/nightly/esr builds, present on
  the machine
- **Real-time Interaction**: Provides live access to browser state and network
  activity
- **Firefox Output Capture**: stdout/stderr capturing, including setting and
  reading MOZ_LOG

Prerequisites
=============

Required Software
-----------------

- **Node.js**: Version 20.19.0 or higher
- **Firefox**: system installation (all channels supported) or local build
- **Claude Code** (or compatible MCP client): For AI-assisted automation

Installation and Configuration
===============================

The Firefox DevTools MCP server is **automatically configured** when using
Claude Code in the Firefox repository, via the ``.mcp.json`` file at the root
of the repository.

For installation in other projects or at user scope, Claude Code can configure
the MCP server with a single command:

.. code-block:: bash

   claude mcp add firefox-devtools npx @mozilla/firefox-devtools-mcp-moz

This command registers the MCP server with your Claude Code configuration for
the current project (typically ``project/.claude/``).

If a global installation is preferred (so that it works from multiple project
directories), it can be installed at the user scope:

.. code-block:: bash

   claude mcp add firefox-devtools npx @mozilla/firefox-devtools-mcp-moz --scope user

This changes ``~/.claude.json``, either in the project section, or in the global
section (if installed at the user scope).

After installation and/or configuration restart Claude Code to load the new MCP
server:

.. code-block:: bash

   # Exit Claude Code, then restart it, continuing the last session
   claude -c

The MCP server will automatically start when Claude Code initializes.

Configuration options
---------------------

The MCP server supports several command-line options. It's preferable to start
with no options, but they exist:

.. list-table::
   :widths: 25 75
   :header-rows: 1

   * - Option
     - Description
   * - ``--firefox-path <path>``
     - Path to Firefox executable (system Firefox used if not specified)
   * - ``--headless``
     - Run Firefox without UI (useful for automated testing)
   * - ``--viewport <size>``
     - Set initial viewport size (e.g., ``1280x720``)
   * - ``--profile-path <path>``
     - Use a specific Firefox profile directory
   * - ``--start-url <url>``
     - Initial URL to load (default: ``about:home``)
   * - ``--accept-insecure-certs``
     - Ignore TLS certificate errors (use with caution)
   * - ``--firefox-arg <arg>``
     - Additional arguments to pass to Firefox
   * - ``--env <KEY=VALUE>``
     - Set environment variables for Firefox (can be used multiple times)
   * - ``--output-file <path>``
     - Path where Firefox output (stdout/stderr) will be captured (optional)

Example with multiple options:

.. code-block:: json

   {
      "type": "stdio",
      "command": "@mozilla/firefox-devtools-mcp-moz",
      "args": [
       "--firefox-path",
       "/home/developer/firefox/obj-x86_64-pc-linux-gnu/dist/bin/firefox",
       "--headless",
       "--viewport",
       "1920x1080",
       "--env",
       "MOZ_LOG=HTMLMediaElement:5"
     ]
   }

Available Capabilities
======================

The ``firefox-devtools-mcp`` provides tools through the MCP protocol. Tool names
are generally self-describing. Simply ask your AI assistant to perform tasks - it
will use the appropriate tools. This list may not be complete as the server is
under active development.

Importantly, you can instruct the agent to run a different Firefox than the one
it picked, e.g. your own build, or some specific version, by telling it the
path: it isn't necessary to configure it.

**Page Management:**
``list_pages``, ``new_page``, ``navigate_page``, ``select_page``, ``close_page``

**DOM Interaction:**
``take_snapshot``, ``resolve_uid_to_selector``, ``click_by_uid``, ``hover_by_uid``,
``fill_by_uid``, ``fill_form_by_uid``, ``drag_by_uid_to_uid``, ``upload_file_by_uid``

**Network & Console:**
``list_network_requests``, ``get_network_request``, ``list_console_messages``,
``clear_console``

**Screenshots:**
``screenshot_page``, ``screenshot_by_uid``

**Dialogs & Navigation:**
``accept_dialog``, ``dismiss_dialog``, ``navigate_history``, ``set_viewport_size``

**Firefox Management:**
``get_firefox_info``, ``get_firefox_output``, ``restart_firefox``

**Script Evaluation:**
``evaluate_script`` (content), ``evaluate_chrome_script`` (privileged)

**Chrome Context (privileged code):**
``list_chrome_contexts``, ``select_chrome_context``, ``evaluate_chrome_script``

Working with MOZ_LOG
====================

Firefox's MOZ_LOG system provides detailed debug output from various modules.
The ``firefox-devtools-mcp`` captures this output automatically when environment
variables are set.

You can enable MOZ_LOG with something like this:

.. code-block:: text

   Developer: "Enable HTMLMediaElement logging at level 5"

   AI uses: restart_firefox(env=["MOZ_LOG=HTMLMediaElement:5"])

Common MOZ_LOG Modules
-----------------------

For a useful list of available MOZ_LOG modules and their descriptions, see
``toolkit/content/aboutLogging/aboutLogging.mjs`` in the Firefox repository, or search
searchfox.org with something like this `searchfox query <https://searchfox.org/firefox-main/search?q=LazyLogModule.*%5C%28%22&path=&case=false&regexp=true>`__.

Chrome Context Access
=====================

For debugging privileged Firefox code (chrome/XUL), enable system access:

.. code-block:: text

   restart_firefox(env=["MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1"])

Then use ``list_chrome_contexts``, ``select_chrome_context``, and
``evaluate_chrome_script`` to access ``gBrowser``, ``Components``, and other
privileged APIs.

At the moment, the MCP relies on WebDriver Classic (Marionette) for Chrome
context APIs. While WebDriver BiDi and Classic are meant to work seamlessly
together, some WebDriver BiDi features might not work when getting executed for
chrome browsing contexts. Chrome support for WebDriver BiDi is tracked on the
following `meta bug <https://bugzilla.mozilla.org/show_bug.cgi?id=1722679>`__.

Troubleshooting
===============

Try to update to the latest version:

.. code-block:: bash

   ./mach npx @mozilla/firefox-devtools-mcp-moz@latest

then quit when seeing "Ready to accept tool requests".

MCP Server Not Working
-----------------------

1. Verify the ``.mcp.json`` at the root of the repository is present.
2. Verify Node.js is available via ``./mach npx --version``.
3. Ask in #ai4dev or #developers on chat.mozilla.org
