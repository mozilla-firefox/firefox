/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.session.storage

import android.os.SystemClock
import androidx.annotation.VisibleForTesting
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.state.selector.normalTabs
import mozilla.components.browser.state.selector.selectedNormalTab
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.lib.state.ext.flow
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.base.utils.NamedThreadFactory

/**
 * Automatically saves the current state of a [BrowserStore] to a provided [Storage] backend.
 *
 * This class provides configurable triggers for persisting the browser session, such as periodic foreground saving,
 * saving when the application moves to the background, or saving when specific session changes occur (e.g., tabs
 * added/removed or navigation completed).
 *
 * To prevent excessive disk I/O, it enforces a [minimumIntervalMs] between save operations.
 *
 * @property store The [BrowserStore] whose state should be observed and saved.
 * @property sessionStorage The [Storage] implementation used to persist the state.
 * @property minimumIntervalMs The minimum time in milliseconds that must pass between save operations.
 * @property applicationScope The [CoroutineScope] used for performing the save operations on a background thread. This
 *   scope should outlive individual Activities and survive configuration changes; it is the caller's responsibility to
 *   cancel it when the application is destroyed.
 * @property ioDispatcher The [CoroutineDispatcher] used to execute the save operations. Defaults to [Dispatchers.IO].
 */
class AutoSave(
    private val store: BrowserStore,
    private val sessionStorage: Storage,
    private val minimumIntervalMs: Long,
    private val applicationScope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) {
    interface Storage {
        /**
         * Saves the provided [BrowserState].
         *
         * @param state the state to save.
         * @return true if save was successful, otherwise false.
         */
        fun save(state: BrowserState): Boolean
    }

    internal val logger = Logger("SessionStorage/AutoSave")
    internal var saveJob: Job? = null

    @VisibleForTesting internal var monitoringJob: Job? = null
    private var lastSaveTimestamp: Long = now()

    /**
     * Saves the state periodically when the app is in the foreground.
     *
     * @param interval The interval in which the state should be saved to disk.
     * @param unit The time unit of the [interval] parameter.
     */
    fun periodicallyInForeground(
        interval: Long = 300,
        unit: TimeUnit = TimeUnit.SECONDS,
        scheduler: ScheduledExecutorService =
            Executors.newSingleThreadScheduledExecutor(NamedThreadFactory("AutoSave")),
        lifecycle: Lifecycle = ProcessLifecycleOwner.get().lifecycle,
    ): AutoSave {
        lifecycle.addObserver(
            AutoSavePeriodically(
                this,
                scheduler,
                interval,
                unit,
            )
        )
        return this
    }

    /**
     * Saves the state when the app goes to the background. The engine may send the selected tab's latest state shortly
     * after, so the state is saved once more when it arrives.
     */
    fun whenGoingToBackground(lifecycle: Lifecycle = ProcessLifecycleOwner.get().lifecycle): AutoSave {
        lifecycle.addObserver(AutoSaveBackground(this, store, applicationScope, ioDispatcher))
        return this
    }

    /** Saves the state automatically when the sessions change, e.g. sessions get added and removed. */
    fun whenSessionsChange(scope: CoroutineScope = applicationScope): AutoSave {
        monitoringJob =
            scope.launch(ioDispatcher) {
                val monitoring = StateMonitoring(this@AutoSave)
                monitoring.monitor(store.flow())
            }
        return this
    }

    /**
     * Triggers saving the current state to disk.
     *
     * This method will not schedule a new save job if a job is already in flight. Additionally it will obey the
     * interval passed to [SessionStorage.autoSave]; job may get delayed.
     *
     * @param delaySave Whether to delay the save job to obey the interval passed to [SessionStorage.autoSave].
     */
    @Synchronized
    internal fun triggerSave(delaySave: Boolean = true): Job {
        val currentJob = saveJob

        if (currentJob != null && currentJob.isActive) {
            logger.debug("Skipping save, other job already in flight")
            return currentJob
        }

        val now = now()
        val delayMs = lastSaveTimestamp + minimumIntervalMs - now
        lastSaveTimestamp = now

        applicationScope
            .launch(ioDispatcher) {
                if (delaySave && delayMs > 0) {
                    logger.debug("Delaying save (${delayMs}ms)")
                    delay(delayMs)
                }

                saveCurrentState()
            }
            .also {
                saveJob = it
                return it
            }
    }

    /**
     * Saves the current state. Unlike [triggerSave], this never skips: if a save job is still scheduled or running, the
     * state is saved once it completes.
     */
    @Synchronized
    internal fun triggerQueuedSave(): Job {
        val previousJob = saveJob
        lastSaveTimestamp = now()

        return applicationScope
            .launch(ioDispatcher) {
                previousJob?.join()
                saveCurrentState()
            }
            .also { saveJob = it }
    }

    private fun saveCurrentState() {
        val start = now()

        try {
            val state = store.state
            sessionStorage.save(state)
        } finally {
            val took = now() - start
            logger.debug("Saved state to disk [${took}ms]")
        }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE) internal fun now() = SystemClock.elapsedRealtime()

    companion object {
        // Minimum interval between saving states.
        const val DEFAULT_INTERVAL_MILLISECONDS = 2000L
    }
}

