/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.helpers

import android.content.ComponentName
import android.content.pm.PackageManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import mozilla.components.browser.state.action.DownloadAction
import mozilla.components.browser.state.action.RecentlyClosedAction
import mozilla.components.browser.state.action.UndoAction
import mozilla.components.browser.storage.sync.PlacesHistoryStorage
import org.mozilla.fenix.components.appstate.AppAction.CollectionsChange
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.AppAndSystemHelper.deleteBookmarksStorage
import org.mozilla.fenix.helpers.AppAndSystemHelper.deletePinnedSitesStorage
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.ui.efficiency.logging.TestLogging

/**
 * The app state this harness clears between tests, as one named list.
 *
 * Two reasons it is here rather than inline in BaseTest's rule.
 *
 * **It has to run at both ends.** It used to run only at the START of a test, so a failing test left everything it
 * created on the device until the next test cleared up before itself --- and if it was the last test in the class,
 * indefinitely. That is why devices are found dirty long after a run.
 *
 * **A failed clear has to be distinguishable from a leak.** Every clear was wrapped in runCatching with a log line, so
 * a clear that errored and a genuine state leak produced identical symptoms: a later test starting with state it did
 * not create. [clear] returns what failed and puts it on the structured stream; BaseTest then fails the boundary while
 * preserving any earlier test failure as the primary error.
 *
 * This is the only app-data cleanup list used by efficiency tests; legacy setup rules are not part of their lifecycle.
 */
object AppDataCleaner {

    /** One clear, named so a failure can say which. */
    private class Step(val name: String, val run: suspend () -> Unit)

    private val steps =
        listOf(
            Step("searchConfiguration") { HarnessSearchState.clear() },
            Step("preferences") { HarnessPreferenceState.clear() },
            Step("bookmarks") { deleteBookmarksStorage() },
            Step("pinnedSites") { deletePinnedSitesStorage() },
            Step("history") {
                withContext(Dispatchers.IO) {
                    PlacesHistoryStorage(appContext.applicationContext).deleteEverything()
                }
            },
            Step("permissions") {
                appContext.components.core.permissionStorage.deleteAllSitePermissions()
                appContext.components.core.geckoSitePermissionsStorage.clearTemporaryPermissions()
            },
            // A leftover address changes the Autofill settings layout and can push "Add address"
            // off-screen; a leftover card replaces "Add card" with "Manage cards", so a card test
            // starts on a different screen than it expects.
            Step("autofill") {
                withContext(Dispatchers.IO) {
                    val autofill = appContext.components.core.autofillStorage
                    autofill.getAllAddresses().forEach { autofill.deleteAddress(it.guid) }
                    autofill.getAllCreditCards().forEach { autofill.deleteCreditCard(it.guid) }
                }
            },
            // Inherited logins mean a re-submit of the same credentials shows no save prompt, which
            // reads as a spurious failure.
            Step("logins") {
                withContext(Dispatchers.IO) { appContext.components.core.passwordsStorage.wipeLocal() }
            },
            Step("tabs") { clearTabsAndPendingUndo() },
            Step("recentlyClosedTabs") {
                appContext.components.core.recentlyClosedTabsStorage.value.removeAllTabs()
                appContext.components.core.store.dispatch(RecentlyClosedAction.ReplaceTabsAction(emptyList()))
            },
            Step("sessions") {
                withContext(Dispatchers.IO) { appContext.components.core.sessionStorage.clear() }
            },
            Step("collections") {
                val storage = appContext.components.core.tabCollectionStorage
                storage.getCollectionsList().forEach { storage.removeCollection(it) }
                storage.cachedTabCollections = emptyList()
                appContext.components.appStore.dispatch(CollectionsChange(emptyList()))
            },
            Step("tabGroups") {
                appContext.components.core.tabGroupRepository.deleteAllTabGroupData()
            },
            Step("downloads") {
                appContext.components.core.store.dispatch(DownloadAction.RemoveAllDownloadsAction)
            },
            Step("launcherIcon") { resetLauncherIconAliases() },
        )

