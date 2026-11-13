/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.helpers

import android.content.ComponentName
import android.content.pm.PackageManager
import android.os.Process
import android.util.Log
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.state.state.selectedOrDefaultPrivateSearchEngine
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.storage.sync.PlacesHistoryStorage
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.ui.efficiency.logging.TestLogging

/**
 * What the app actually holds, sampled before and after a test.
 *
 * Two questions this answers that a screenshot cannot.
 *
 * **Did this test arrive dirty, and did cleanup establish the contract?** Arrival is sampled before cleanup as
 * evidence. The enforced before-cleanup and after-cleanup samples answer whether the harness actually established and
 * restored its declared state boundary.
 *
 * **Is a UI failure actually a UI failure?** If history has three entries and the history screen shows none, that is a
 * specific and reportable bug in the path from the store to the UI. If the store is empty too, the test never created
 * the data and the UI is innocent. Today both look identical: "element not found".
 *
 * Sampling is deliberately cheap and count-based. Reading every row of every store on both sides of every test would
 * cost more than the information is worth, and counts plus a diff answer both questions above. An unreadable value is
 * retained as evidence and fails an enforced isolation boundary because silence would make that boundary unverifiable.
 */
object StateProbe {

    private val contributors: List<StateContributor> =
        listOf(
            contributor(
                name = "browserStore",
                fields =
                    setOf(
                        "tabs",
                        "tabsPrivate",
                        "downloads",
                        "pendingUndoTabs",
                        "recentlyClosedStoreTabs",
                    ),
                captureCost = StateCaptureCost.IN_MEMORY,
                policy = StateResourcePolicy.RESTORE,
                boundaryBaseline =
                    mapOf(
                        "tabs" to 0,
                        "tabsPrivate" to 0,
                        "downloads" to 0,
                        "pendingUndoTabs" to 0,
                        "recentlyClosedStoreTabs" to 0,
                    ),
                controlDrivers = setOf("tabs", "downloads", "recentlyClosedTabs"),
            ) {
                mapOf(
                    "tabs" to observe { appContext.components.core.store.state.tabs.size },
                    "tabsPrivate" to
                        observe {
                            appContext.components.core.store.state.tabs.count { it.content.private }
                        },
                    "downloads" to observe { appContext.components.core.store.state.downloads.size },
                    "pendingUndoTabs" to
                        observe {
                            appContext.components.core.store.state.undoHistory.tabs.size
                        },
                    "recentlyClosedStoreTabs" to
                        observe {
                            appContext.components.core.store.state.closedTabs.size
                        },
                )
            },
            contributor(
                name = "places",
                fields = setOf("history", "bookmarks", "topSites"),
                captureCost = StateCaptureCost.STORAGE_IO,
                sensitivity = StateSensitivity.AGGREGATE_ONLY,
                policy = StateResourcePolicy.RESTORE,
                boundaryBaseline = mapOf("history" to 0, "bookmarks" to 0),
                controlDrivers = setOf("history", "bookmarks", "pinnedSites"),
            ) {
                mapOf(
                    "history" to
                        observe {
                            runBlocking { PlacesHistoryStorage(appContext.applicationContext).getVisited().size }
                        },
                    "bookmarks" to
                        observe {
                            runBlocking {
                                appContext.components.core.bookmarksStorage
                                    .getTree(BookmarkRoot.Mobile.id)
                                    .getOrNull()
                                    ?.children
                                    ?.size ?: 0
                            }
                        },
                    "topSites" to
                        observe {
                            runBlocking { appContext.components.core.pinnedSiteStorage.getPinnedSites().size }
                        },
                )
            },
            contributor(
                name = "savedUserData",
                fields = setOf("logins", "addresses", "creditCards"),
                captureCost = StateCaptureCost.STORAGE_IO,
                sensitivity = StateSensitivity.AGGREGATE_ONLY,
                policy = StateResourcePolicy.RESTORE,
                boundaryBaseline = mapOf("logins" to 0, "addresses" to 0, "creditCards" to 0),
                controlDrivers = setOf("logins", "autofill"),
            ) {
                mapOf(
                    "logins" to
                        observe {
                            runBlocking { appContext.components.core.passwordsStorage.list().size }
                        },
                    "addresses" to
                        observe {
                            runBlocking { appContext.components.core.autofillStorage.getAllAddresses().size }
                        },
                    "creditCards" to
                        observe {
                            runBlocking { appContext.components.core.autofillStorage.getAllCreditCards().size }
                        },
                )
            },
            contributor(
                name = "isolationStorage",
                fields = setOf("sitePermissions", "savedSessions"),
                captureCost = StateCaptureCost.STORAGE_IO,
                sensitivity = StateSensitivity.AGGREGATE_ONLY,
                policy = StateResourcePolicy.RESTORE,
                boundaryBaseline = mapOf("sitePermissions" to 0, "savedSessions" to 0),
                controlDrivers = setOf("permissions", "sessions"),
            ) {
                mapOf(
                    "sitePermissions" to
                        observe {
                            runBlocking {
                                appContext.components.core.geckoSitePermissionsStorage.all().size
                            }
                        },
                    "savedSessions" to
                        observe {
                            appContext.components.core.sessionStorage.restore()?.tabs?.size ?: 0
                        },
                )
            },
            contributor(
                name = "tabOrganization",
                fields = setOf("collections", "tabGroups", "tabGroupAssignments", "recentlyClosedTabs"),
                captureCost = StateCaptureCost.STORAGE_IO,
                sensitivity = StateSensitivity.AGGREGATE_ONLY,
                policy = StateResourcePolicy.RESTORE,
                boundaryBaseline =
                    mapOf(
                        "collections" to 0,
                        "tabGroups" to 0,
                        "tabGroupAssignments" to 0,
                        "recentlyClosedTabs" to 0,
                    ),
                controlDrivers = setOf("collections", "tabGroups", "recentlyClosedTabs"),
            ) {
                mapOf(
                    "collections" to
                        observe {
                            runBlocking { appContext.components.core.tabCollectionStorage.getCollectionsList().size }
                        },
                    "tabGroups" to
                        observe {
                            runBlocking {
                                appContext.components.core.tabGroupRepository.tabGroupDataFlow.first().tabGroups.size
                            }
                        },
                    "tabGroupAssignments" to
                        observe {
                            runBlocking {
                                appContext.components.core.tabGroupRepository.tabGroupDataFlow
                                    .first()
                                    .tabGroupAssignments
                                    .size
                            }
                        },
                    "recentlyClosedTabs" to
                        observe {
                            runBlocking {
                                appContext.components.core.recentlyClosedTabsStorage.value.getTabs().first().size
                            }
                        },
                )
            },
            contributor(
                name = "appRuntime",
                fields = setOf("searchActive", "voiceInputRequested", "voiceInputResult"),
                captureCost = StateCaptureCost.IN_MEMORY,
                policy = StateResourcePolicy.RESTORE,
                boundaryBaseline =
                    mapOf(
                        "searchActive" to false,
                        "voiceInputRequested" to false,
                        "voiceInputResult" to false,
                    ),
                controlDrivers = setOf("runtimeCleanup"),
            ) {
                mapOf(
                    "searchActive" to observe { appContext.components.appStore.state.searchState.isSearchActive },
                    "voiceInputRequested" to
                        observe {
                            appContext.components.appStore.state.voiceSearchState.isRequestingVoiceInput
                        },
                    "voiceInputResult" to
                        observe {
                            appContext.components.appStore.state.voiceSearchState.voiceInputResult != null
                        },
                )
            },
            contributor(
                name = "preferences",
                fields = setOf("preferenceOverrideCount", "preferenceOverrideIds"),
                captureCost = StateCaptureCost.IN_MEMORY,
                policy = StateResourcePolicy.RESTORE,
                boundaryBaseline =
                    mapOf("preferenceOverrideCount" to 0, "preferenceOverrideIds" to emptyList<String>()),
                controlDrivers = setOf("preferences"),
            ) {
                val overrides = HarnessPreferenceState.overrideIds()
                mapOf(
                    "preferenceOverrideCount" to observe { overrides.size },
                    "preferenceOverrideIds" to overrides,
                )
            },
            contributor(
                name = "searchConfiguration",
                fields =
                    setOf(
                        "complete",
                        "defaultEngineId",
                        "regionDefaultEngineId",
                        "privateDefaultEngineId",
                        "userSelectedEngineOverride",
                        "userSelectedPrivateEngineOverride",
                        "defaultMatchesRegion",
                        "privateMatchesDefault",
                        "customEngineCount",
                        "hiddenEngineCount",
                        "disabledShortcutCount",
                        "disabledShortcutsAtDefault",
                        "additionalEngineCount",
                        "temporaryEngineId",
                        "persistedMetadataOverrideCount",
                        "persistedCustomEngineCount",
                    ),
                captureCost = StateCaptureCost.STORAGE_IO,
                sensitivity = StateSensitivity.AGGREGATE_ONLY,
                policy = StateResourcePolicy.RESTORE,
                includeInCompatibilityState = false,
                boundaryBaseline =
                    mapOf(
                        "complete" to true,
                        "userSelectedEngineOverride" to false,
                        "userSelectedPrivateEngineOverride" to false,
                        "defaultMatchesRegion" to true,
                        "privateMatchesDefault" to true,
                        "customEngineCount" to 0,
                        "hiddenEngineCount" to 0,
                        "disabledShortcutsAtDefault" to true,
                        "additionalEngineCount" to 0,
                        "temporaryEngineId" to null,
                        "persistedMetadataOverrideCount" to 0,
                        "persistedCustomEngineCount" to 0,
                    ),
                controlDrivers = setOf("searchConfiguration", "runtimeCleanup"),
            ) {
                val browserSearch = appContext.components.core.store.state.search
                val defaultEngineId = browserSearch.selectedOrDefaultSearchEngine?.id
                mapOf(
                    "complete" to observe { browserSearch.complete },
                    "defaultEngineId" to observe { defaultEngineId },
                    "regionDefaultEngineId" to observe { browserSearch.regionDefaultSearchEngineId },
                    "privateDefaultEngineId" to observe { browserSearch.selectedOrDefaultPrivateSearchEngine?.id },
                    "userSelectedEngineOverride" to
                        observe {
                            browserSearch.userSelectedSearchEngineId != null ||
                                browserSearch.userSelectedSearchEngineName != null
                        },
                    "userSelectedPrivateEngineOverride" to
                        observe {
                            browserSearch.userSelectedPrivateSearchEngineId != null ||
                                browserSearch.userSelectedPrivateSearchEngineName != null
                        },
                    "defaultMatchesRegion" to observe { defaultEngineId == browserSearch.regionDefaultSearchEngineId },
                    "privateMatchesDefault" to
                        observe { browserSearch.selectedOrDefaultPrivateSearchEngine?.id == defaultEngineId },
                    "customEngineCount" to observe { browserSearch.customSearchEngines.size },
                    "hiddenEngineCount" to observe { browserSearch.hiddenSearchEngines.size },
                    "disabledShortcutCount" to observe { browserSearch.disabledSearchEngineIds.size },
                    "disabledShortcutsAtDefault" to
                        observe { HarnessSearchState.hasDefaultDisabledShortcuts(browserSearch) },
                    "additionalEngineCount" to observe { browserSearch.additionalSearchEngines.size },
                    "temporaryEngineId" to
                        observe {
                            appContext.components.appStore.state.searchState.selectedSearchEngine?.searchEngine?.id
                        },
                    "persistedMetadataOverrideCount" to
                        observe { HarnessSearchState.persistentMetadataOverrideCount() },
                    "persistedCustomEngineCount" to observe { HarnessSearchState.persistentCustomEngineCount() },
                )
            },
            contributor(
                name = "launcher",
                fields = setOf("launcherIcon"),
                captureCost = StateCaptureCost.PACKAGE_MANAGER,
                policy = StateResourcePolicy.RESTORE,
                boundaryBaseline = mapOf("launcherIcon" to "default"),
                controlDrivers = setOf("launcherIcon"),
            ) {
                mapOf("launcherIcon" to observe(::launcherIconAlias))
            },
            contributor(
                name = "executionIdentity",
                fields = setOf("processId"),
                captureCost = StateCaptureCost.IN_MEMORY,
                policy = StateResourcePolicy.OBSERVE,
            ) {
                mapOf("processId" to observe(Process::myPid))
            },
        )

