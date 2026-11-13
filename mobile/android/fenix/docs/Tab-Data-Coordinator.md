# Tab Data Coordinator

A component that gathers tab data from the various sources and exposes it from a *single* location, so that consumers do not have to know which source a given piece of data came from.

This component is intended to hold that merging logic: the coordination between tab sources, the combined models describing tabs and tab groups, and the observable surface consumers subscribe to. Code that reconciles two or more tab sources belongs here rather than in the individual source components or in the app.

Note that this component coordinates existing tab data; it is not a storage layer. Persisting and owning tab state remains the responsibility of the components that produce it.

Synced tabs are external to this component. They could eventually be integrated into the coordinator though, if the need arises to model a unified sync model from tabs and tab groups.

Anything requiring more complex tab data modeling should still read directly from `BrowserState`. See [Writing tab data](#writing-tab-data) for the rules on the write side.

## Glossary

| Term | Meaning |
| :--- | :--- |
| Normal tab | A non-private tab. Only normal tabs can belong to a tab group. |
| Inactive tab | A normal tab that has not been accessed within the inactivity timeout. Only reported when the inactive tabs feature is enabled. |
| Browsable tab | A tab that can be viewed in the browser - neither inactive, nor in a closed tab group. |
| Closed tab group | A group the user has hidden. Its tabs stay **open**, but are excluded from browsable tabs. |
| Open tab group | A tab group that is not closed. |
| Ungroup |  Delete the tab group and restore the tabs to an ungrouped state. Tabs remain open. |
| Delete group and tabs | Delete the tab group *and* close all of its tabs. |

## Data flow

`TabDataCoordinator` is the entry point for accessing simple, transformed models of tab data. It combines open tabs with their associated tab groups and exposes the merged snapshot as a `StateFlow<TabsSnapshot?>`. As the underlying data sources update, the coordinator emits a new snapshot. When the application asks the coordinator for a data update, the coordinator passes the update along to the appropriate source(s).

```{mermaid}
flowchart LR
    BS["BrowserStore"] --> TR["DefaultTabRepository (Fenix)"]
    TR -* "tabFlow" --> C["DefaultTabDataCoordinator"]
    TGR["TabGroupRepository"] -* "tabGroupDataFlow" --> C
    C -* "StateFlow of TabsSnapshot" --> UI["Consumers"]
    UI -* "suspend write APIs" --> C
    C -* "moveTabs / removeTabs" --> TR
    TR -* "TabsUseCases" --> BS
    C -* "group writes" --> TGR
```

## Emitted data

The KDoc on [TabsSnapshot](https://searchfox.org/firefox-main/source/mobile/android/android-components/components/feature/tabdata-coordinator/src/main/java/mozilla/components/feature/tabdata/coordinator/data/TabsSnapshot.kt) is the source of truth for the available fields. The diagram below is illustrative and may not list every field.

```{mermaid}
classDiagram
    class TabsSnapshot {
        selectedTab: Tab?
        normalTabs: List~Tab~
        browsableNormalTabs: List~Tab~
        privateTabs: List~Tab~
        inactiveTabs: List~Tab~
        tabGroups: List~TabGroup~
        openTabGroups: List~TabGroup~
        inactiveTabsEnabled: Boolean
        tabGroupsEnabled: Boolean
        ...
    }
    class Tab {
        id: String
        url: String
        title: String
        private: Boolean
        inactive: Boolean
        tabGroupId: String?
        ...
    }
    class TabGroup {
        id: String
        title: String
        theme: String
        closed: Boolean
        tabIds: List~String~
        ...
    }
    TabsSnapshot --> Tab
    TabsSnapshot --> TabGroup
```

### Tab feature settings

`TabsSnapshot` also reports whether tab-specific features, such as inactive tabs and tab groups, are enabled. These come from user settings, not Nimbus.

:::{warning}
The settings are currently only read when a snapshot is computed. Toggling one does not produce a new snapshot on its own; the new value appears once the tab or tab group source next emits. This will be remedied in [Bug 2069529](https://bugzilla.mozilla.org/show_bug.cgi?id=2069529).
:::

### Semantics and edge cases

* Private tabs are never grouped.
* When the tab groups feature is disabled, `tabGroups` and `openTabGroups` are empty and no tab has a `tabGroupId`, even if assignments are stored.
* When the inactive tabs feature is disabled, `inactiveTabs` is empty and inactivity is ignored when computing `browsableNormalTabs`.
* `TabGroup.theme` is carried through as the raw stored string. Mapping it onto a UI theme is the consumer's responsibility.

### Query helpers

[TabsSnapshotQueries.kt](https://searchfox.org/firefox-main/source/mobile/android/android-components/components/feature/tabdata-coordinator/src/main/java/mozilla/components/feature/tabdata/coordinator/data/TabsSnapshotQueries.kt) provides extension functions for common lookups, such as `findTab`, `findTabGroup`, `tabsInTabGroup` and `tabIdsInTabGroup`. Prefer these over filtering the snapshot by hand. Group membership helpers work from the open tabs, so they never return tabs that have already been closed.

## Writing tab data

The coordinator exposes `suspend` write APIs for operations that touch more than one source.

| Category | APIs |
| :--- | :--- |
| Create / update | `createTabGroup`, `updateTabGroup` |
| Membership | `addTabsToTabGroup`, `mergeTabGroupIntoTab`, `mergeTabGroups`, `removeTabFromTabGroup` |
| Group lifecycle | `openTabGroup`, `closeTabGroup`, `ungroupTabGroup`, `ungroupTabGroups`, `deleteTabGroupAndTabs` |
| Ordering | `reorderItem` |

See the KDoc on [TabDataCoordinator](https://searchfox.org/firefox-main/source/mobile/android/android-components/components/feature/tabdata-coordinator/src/main/java/mozilla/components/feature/tabdata/coordinator/TabDataCoordinator.kt) for each operation's exact behavior.

### Rules the coordinator enforces

* **Tab order belongs to the tab source.** Tab groups have no position of their own; a group is displayed at its first tab's position and the other tabs in the group are sequentially ordered after it.
* **A group's tabs are kept next to each other.** Every membership and ordering operation moves tabs in the tab source so the group's tabs stay together, provided the group ID is passed in. `reorderItem` moves a group as a unit.
* **A group is always opened with at least one tab.** When spawning a tab group without any initial tabs from the user, a new tab is always created in tandem, which is automatically added to the new group.
* **A group is never left without tabs.** `removeTabFromTabGroup` won't close a group's last tab. When deleting the last tab in a tab group, invoke `deleteTabGroupAndTabs` instead.

:::{important}
Do not bypass the coordinator when the operation involves tab groups. Moving or deleting grouped tabs through `TabsUseCases`, or writing to `TabGroupRepository` directly, can break the rules above. Operations that only touch a single source and cannot affect grouping, such as selecting a tab, can still use the existing use cases.
:::

### Consistency and timing

* **`tabsSnapshot` emits `null` until it is first populated.** Treat `null` as "not ready", not "no tabs"; use `filterNotNull()` when collecting.
* **Writes are not atomic across sources.** An operation such as `createTabGroup` moves tabs in the tab source, then writes to tab group storage, as two separate steps.
  * The atomicity would be enforceable if both sources were stored in a database that supports atomic commits. This is not possible today due to tab data being stored in a JSON list.
* **Writes are not reflected right away.** When a write API returns, the snapshot may not contain the change yet. Observe `tabsSnapshot` rather than reading `tabsSnapshot.value` right after a write.
* **The sources update independently.** The tab source can update before the tab group source, so `TabGroup.tabIds` is filtered against the open tabs on every snapshot.

## Lifecycle

`TabDataCoordinator` lives for the lifetime of the Firefox process. It is created lazily as `Components.tabDataCoordinator`, and `initialize()` is called once at the end of `FenixApplication`'s startup. After that, the coordinator continuously observes its data sources.

:::{caution}
Consumers must not call `initialize()` more than once. It is not idempotent: each call starts another collector.
:::

## Data dependency boundaries

The component deliberately does not depend on `browser-state` or `feature-tabs`. Instead, the embedding app supplies an implementation of `TabRepository` that:

* maps its own tab representation onto the coordinator's `Tab` model, leaving `Tab.tabGroupId` null (group membership is assigned by the coordinator), and
* applies the tab moves and removals the coordinator requests.

Fenix's implementation is `DefaultTabRepository`, which is backed by `BrowserStore` and `TabsUseCases`.

Tab groups are read from, and written to, `TabGroupRepository` in `feature-tabgroups-storage` directly.

## Usage guidelines

### When to use the coordinator

`TabDataCoordinator` should be used for any simple tab data look-ups in Fenix, and for any update involving tab groups. Examples:

* Checking whether the selected tab is normal or private.
* Checking whether the selected tab is in a tab group.
* Fetching a simple model for inactive tabs.
* Fetching a simple model for private tabs.
* Fetching the list of tabs that are neither inactive nor within a closed tab group.
* Querying the subset of tab groups that are marked as `open`.
* Checking whether tab-related features are enabled.
* Creating, editing, reordering or deleting tab groups, or changing which tabs belong to them.

### When not to use the coordinator

`TabDataCoordinator` should not be used for features that require deeper data modeling or specific, one-off data fields. Examples:

* Modeling reader mode
* Querying a tab's `EngineState` data
* Querying a tab's history or download state

### How to consume it

* Access it through `components.tabDataCoordinator` (or `requireComponents.tabDataCoordinator`), but pass the `TabDataCoordinator` interface into middleware, ViewModels and other classes so they can be tested. `TabStorageMiddleware` is a good reference.
* Collect `tabsSnapshot` in a lifecycle-aware way, e.g. `repeatOnLifecycle` or `collectAsStateWithLifecycle`.
* Launch write APIs in a scope that outlives the UI interaction if the write must complete even when the screen is dismissed.

## Contribution guidelines

### When to expand the coordinator surface

In the future, it might make sense to add data fields to `TabsSnapshot` and/or add tab data write operations to the coordinator contract.

New data fields should be intended for use by two or more features. The fields themselves should ideally be primitives or simple immutable types. Lambdas and complex Android-framework types should not be added to the data model. (`Tab.icon` is an existing exception; note that `Bitmap` is compared by reference, which affects whether a new snapshot is emitted.)

:::{warning}
Avoid fields that change often, such as page load progress. Every `BrowserState` change maps all tabs again, and a field that changes often will make the snapshot emit constantly to every consumer.
:::

For data APIs, feel empowered to add any reasonable tab data operation that requires the coordination of two or more data sources. For example, any new tab group functionality should be routed through the coordinator, since API calls will likely touch both tab and tab group data simultaneously.

### Checklists

**Adding a field to `Tab` or `TabGroup`**

1. Add the property and its KDoc to `Tab` / `TabGroup`.
2. Map it in Fenix's `DefaultTabRepository` (for `Tab`) or in the coordinator's tab group processing (for `TabGroup`).
3. Add test coverage in `DefaultTabDataCoordinatorTest`.
4. Update the class diagram above if the field matters to most consumers.

**Adding a field to `TabsSnapshot`**

1. Ask first whether it can be a derived lookup in `TabsSnapshotQueries.kt` instead. Prefer that if so.
2. Add the property and its KDoc to `TabsSnapshot`.
3. Compute it in `transformTabData`.
4. Add test coverage in `DefaultTabDataCoordinatorTest`.

**Adding a write API**

1. Add the method and its KDoc to the `TabDataCoordinator` interface, documenting no-op cases and ordering behavior.
2. Implement it in `DefaultTabDataCoordinator`, keeping the [rules above](#rules-the-coordinator-enforces).
3. If the tab source needs a new operation, add it to `TabRepository` and implement it in `DefaultTabRepository` and `FakeTabRepository`.
4. Add test coverage in `DefaultTabDataCoordinatorTest`.
5. Route the consumers' existing direct calls through the new API.

**Adding a new data source**

1. Add its flow to the `combine` in `DefaultTabDataCoordinator`, and extend `CombinedTabData`.
2. If the source is owned by the app, define a repository interface in this component rather than depending on the source directly.

### Testing

* Coordinator tests live in `DefaultTabDataCoordinatorTest` and use `FakeTabRepository`.
* For consumers, build a real `DefaultTabDataCoordinator` with test sources and call `initialize()`, as `TabStorageMiddlewareTest` does:

```kotlin
val tabDataCoordinator =
    DefaultTabDataCoordinator(
        tabRepository = DefaultTabRepository(browserStore = browserStore, tabsUseCases = TabsUseCases(store = browserStore)),
        tabGroupRepository = tabGroupRepository,
        isInactiveTabsEnabled = { inactiveTabsEnabled },
        isTabGroupsEnabled = { tabGroupsEnabled },
        scope = backgroundScope,
        dispatcher = StandardTestDispatcher(testScheduler),
        dateTimeProvider = fakeDateTimeProvider,
    )
tabDataCoordinator.initialize()
```

* Run the component's unit tests with:

```shell
./mach gradle -p mobile/android/android-components :components:feature-tabdata-coordinator:testDebugUnitTest
```

* For any bugs, please file them under the `Tabs` Bugzilla component.

## Code

* [Tab Data Coordinator](https://searchfox.org/firefox-main/rev/0b6146da78e50813404df3e74fe0dc29257252bc/mobile/android/android-components/components/feature/tabdata-coordinator/src/main/java/mozilla/components/feature/tabdata/coordinator/TabDataCoordinator.kt#56)
* [Tabs Snapshot](https://searchfox.org/firefox-main/rev/0b6146da78e50813404df3e74fe0dc29257252bc/mobile/android/android-components/components/feature/tabdata-coordinator/src/main/java/mozilla/components/feature/tabdata/coordinator/data/TabsSnapshot.kt#21)
* [Tabs Snapshot queries](https://searchfox.org/firefox-main/rev/0b6146da78e50813404df3e74fe0dc29257252bc/mobile/android/android-components/components/feature/tabdata-coordinator/src/main/java/mozilla/components/feature/tabdata/coordinator/data/TabsSnapshotQueries.kt)
* Tab Repository ([interface](https://searchfox.org/firefox-main/rev/0b6146da78e50813404df3e74fe0dc29257252bc/mobile/android/android-components/components/feature/tabdata-coordinator/src/main/java/mozilla/components/feature/tabdata/coordinator/repository/TabRepository.kt#11), [default implementation in Fenix](https://searchfox.org/firefox-main/rev/0b6146da78e50813404df3e74fe0dc29257252bc/mobile/android/fenix/app/src/main/java/org/mozilla/fenix/components/tabs/DefaultTabRepository.kt#26))
* Components attachment: `Components.tabDataCoordinator` in [Components.kt](https://searchfox.org/firefox-main/rev/0b6146da78e50813404df3e74fe0dc29257252bc/mobile/android/fenix/app/src/main/java/org/mozilla/fenix/components/Components.kt#577-589)
* Initialization: `components.tabDataCoordinator.initialize()` in [FenixApplication.kt](https://searchfox.org/firefox-main/rev/0b6146da78e50813404df3e74fe0dc29257252bc/mobile/android/fenix/app/src/main/java/org/mozilla/fenix/FenixApplication.kt#475)
* [Coordinator tests](https://searchfox.org/firefox-main/rev/0b6146da78e50813404df3e74fe0dc29257252bc/mobile/android/android-components/components/feature/tabdata-coordinator/src/test/java/mozilla/components/feature/tabdata/coordinator/DefaultTabDataCoordinatorTest.kt)
* [Reference consumer: TabStorageMiddleware](https://searchfox.org/firefox-main/rev/0b6146da78e50813404df3e74fe0dc29257252bc/mobile/android/fenix/app/src/main/java/org/mozilla/fenix/tabstray/redux/middleware/TabStorageMiddleware.kt)

## References

* [Spike](https://bugzilla.mozilla.org/show_bug.cgi?id=2053265)
* [Original architecture proposal](https://www.figma.com/board/j7h2lms8no3wiTdZ0ey6go/Tab-Groups-MVP-architecting?node-id=269-237&p=f&t=ftM8B32hLJcWz58V-0)
* [ADR](https://searchfox.org/firefox-main/source/mobile/android/docs/adrs/0002-global-organized-tab-data.md)
* [Implementation ticket](https://bugzilla.mozilla.org/show_bug.cgi?id=2060386)
* [Ticket to integrate into TabManagementFragment](https://bugzilla.mozilla.org/show_bug.cgi?id=2073219)
* Tab data in Browser State: `BrowserState.tabs` in [BrowserState.kt](https://searchfox.org/firefox-main/rev/0b6146da78e50813404df3e74fe0dc29257252bc/mobile/android/android-components/components/browser/state/src/main/java/mozilla/components/browser/state/state/BrowserState.kt#43)
* [Tab group data in Tab Group Repository](https://searchfox.org/firefox-main/rev/0b6146da78e50813404df3e74fe0dc29257252bc/mobile/android/android-components/components/feature/tabgroups-storage/src/main/java/mozilla/components/feature/tabgroups/storage/repository/TabGroupRepository.kt)
