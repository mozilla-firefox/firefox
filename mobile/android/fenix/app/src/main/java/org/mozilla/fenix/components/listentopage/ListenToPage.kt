/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.listentopage

import android.app.PendingIntent
import android.content.Context
import android.os.Build
import android.os.PowerManager
import androidx.core.content.ContextCompat
import androidx.core.content.getSystemService
import kotlinx.coroutines.CoroutineScope
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.base.profiler.Profiler
import mozilla.components.feature.listentopage.ListenMiddleware
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.ListenStore
import mozilla.components.feature.listentopage.content.ContentProvider
import mozilla.components.feature.listentopage.listenReducer
import mozilla.components.feature.listentopage.playback.DirectoryAudioFileCache
import mozilla.components.feature.listentopage.playback.ListenPlaybackController
import mozilla.components.feature.listentopage.settings.ListenSettings
import mozilla.components.feature.listentopage.synthesis.SpeechSynthesizer
import mozilla.components.feature.media.service.AbstractMediaSessionService
import mozilla.components.support.base.facts.register
import mozilla.components.support.base.ids.SharedIdsHelper
import mozilla.components.support.locale.LocaleManager

private const val OPEN_TAB_PENDING_INTENT_TAG = "org.mozilla.fenix.listentopage.OPEN_TAB"

internal fun Context.openTabPendingIntent(tabId: String): PendingIntent? {
    val intent =
        packageManager.getLaunchIntentForPackage(packageName)?.apply {
            action = AbstractMediaSessionService.ACTION_SWITCH_TAB
            putExtra(AbstractMediaSessionService.EXTRA_TAB_ID, tabId)
        } ?: return null

    return PendingIntent.getActivity(
        this,
        SharedIdsHelper.getIdForTag(this, OPEN_TAB_PENDING_INTENT_TAG),
        intent,
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )
}

/**
 * Provides access to the components that read a page out loud.
 *
 * @param browserStore Used to find the engine session of the tab an article is extracted from.
 * @param context Used to reach the speech engine, the audio cache and the playback service.
 * @param scope The application scope. The [store] outlives any screen, because the audio keeps playing when the app is
 *   in the background, so its work cannot be tied to a fragment or a view model.
 * @param profiler Profiler to log performance markers for the feature.
 * @param isAvailable Whether remote configuration has made the feature available.
 */
class ListenToPage(
    private val browserStore: BrowserStore,
    private val context: Context,
    private val scope: CoroutineScope,
    private val profiler: Profiler?,
    private val isAvailable: Boolean,
) {

    val settings by lazy { ListenSettings.dataStore(context = context, available = isAvailable, scope = scope) }

    val store by lazy {
        val performanceMiddleware = profiler?.let { ListenPerformanceMiddleware(profiler = it) }?.also { it.register() }

        ListenStore(
            initialState = ListenState(),
            reducer = ::listenReducer,
            middleware =
                listOfNotNull(
                    performanceMiddleware,
                    ListenMiddleware(
                        browserStore = browserStore,
                        contentProvider =
                            ContentProvider.fromPage(
                                pageContentExtractor = pageExtractor,
                                pageMetadataExtractor = pageExtractor,
                                fallbackLanguageTag = {
                                    (LocaleManager.getCurrentLocale(context) ?: LocaleManager.getSystemDefault())
                                        .toLanguageTag()
                                },
                            ),
                        // A provider, not an instance: building one binds the speech engine over IPC, and the
                        // middleware closes it when a session stops, so it needs a way to get another.
                        synthesizerProvider = {
                            SpeechSynthesizer.android(context = context, audioCache = audioCache)
                        },
                        audioCache = audioCache,
                        playbackController =
                            ListenPlaybackController(
                                context = context,
                                scope = scope,
                                sessionActivity = { tabId -> context.openTabPendingIntent(tabId) },
                            ),
                        settings = settings,
                        scope = scope,
                    ),
                    ListenToPageTelemetryMiddleware(),
                    thermalStatusMonitor()?.let { ListenThermalMiddleware(monitor = it) },
                ),
        )
    }

    private fun thermalStatusMonitor(): ThermalStatusMonitor? {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return null
        val powerManager = context.getSystemService<PowerManager>() ?: return null
        return PowerManagerThermalStatusMonitor(powerManager, ContextCompat.getMainExecutor(context))
    }

    // One cache for the whole feature: the synthesizer writes the audio into it and the middleware empties it when the
    // session stops. Two instances would each hold their own directory and neither would clean up after the other.
    private val audioCache by lazy { DirectoryAudioFileCache(context) }

    private val pageExtractor by lazy {
        FenixListenPageExtractor { tabId -> browserStore.state.findTab(tabId)?.engineState?.engineSession }
    }
}