    /**
     * Run every clear. Returns the names of the ones that failed.
     *
     * This function attempts every step and returns all failures instead of stopping at the first. BaseTest enforces
     * the returned result after the complete cleanup attempt has been recorded.
     *
     * `phase` is "before" or "after", so a consumer can tell a test that started dirty from one that failed to tidy up
     * after itself.
     */
    fun clear(phase: String, testId: String): List<String> {
        val failed = mutableListOf<String>()
        val failureDetails = linkedMapOf<String, String>()
        runBlocking {
            for (step in steps) {
                runCatching { step.run() }
                    .onFailure {
                        failed += step.name
                        failureDetails[step.name] = "${it::class.simpleName}: ${it.message}"
                        Log("${step.name} clear failed at $phase: ${it.message}")
                    }
            }
        }
        try {
            TestLogging.installed()
                .record(
                    "cleanup",
                    mapOf(
                        "phase" to phase,
                        "testId" to testId,
                        "failed" to failed.joinToString(","),
                        "failureDetails" to failureDetails,
                    ),
                )
        } catch (_: Throwable) {}
        return failed
    }

    private fun Log(msg: String) = android.util.Log.i("AppDataCleaner", msg)

    private suspend fun clearTabsAndPendingUndo() {
        val components = appContext.components
        val store = components.core.store
        val hadPendingUndo = store.state.undoHistory.tabs.any { !it.state.private }
        if (store.state.undoHistory.tabs.isNotEmpty()) {
            store.dispatch(UndoAction.ClearRecoverableTabs(store.state.undoHistory.tag))
        }
        val expectedRecentlyClosedIds = if (hadPendingUndo) store.state.closedTabs.map { it.id }.toSet() else emptySet()
        components.useCases.tabsUseCases.removeAllTabs(recoverable = false)
        if (store.state.undoHistory.tabs.isNotEmpty()) {
            store.dispatch(UndoAction.ClearRecoverableTabs(store.state.undoHistory.tag))
        }
        withTimeout(TAB_CLEAR_TIMEOUT_MS) {
            while (store.state.tabs.isNotEmpty() || store.state.undoHistory.tabs.isNotEmpty()) {
                delay(10)
            }
        }
        if (expectedRecentlyClosedIds.isNotEmpty()) {
            withTimeout(RECENTLY_CLOSED_SYNC_TIMEOUT_MS) {
                val storage = components.core.recentlyClosedTabsStorage.value
                while (!storage.getTabs().first().map { it.id }.toSet().containsAll(expectedRecentlyClosedIds)) {
                    delay(10)
                }
            }
        }
    }

    /**
     * Put the launcher icon back to the manifest default.
     *
     * The app icon is not app data --- changing it calls PackageManager.setComponentEnabledSetting on activity-aliases,
     * which lives in the package manager's own state. `pm clear` wipes /data/data/<pkg> and runtime permissions and
     * does not touch it, so an icon a test switched stays switched for every later test on that device.
     *
     * That is not theoretical: verifyTheChangeAppIconButtonTest sets the icon to Dark, and
     * verifyTheDefaultAppIconSettingTest then asserts it reads Default. Whether that fails depends on which order the
     * work queue happens to hand them to the same device, so it is an intermittent failure with no intermittent cause.
     *
     * COMPONENT_ENABLED_STATE_DEFAULT restores whatever the manifest declares, which is the default icon enabled and
     * the alternatives disabled --- a reset, not a guess at the right state.
     */
    private fun resetLauncherIconAliases() {
        val pm = appContext.packageManager
        val pkg = appContext.packageName
        val info =
            pm.getPackageInfo(
                pkg,
                PackageManager.GET_ACTIVITIES or PackageManager.MATCH_DISABLED_COMPONENTS,
            )
        info.activities
            .orEmpty()
            .map { it.name }
            .filter { it.startsWith("$pkg.App") || it == "$pkg.AlternativeApp" }
            .forEach { name ->
                pm.setComponentEnabledSetting(
                    ComponentName(pkg, name),
                    PackageManager.COMPONENT_ENABLED_STATE_DEFAULT,
                    PackageManager.DONT_KILL_APP,
                )
            }
    }

    private const val TAB_CLEAR_TIMEOUT_MS = 2_000L
    private const val RECENTLY_CLOSED_SYNC_TIMEOUT_MS = 5_000L
}
