/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.session.storage

import android.content.Context
import android.net.Uri
import android.os.SystemClock
import android.util.AtomicFile
import androidx.annotation.CheckResult
import androidx.annotation.VisibleForTesting
import androidx.annotation.WorkerThread
import androidx.core.net.toUri
import java.io.File
import java.util.Locale
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.CoroutineScope
import mozilla.components.browser.session.storage.facts.emitRestoreFact
import mozilla.components.browser.session.storage.facts.emitSaveFact
import mozilla.components.browser.session.storage.serialize.BrowserStateReader
import mozilla.components.browser.session.storage.serialize.BrowserStateWriter
import mozilla.components.browser.state.selector.normalTabs
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.recover.RecoverableTab
import mozilla.components.browser.state.state.recover.isContentUri
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.engine.Engine
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.android.net.isReadable

private const val STORE_FILE_NAME_FORMAT = "mozilla_components_session_storage_%s.json"

private val sessionFileLock = Any()

/** Session storage for (partially) persisting the state of [BrowserStore] to disk. */
class SessionStorage(
    private val context: Context,
    private val engine: Engine,
    private val crashReporting: CrashReporting? = null,
    private val applicationScope: CoroutineScope,
) : AutoSave.Storage {
    private val logger = Logger("SessionStorage")
    private val stateWriter = BrowserStateWriter()
    private val stateReader = BrowserStateReader()
    private val currentTimeInMillis: () -> Long = { SystemClock.elapsedRealtime() }

    /**
     * Reads the saved state from disk. Returns null if no state was found on disk or if reading the file failed.
     *
     * @param predicate an optional predicate applied to each tab to determine if it should be restored.
     */
    @WorkerThread
    fun restore(predicate: (RecoverableTab) -> Boolean = { true }): RecoverableBrowserState? {
        synchronized(sessionFileLock) {
            val file = getFileForEngine(context, engine)
            val start = currentTimeInMillis()
            var tabsInFile = 0

            val state =
                stateReader.read(engine, file) { tab ->
                    tabsInFile++
                    // Tab whose url is a contentUri which is not readable won't be restored
                    // e.g. Pdfs opened from a different app that only provided transient permission for
                    // a specific session, that has already expired
                    predicate(tab) && (!tab.isContentUri() || isUriReadable(tab.state.url.toUri()))
                }

            if (state != null) {
                emitRestoreFact(
                    durationMs = currentTimeInMillis() - start,
                    fileBytes = file.baseFile.length(),
                    tabsInFile = tabsInFile,
                    tabsRestored = state.tabs.size,
                )
            }
            return state
        }
    }

    @VisibleForTesting internal fun isUriReadable(uri: Uri): Boolean = uri.isReadable(context.contentResolver)

    /** Clears the state saved on disk. */
    @WorkerThread
    fun clear() {
        removeSnapshotFromDisk(context, engine)
    }

    /** Saves the given state to disk. */
    @WorkerThread
    override fun save(state: BrowserState): Boolean {
        if (state.normalTabs.isEmpty()) {
            clear()
            return true
        }

        // "about:crashparent" is meant for testing purposes only.  If saved/restored then it will
        // continue to crash the app until data is cleared.  Therefore, we are filtering it out.
        val updatedTabList = state.tabs.filterNot { it.content.url == "about:crashparent" }
        val updatedState = state.copy(tabs = updatedTabList)

        val stateToPersist =
            if (updatedState.selectedTabId != null && updatedState.selectedTab == null) {
                // Needs investigation to figure out and prevent cause:
                // https://github.com/mozilla-mobile/android-components/issues/8417
                logger.error("Selected tab ID set, but tab with matching ID not found. Clearing selection.")
                updatedState.copy(selectedTabId = null)
            } else {
                updatedState
            }

        return synchronized(sessionFileLock) {
            try {
                val file = getFileForEngine(context, engine)
                val start = currentTimeInMillis()
                val written = stateWriter.write(stateToPersist, file)
                if (written) {
                    emitSaveFact(
                        durationMs = currentTimeInMillis() - start,
                        fileBytes = file.baseFile.length(),
                        // Matches the tabs that BrowserStateWriter writes (no private tabs). Update this count if
                        // the writer starts skipping other tabs.
                        tabsInFile = stateToPersist.normalTabs.size,
                    )
                }
                written
            } catch (e: OutOfMemoryError) {
                crashReporting?.submitCaughtException(e)
                logger.error("Failed to save state to disk due to OutOfMemoryError", e)
                false
            }
        }
    }

    /** Starts configuring automatic saving of the state. */
    @CheckResult
    fun autoSave(
        store: BrowserStore,
        interval: Long = AutoSave.DEFAULT_INTERVAL_MILLISECONDS,
        unit: TimeUnit = TimeUnit.MILLISECONDS,
    ): AutoSave {
        return AutoSave(store, this, unit.toMillis(interval), applicationScope)
    }
}

private fun removeSnapshotFromDisk(context: Context, engine: Engine) {
    synchronized(sessionFileLock) {
        getFileForEngine(context, engine).delete()
    }
}

@VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
internal fun getFileForEngine(context: Context, engine: Engine): AtomicFile {
    return AtomicFile(
        File(
            context.filesDir,
            String.format(STORE_FILE_NAME_FORMAT, engine.name()).lowercase(Locale.ROOT),
        )
    )
}
