---
myst:
  substitutions:
    image1: |-
      ```{image} iframe_button.png
      :class: center```
    image2: |-
      ```{image} camera_button.png
      :class: center```
    image3: |-
      ```{image} responsive_button.png
      :class: center```
    image4: |-
      ```{image} menu_button.png
      :class: center```
    image5: |-
      ```{image} close_button.png
      :class: center```
---

```{toctree}
:name: devtools-user-doc
:hidden:
:maxdepth: 1

3d_view/index
about_colon_debugging/index
accessibility_inspector/index
application/index
browser_console/index
browser_toolbox/index
custom_formatters/index
debugger/index
debugger-api/index
deprecated_tools/index
devtoolsapi/index
devtoolscolors/index
dom_property_viewer/index
eyedropper/index
index/index
javascript_tracer/index
json_viewer/index
keyboard_shortcuts/index
local_mode/index
measure_a_portion_of_the_page/index
memory/index
migrating_from_firebug/index
network_monitor/index
page_inspector/index
performance/index
responsive_design_mode/index
rulers/index
settings/index
shader_editor/index
storage_inspector/index
style_editor/index
taking_screenshots/index
tips/index
tools_toolbox/index
validators/index
view_source/index
web_audio_editor/index
web_console/index
working_with_iframes/index
```

# Firefox DevTools User Docs

Firefox Developer Tools is a set of web developer tools built into Firefox. You can use them to examine, edit, and debug HTML, CSS, and JavaScript.

This section contains detailed guides to all of the tools as well as information on how to debug Firefox for Android, how to extend DevTools, and how to debug the browser as a whole.