    /** One sample of everything worth watching. Ordered so the diff reads consistently. */
    fun sample(): Map<String, Any?> = snapshot().values

    fun snapshot(): StateSnapshot {
        val contributions = contributors.map { contributor ->
            val captured =
                runCatching(contributor::capture).getOrElse { failure ->
                    contributor.fields.associateWith {
                        "unreadable: ${failure::class.simpleName}"
                    }
                }
            val values =
                contributor.fields.associateWith { field ->
                    if (field in captured) captured[field] else "unreadable: MissingValue"
                }
            StateContribution(
                name = contributor.name,
                schemaVersion = contributor.schemaVersion,
                captureCost = contributor.captureCost,
                sensitivity = contributor.sensitivity,
                policy = contributor.policy,
                includeInCompatibilityState = contributor.includeInCompatibilityState,
                boundaryBaseline = contributor.boundaryBaseline,
                controlDrivers = contributor.controlDrivers,
                values = values,
            )
        }
        val allValues = contributions.flatMap { it.values.entries }
        check(allValues.size == allValues.map { it.key }.toSet().size) {
            "State contributors declare duplicate field ownership"
        }
        val values = contributions.filter(StateContribution::includeInCompatibilityState).flatMap { it.values.entries }
        return StateSnapshot(
            values = values.associate { it.toPair() },
            contributions = contributions,
        )
    }

