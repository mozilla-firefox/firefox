# gBrowser

{searchfox}`Tabbrowser.sys.mjs <browser/components/tabbrowser/Tabbrowser.sys.mjs>` defines the `Tabbrowser` class, shared across windows: as a system module it loads once per process, so there is one `Tabbrowser` class for the whole browser. The `Tabbrowser` manages tabs and provides an interface to operate on tabs. It also manages the underlying infrastructure for switching tabs, adding tabs, removing tabs, knowing about tab switches, etc. Each window creates its own `Tabbrowser` instance during startup and exposes that instance in the window scope as `gBrowser`.

## What does the name gBrowser stand for?

What does the *g* in `gBrowser` stand for? It's an old Mozilla convention and *g* stands for *global*. It's a way of indicating inside of the variable name that something is globally scoped. In this case it is global to the browser window and every single tab of the window will be managed through that window's `gBrowser`.

What does the *browser* in `gBrowser` stand for? *browser* is an element that knows how to render web content. At some point in its lineage, Firefox didn't have tabs. There was one browser per window, and that individual browser was called `gBrowser`. Today's `gBrowser` forwards a number of calls to the current `selectedBrowser`, which is an actual `<xul:browser>` element.

## Relationship between browser and gBrowser

`<xul:browser>` is a XUL element that can load web pages, make HTTP requests and respond accordingly. It is conceptually similar to an `iframe` except that it contains additional methods and has elevated privileges. In Firefox, each tab is associated with one `<xul:browser>`, and `gBrowser` owns all of the browsers.