If you have any feedback on DevTools or want to contribute to the project, you can [join the DevTools community](https://firefox-dev.tools/).

:::{note}
If you are just getting started with web development and using developer tools, our [learning](https://developer.mozilla.org/en-US/docs/Learn) docs will help you — see [Getting started with the Web](https://developer.mozilla.org/en-US/docs/Learn/Getting_started_with_the_web) and [What are browser developer tools?](https://developer.mozilla.org/en-US/docs/Learn/Common_questions/What_are_browser_developer_tools) for good starting points.
:::

## The Core Tools

You can open the Firefox Developer Tools from the menu by selecting **Tools > Web Developer > Web Developer Tools** or use the keyboard shortcut {kbd}`Ctrl` + {kbd}`Shift` + {kbd}`I` or {kbd}`F12` on Windows and Linux, or {kbd}`Cmd` + {kbd}`Opt` + {kbd}`I` on macOS.

The ellipsis menu on the right-hand side of Developer Tools contains several commands that let you perform actions or change tool settings.

:::{figure} devtools_layoutmenu.png
:align: center
:::

```{eval-rst}
.. list-table::
   :widths: 20 80

   * - .. image:: iframe_button.png
     - This button only appears when there are multiple iframes on a page. Click it to display a list of the iframes on the current page and select the one with which you want to work.

   * - .. image:: camera_button.png
     - Click this button to take a screenshot of the current page. (*Note:* This feature is not turned on by default and must be enabled in settings before the icon will appear.)

   * - .. image:: responsive_button.png
     - Toggles Responsive Design Mode

   * - .. image:: menu_button.png
     - Opens the menu that includes docking options, the ability to show or hide the split console, and Developer Tools settings. The menu also includes links to the documentation for Firefox Web Tools and the Mozilla Community.

   * - .. image:: close_button.png
     - Closes the Developer Tools
```

### {doc}`Page Inspector </devtools-user/page_inspector/index>`

```{image} landingpage_pageinspector.png
:alt: The all-new Inspector panel in Firefox 57.
:class: border
:target: /devtools-user/page_inspector/index.html
```

View and edit page content and layout. Visualize many aspects of the page including the box model, animations, and grid layouts

### {doc}`Web Console </devtools-user/web_console/index>`

```{image} landingpage_console.png
:alt: The all-new Console panel in Firefox 57.
:class: border
:target: /devtools-user/web_console/index.html
```

See messages logged by a web page and interact with the page using JavaScript.

### [JavaScript Debugger](debugger/index.md)

```{image} landingpage_debugger.png
:alt: The all-new Debugger panel in Firefox 57.
:class: border
:target: debugger
```

Stop, step through, and examine the JavaScript running on a page.

### {doc}`Network Monitor </devtools-user/network_monitor/index>`

```{image} landingpage_network.png
:alt: The Network panel in Firefox 57 DevTools.
:class: border
:target: /devtools-user/network_monitor/index.html
```

See the network requests made when a page is loaded.

### [Performance Panel](https://profiler.firefox.com/docs/)

```{image} landingpage_performance_2022.png
:alt: Performance Panel in Firefox 103 Developer Tools.
:class: border
:target: https://profiler.firefox.com/docs/
```

Analyze your site's general responsiveness, JavaScript, and layout performance.

### {doc}`Responsive Design Mode </devtools-user/responsive_design_mode/index>`

```{image} landingpage_responsivedesign.png
:alt: Responsive Design mode in Firefox 57 Developer Tools.
:class: border
:target: /devtools-user/responsive_design_mode/index.html
```

See how your website or app will look and behave on different devices and network types.

### {doc}`Accessibility inspector </devtools-user/accessibility_inspector/index>`

```{image} landingpage_accessibility.png
:alt: Performance Tools in Firefox 57 Developer Tools.
:class: border
:target: /devtools-user/accessibility_inspector/index.html
```

Provides a means to access the page's accessibility tree, allowing you to check what's missing or otherwise needs attention.

### [Application panel](application/index.md)

```{image} just-application-panel.png
:alt: Performance Tools in Firefox 57 Developer Tools.
:class: border
:target: application
```

Provides tools for inspecting and debugging modern web apps (also known as [Progressive Web Apps](https://developer.mozilla.org/en-US/docs/Web/Progressive_web_apps)). This includes inspection of [service workers](https://developer.mozilla.org/en-US/docs/Web/API/Service_Worker_API) and [web app manifests](https://developer.mozilla.org/en-US/docs/Web/Manifest)

:::{note}
The collective term for the UI inside which the DevTools all live is the {doc}`Toolbox <tools_toolbox/index>`
:::

## More Tools

These developer tools are also built into Firefox. Unlike the "Core Tools" above, you might not use them every day.

```{eval-rst}
.. list-table::
  :widths: 25 75
  :header-rows: 0

  * - :doc:`Memory <memory/index>`
    - Figure out which objects are keeping memory in use.

  * - :doc:`Storage Inspector <storage_inspector/index>`
    - Inspect cookies, local storage, indexedDB, and session storage present in a page.

  * - :doc:`DOM Property Viewer <dom_property_viewer/index>`
    - Inspect the page's DOM properties, functions, etc.

  * - :doc:`Eyedropper <eyedropper/index>`
    - Select a color from the page.

  * - :doc:`Style Editor <style_editor/index>`
    - View and edit CSS styles for the current page.

  * - :doc:`Taking screenshot <taking_screenshots/index>`
    - Take a screenshot of the entire page or of a single element.

  * - :doc:`Measure a portion of the page <measure_a_portion_of_the_page/index>`
    - Measure a specific area of a web page.

  * - :doc:`Rulers <rulers/index>`
    - Overlay horizontal and vertical rulers on a web page

  * - :doc:`Custom formatters <custom_formatters/index>`
    - Customize the way objects are displayed within the DevTools.

  * - :doc:`JavaScript tracer <javascript_tracer/index>`
    - Live display all JavaScript function calls.

  * - :doc:`Local Mode <local_mode/index>`
    - Easily open local files through custom https:// URL.
```

```{image} logo-developer-quantum.png
:class: center
```

```{eval-rst}
.. rst-class:: center

  For the latest developer tools and features, try Firefox Developer Edition.

  `Download Firefox Developer Edition <https://www.mozilla.org/en-US/firefox/developer/>`_


```

## AI Tools

| | |
| --- | --- |
| [Firefox MCP](https://github.com/mozilla/firefox-mcp) | A Model Context Protocol (MCP) server that enables AI assistants to automate and interact with Firefox. |

## Connecting the Developer Tools

If you open the developer tools using {ref}`keyboard shortcuts <keyboard-shortcuts-opening-and-closing-tools>` or the equivalent menu items, they'll target the document hosted by the currently active tab. But you can attach the tools to a variety of other targets, too, both within the current browser and in different browsers or even different devices.

```{eval-rst}
.. list-table::
   :widths: 30 70
   :header-rows: 0

   * - :doc:`about:debugging <about_colon_debugging/index>`
     - Debug add-ons, content tabs, and workers running in the browser.

   * - :ref:`Connecting to Firefox for Android <about-colon-debugging-connecting-to-a-remote-device>`
     - Connect the developer tools to an instance of Firefox running on an Android device.

   * - :doc:`Connecting to iframes <working_with_iframes/index>`
     - Connect the developer tools to a specific iframe in the current page.

```

## Debugging the browser

By default, the developer tools are attached to a web page or web app. But you can also connect them to the browser as a whole. This is useful for browser and add-on development.

```{eval-rst}
.. list-table::
   :widths: 30 70
   :header-rows: 0

   * - :doc:`Browser Console <browser_console/index>`
     - See messages logged by the browser itself and by add-ons, and run JavaScript code in the browser's scope.

   * - :doc:`Browser Toolbox <browser_toolbox/index>`
     - Attach the Developer Tools to the browser itself.


```

## Extending DevTools

For information on extending the Firefox DevTools, see [Extending the developer tools](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Extending_the_developer_tools) over in the [Browser Extensions](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions) section of MDN.

## Migrating from Firebug

Firebug has come to the end of its lifespan (see [Firebug lives on in Firefox DevTools](https://hacks.mozilla.org/2016/12/firebug-lives-on-in-firefox-devtools/) for details of why), and we appreciate that some people will find migrating to another less familiar set of DevTools to be challenging. To ease a transition from Firebug to the Firefox developer tools, we have written a handy guide — {doc}`Migrating from Firebug <./migrating_from_firebug/index>`

## Contribute

If you want to help improve the developer tools, these resources will get you started.

```{eval-rst}
.. list-table::
   :widths: 30 70
   :header-rows: 0

   * - `Get Involved <https://firefox-dev.tools/>`_
     - Our community website explains how to get involved.

   * - `codetribute.mozilla.org <https://codetribute.mozilla.org/projects/devtools/>`_
     - A tool helping to find bugs to work on.

   * - :ref:`Read source docs <devtools-contributor-doc>`
     - Firefox DevTools source code documentation.
```