/** [LifecycleObserver] to start/stop a task that saves the state at a periodic interval. */
private class AutoSavePeriodically(
    private val autoSave: AutoSave,
    private val scheduler: ScheduledExecutorService,
    private val interval: Long,
    private val unit: TimeUnit,
) : DefaultLifecycleObserver {
    private var scheduledFuture: ScheduledFuture<*>? = null

    // Use of scheduleAtFixedRate is discouraged in favor of scheduleWithFixedDelay.
    // Our usage is safe, however, because we cancel the scheduled task when the app
    // is going to the background (see onStop below).
    @Suppress("DiscouragedApi")
    override fun onStart(owner: LifecycleOwner) {
        scheduledFuture =
            scheduler.scheduleAtFixedRate(
                {
                    autoSave.logger.info("Save: Periodic")
                    autoSave.triggerSave()
                },
                interval,
                interval,
                unit,
            )
    }

    override fun onStop(owner: LifecycleOwner) {
        scheduledFuture?.cancel(false)
    }
}

/**
 * [LifecycleObserver] to save the state when the app goes to the background, and once more when the selected tab's
 * engine session state arrives after that.
 */
private class AutoSaveBackground(
    private val autoSave: AutoSave,
    private val store: BrowserStore,
    private val scope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher,
) : DefaultLifecycleObserver {
    private var monitoringJob: Job? = null

    override fun onStart(owner: LifecycleOwner) {
        monitoringJob?.cancel()
        monitoringJob = null
    }

    override fun onStop(owner: LifecycleOwner) {
        monitoringJob?.cancel()

        autoSave.logger.info("Save: Background")
        val stateWhenStopped = store.state.selectedEngineSessionState()
        autoSave.triggerSave(delaySave = false)

        monitoringJob =
            scope.launch(ioDispatcher) {
                // EngineSessionState has no equals: every update is a new object.
                store.flow().map { it.selectedEngineSessionState() }.first { it !== stateWhenStopped }

                autoSave.logger.info("Save: Engine session state updated in background")
                autoSave.triggerQueuedSave()
            }
    }

    // Private tabs are never saved.
    private fun BrowserState.selectedEngineSessionState() = selectedNormalTab?.engineState?.engineSessionState
}

private class StateMonitoring(private val autoSave: AutoSave) {
    private var lastObservation: Observation? = null

    suspend fun monitor(flow: Flow<BrowserState>) {
        flow
            .map { state ->
                Observation(
                    selectedTabId = state.selectedTabId,
                    tabs = state.normalTabs.size,
                    loading = state.selectedTab?.content?.loading,
                )
            }
            .distinctUntilChanged()
            .collect { observation -> onChange(observation) }
    }

    private fun onChange(observation: Observation) {
        if (lastObservation == null) {
            // If this is the first observation then just remember it. We only want to react to
            // changes and not the initial state.
            lastObservation = observation
            return
        }

        val triggerSave =
            if (lastObservation!!.selectedTabId != observation.selectedTabId) {
                autoSave.logger.info("Save: New tab selected")
                true
            } else if (lastObservation!!.tabs != observation.tabs) {
                autoSave.logger.info("Save: Number of tabs changed")
                true
            } else if (lastObservation!!.loading != observation.loading && observation.loading == false) {
                autoSave.logger.info("Save: Load finished")
                true
            } else {
                false
            }

        lastObservation = observation

        if (triggerSave) {
            autoSave.triggerSave()
        }
    }

    private data class Observation(
        val selectedTabId: String?,
        val tabs: Int,
        val loading: Boolean?,
    )
}