    fun descriptors(): List<StateContributor> = contributors.toList()

    /**
     * Emit a sample on the structured stream.
     *
     * Consumers pair lifecycle phases by testId and compute their own transitions so the emitted facts remain stable.
     */
    fun record(phase: String, testId: String): StateSnapshot {
        val snapshot = snapshot()
        val state = snapshot.values
        runCatching {
            val reporter = TestLogging.installed()
            reporter.record(
                "state",
                mapOf("phase" to phase, "testId" to testId) + state,
            )
            val snapshotId = "${Process.myPid()}:${snapshotSequence.incrementAndGet()}"
            snapshot.contributions.forEachIndexed { index, contribution ->
                reporter.record(
                    "stateSnapshot",
                    mapOf(
                        "schemaVersion" to SNAPSHOT_SCHEMA_VERSION,
                        "snapshotId" to snapshotId,
                        "chunkIndex" to index,
                        "chunkCount" to snapshot.contributions.size,
                        "phase" to phase,
                        "testId" to testId,
                        "contributors" to listOf(contribution.asRecord()),
                    ),
                )
            }
        }
            .onFailure { Log.i(TAG, "state probe failed at $phase: ${it.message}") }
        return snapshot
    }

    fun assertIsolated(phase: String, testId: String) {
        val snapshot = record(phase, testId)
        val state = snapshot.contributions.flatMap { it.values.entries }.associate { it.toPair() }
        val violations =
            contributors
                .flatMap { contributor -> contributor.boundaryBaseline.entries }
                .filter { (field, expected) -> state[field] != expected }
                .map { (field, _) -> "$field=${state[field]}" }
        try {
            TestLogging.installed()
                .record(
                    "isolation",
                    mapOf(
                        "phase" to phase,
                        "testId" to testId,
                        "verified" to violations.isEmpty(),
                        "violations" to violations.joinToString(","),
                    ),
                )
        } catch (_: Throwable) {}
        check(violations.isEmpty()) {
            "Harness state was not isolated at $phase for $testId: ${violations.joinToString()}"
        }
    }

