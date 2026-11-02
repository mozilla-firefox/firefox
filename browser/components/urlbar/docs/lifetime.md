# Search Lifecycle

When a character is typed into an address or search bar, or when it's focused,
we initiate a search. What follows is a simplified version of the lifetime of a
search, describing the pipeline that returns results for a typed string. Some
parts of the query lifetime are intentionally omitted from this document for
clarity.

The search described in this document is internal to urlbar code. It is not
the search sent to the default search engine when you press Enter. Parts of this
process often occur multiple times per keystroke, as described below.

It is recommended that you first read the {doc}`nontechnical-overview` to become
familiar with the terminology in this document. This document is current as
of August 2026.

01. The user types a query (e.g. "coffee near me") into the *UrlbarInput*
    `<input>` {searchfox}`DOM element <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/content/UrlbarInput.mjs#145>`.
    That DOM element {searchfox}`tells <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/content/UrlbarInput.mjs#5474>`
    *UrlbarInput* that text is being input.

02. *UrlbarInput* {searchfox}`starts a search <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/content/UrlbarInput.mjs#2432>`.
    It {searchfox}`creates <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/content/UrlbarInput.mjs#5778>`
    a [UrlbarQueryContext](overview.md#the-urlbarquerycontext)
    and passes it to the {searchfox}`UrlbarChildController <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/content/UrlbarChildController.mjs#332>`,
    which forwards it to the *UrlbarParentController*. The controller is split in
    two: the *UrlbarChildController* lives alongside the input, while the
    *UrlbarParentController* owns the *ProvidersManager* and runs the query. This
    split is what lets the input run in a content process (such as about:newtab)
    while the providers stay in the parent process.
    The query context is an object that will exist for the lifetime of the query
    and it's how we keep track of what results to show. It contains information
    like what kind of results are allowed, the search string ("coffee near me",
    in this case), and other information about the state of the Urlbar. A new
    *UrlbarQueryContext* is created every time the text in the input changes.

    On the [message path](overview.md#direct-path-and-message-path), this is
    where the query crosses the {doc}`process boundary <process-boundary>`: the
    *UrlbarChildController* sends the query context to the parent as a message,
    and the *UrlbarParentController* runs the query on its own copy of it. The
    results reach the view only through the notifications in step 10, and the
    query context the input created never receives them.

03. *UrlbarParentController* {searchfox}`tells ProvidersManager <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/UrlbarParentController.sys.mjs#415>`
    that the providers should fetch results.

04. *ProvidersManager* tells {searchfox}`each <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/UrlbarProvidersManager.sys.mjs#760>`
    provider to decide if it wants to provide results for this query by calling
    their {searchfox}`isActive <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/UrlbarProvidersManager.sys.mjs#772>`
    methods. The provider can decide whether or not it will be active for this
    query. Some providers are rarely active: for example,
    *UrlbarProviderTopSites* {searchfox}`isn't active if the user has typed a search string <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/UrlbarProviderTopSites.sys.mjs#81>`.

05. *ProvidersManager* then tells the *active* providers to fetch results by
    {searchfox}`calling their startQuery method <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/UrlbarProvidersManager.sys.mjs#815>`.

06. Each provider fetches its results in its own way and asynchronously. As one
    example, if the default search engine is Google,
    *UrlbarProviderSearchSuggestions* would send the string "coffee near me" to
    Google. Google would return a list of suggestions and
    *UrlbarProviderSearchSuggestions* would create a *UrlbarResult* for each one.

    The providers send their results back to *ProvidersManager*. They do this
    one result at a time by {searchfox}`calling the addCallback callback <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/UrlbarProviderSearchSuggestions.sys.mjs#312>`
    passed into startQuery. *ProvidersManager* takes all the results from all the
    providers and {searchfox}`puts them into the list of unsorted results <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/UrlbarProvidersManager.sys.mjs#996>`.

    Due to the asynchronous and parallel nature of providers, this and the
    following steps may occur multiple times per search. Some providers may take
    longer than others to return their results. We don't want to wait for slow
    providers before showing results. To handle slow providers,
    *ProvidersManager* gathers results from providers in "chunks". A timer
    fires at an interval. Every time the timer fires, we take whatever results we
    have from the active providers (the "chunk" of results) and perform the
    following steps.

07. *ProvidersManager* {searchfox}`asks <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/UrlbarProvidersManager.sys.mjs#1026>`
    *UrlbarMuxer* to sort the unsorted results.

08. *UrlbarMuxer* chooses the results that will be shown to the user. It groups
    and sorts the results to determine the order in which the results will be
    shown. This process usually involves discarding irrelevant and duplicate
    results. We also cap results at a limit, defined in the
    `browser.urlbar.maxRichResults` preference.

09. Once the results are sorted, *ProvidersManager*
    {searchfox}`tells UrlbarParentController <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/UrlbarProvidersManager.sys.mjs#1042>`
    that results are ready to be shown.

10. *UrlbarParentController* {searchfox}`sends out a notification <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/UrlbarParentController.sys.mjs#773>`
    that results are ready to be shown. The notification is dispatched by the
    *UrlbarChildController*, which *UrlbarView* was {searchfox}`listening <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/content/UrlbarView.mjs#73>`
    to. Once the view gets the notification, it {searchfox}`calls #updateResults <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/content/UrlbarView.mjs#998>`
    to create {searchfox}`DOM nodes <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/content/UrlbarView.mjs#1533>`
    for each *UrlbarResult* and {searchfox}`inserts them <firefox-main/rev/8e42adb00f0d301d1b74f71d5f7d49228eb712c9:browser/components/urlbar/content/UrlbarView.mjs#1527>`
    into the view's DOM element.

    On the message path, this is where the results cross back. Each query
    notification is a message with the query context in its wire form, and the
    *UrlbarChildController* builds a new *UrlbarQueryContext* from it before
    any listener reads it. The view therefore receives the notification
    asynchronously, and gets a different query context object with every
    update.

    As described above, we may reach this step multiple times per search. That
    means we may be updating the view multiple times per keystroke. A view that
    visibly changes many times after a single keystroke is perceived as
    "flickering" by the user. As a result, we try to limit the number of times
    the view needs to update.

## Diagram

The blue rounded boxes are the UI modules, which run wherever the input
lives; the amber rectangles always run in the parent process. The solid
lines show a query traveling to the providers. The dotted lines show its
results coming back to the view. The two thick red lines between
*UrlbarChildController* and *UrlbarParentController* cross the process boundary
on the message path.

```{mermaid}
:align: center
:caption: One trip through the search pipeline, from a keystroke to result rows.

---
config:
  flowchart:
    wrappingWidth: 400
---
%% wrappingWidth works around https://github.com/mermaid-js/mermaid/issues/5785,
%% which makes Firefox drop labels containing long words.
flowchart TD
    dom([DOM])
    input(UrlbarInput)
    child(UrlbarChildController)
    view(UrlbarView)
    parent[UrlbarParentController]
    manager[UrlbarProvidersManager]
    providers[UrlbarProviders]
    muxer[UrlbarMuxer]

    dom -- "1: text input" --> input
    input -- "2: UrlbarQueryContext" --> child
    child -- "2: UrlbarQueryContext" --> parent
    parent -- "3: fetch results" --> manager
    manager -- "4: isActive<br/>5: startQuery" --> providers
    providers -. "6: results" .-> manager
    manager -. "7: sort" .-> muxer
    muxer -. "8: sorted results" .-> manager
    manager -. "9: results ready" .-> parent
    parent -. "10: notification" .-> child
    child -. "10: notification" .-> view
    view -. "result rows" .-> dom

    classDef uiModule fill:#dbeafe,stroke:#1e40af,color:#1a1a1a;
    classDef parentModule fill:#fef3c7,stroke:#92400e,color:#1a1a1a;
    classDef domNode fill:#e5e7eb,stroke:#4b5563,color:#1a1a1a;
    class input,child,view uiModule;
    class parent,manager,providers,muxer parentModule;
    class dom domNode;
    linkStyle 2,9 stroke:#b91c1c,stroke-width:3px;
```