    /** Which launcher alias is enabled, by name, or "default" when none has been overridden. */
    private fun launcherIconAlias(): String {
        val pm = appContext.packageManager
        val pkg = appContext.packageName
        val info =
            pm.getPackageInfo(
                pkg,
                PackageManager.GET_ACTIVITIES or PackageManager.MATCH_DISABLED_COMPONENTS,
            )
        val enabled =
            info.activities
                .orEmpty()
                .map { it.name }
                .filter { it.startsWith("$pkg.App") || it == "$pkg.AlternativeApp" }
                .filter {
                    pm.getComponentEnabledSetting(ComponentName(pkg, it)) ==
                        PackageManager.COMPONENT_ENABLED_STATE_ENABLED
                }
        return enabled.firstOrNull()?.removePrefix("$pkg.") ?: "default"
    }

    private fun contributor(
        name: String,
        fields: Set<String>,
        captureCost: StateCaptureCost,
        sensitivity: StateSensitivity = StateSensitivity.NONE,
        policy: StateResourcePolicy,
        includeInCompatibilityState: Boolean = true,
        boundaryBaseline: Map<String, Any?> = emptyMap(),
        controlDrivers: Set<String> = emptySet(),
        capture: () -> Map<String, Any?>,
    ): StateContributor =
        object : StateContributor {
            override val name = name
            override val schemaVersion = 1
            override val fields = fields
            override val captureCost = captureCost
            override val sensitivity = sensitivity
            override val policy = policy
            override val includeInCompatibilityState = includeInCompatibilityState
            override val boundaryBaseline = boundaryBaseline
            override val controlDrivers = controlDrivers

            override fun capture(): Map<String, Any?> = capture()
        }

    private inline fun observe(read: () -> Any?): Any? =
        runCatching(read).getOrElse { "unreadable: ${it::class.simpleName}" }

    private val snapshotSequence = AtomicLong()
    private const val TAG = "StateProbe"
    private const val SNAPSHOT_SCHEMA_VERSION = 3
}
