/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.state

import java.util.UUID
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSessionState
import mozilla.components.concept.engine.manifest.WebAppManifest
import mozilla.components.concept.storage.HistoryMetadataKey

/**
 * Value type that represents the state of a tab (private or normal).
 *
 * @property id the ID of this tab and session.
 * @property content the [ContentState] of this tab.
 * @property trackingProtection the [TrackingProtectionState] of this tab.
 * @property translationsState the [TranslationsState] of this tab.
 * @property parentId the parent ID of this tab or null if this tab has no parent. The parent tab is usually the tab
 *   that initiated opening this tab (e.g. the user clicked a link with target="_blank" or selected "open in new tab" or
 *   a "window.open" was triggered).
 * @property extensionState a map of web extension ids to extensions, that contains the overridden values for this tab.
 * @property readerState the [ReaderState] of this tab.
 * @property contextId the session context ID of this tab.
 * @property lastAccess The last time this tab was selected (requires LastAccessMiddleware).
 * @property lastVisibleAt The last time this tab was visible to the user, i.e. when it was last rendered and then
 *   hidden — either because the user switched to another tab or the app was backgrounded. Stamped by
 *   [EngineViewPresenter] when it releases the engine view.
 * @property createdAt Timestamp of this tab's creation.
 * @property lastMediaAccessState - [LastMediaAccessState] detailing the tab state when media started playing. Requires
 *   [LastMediaAccessMiddleware] to update the value when playback starts.
 * @property restored Indicates if this page was restored from a persisted state.
 * @property originalInput If the user entered a URL, this is the original user input before any fixups were applied to
 *   it.
 * @property listenToPageActive Whether this tab is currently being read out loud.
 */
data class TabSessionState(
    override val id: String = UUID.randomUUID().toString(),
    override val content: ContentState,
    override val trackingProtection: TrackingProtectionState = TrackingProtectionState(),
    override val translationsState: TranslationsState = TranslationsState(),
    override val engineState: EngineState = EngineState(),
    override val extensionState: Map<String, WebExtensionState> = emptyMap(),
    override val mediaSessionState: MediaSessionState? = null,
    override val contextId: String? = null,
    override val source: SessionState.Source = SessionState.Source.Internal.None,
    override val restored: Boolean = false,
    override val originalInput: String? = null,
    val parentId: String? = null,
    val lastAccess: Long = 0L,
    val lastVisibleAt: Long = 0L,
    val createdAt: Long = System.currentTimeMillis(),
    val lastMediaAccessState: LastMediaAccessState = LastMediaAccessState(),
    val readerState: ReaderState = ReaderState(),
    val historyMetadata: HistoryMetadataKey? = null,
    val listenToPageActive: Boolean = false,
) : SessionState {

    override fun createCopy(
        id: String,
        content: ContentState,
        trackingProtection: TrackingProtectionState,
        translationsState: TranslationsState,
        engineState: EngineState,
        extensionState: Map<String, WebExtensionState>,
        mediaSessionState: MediaSessionState?,
        contextId: String?,
    ): SessionState =
        copy(
            id = id,
            content = content,
            trackingProtection = trackingProtection,
            translationsState = translationsState,
            engineState = engineState,
            extensionState = extensionState,
            mediaSessionState = mediaSessionState,
            contextId = contextId,
        )
}

/** Convenient function for creating a tab. */
fun createTab(
    url: String,
    private: Boolean = false,
    id: String = UUID.randomUUID().toString(),
    parent: TabSessionState? = null,
    parentId: String? = null,
    extensions: Map<String, WebExtensionState> = emptyMap(),
    readerState: ReaderState = ReaderState(),
    title: String = "",
    contextId: String? = null,
    lastAccess: Long = 0L,
    lastVisibleAt: Long = 0L,
    createdAt: Long = System.currentTimeMillis(),
    lastMediaAccessState: LastMediaAccessState = LastMediaAccessState(),
    source: SessionState.Source = SessionState.Source.Internal.None,
    restored: Boolean = false,
    engineSession: EngineSession? = null,
    engineSessionState: EngineSessionState? = null,
    crashed: Boolean = false,
    mediaSessionState: MediaSessionState? = null,
    historyMetadata: HistoryMetadataKey? = null,
    webAppManifest: WebAppManifest? = null,
    searchTerms: String = "",
    initialLoadFlags: EngineSession.LoadUrlFlags = EngineSession.LoadUrlFlags.none(),
    initialAdditionalHeaders: Map<String, String>? = null,
    desktopMode: Boolean = false,
    previewImageUrl: String? = null,
    hasFormData: Boolean = false,
    originalInput: String? = null,
    initialTextDirectiveUserActivation: Boolean = false,
    trackingProtection: TrackingProtectionState = TrackingProtectionState(),
    securityInfo: SecurityInfo = SecurityInfo.Unknown,
    listenToPageActive: Boolean = false,
): TabSessionState {
    return TabSessionState(
        id = id,
        content =
            ContentState(
                url,
                private,
                title = title,
                securityInfo = securityInfo,
                webAppManifest = webAppManifest,
                searchTerms = searchTerms,
                desktopMode = desktopMode,
                previewImageUrl = previewImageUrl,
                hasFormData = hasFormData,
            ),
        parentId = parentId ?: parent?.id,
        extensionState = extensions,
        readerState = readerState,
        contextId = contextId,
        lastAccess = lastAccess,
        lastVisibleAt = lastVisibleAt,
        createdAt = createdAt,
        lastMediaAccessState = lastMediaAccessState,
        source = source,
        restored = restored,
        engineState =
            EngineState(
                engineSession = engineSession,
                engineSessionState = engineSessionState,
                crashed = crashed,
                initialLoadFlags = initialLoadFlags,
                initialAdditionalHeaders = initialAdditionalHeaders,
                initialTextDirectiveUserActivation = initialTextDirectiveUserActivation,
            ),
        mediaSessionState = mediaSessionState,
        historyMetadata = historyMetadata,
        originalInput = originalInput,
        trackingProtection = trackingProtection,
        listenToPageActive = listenToPageActive,
    )
}

/** Indicates if the specified tab should be considered "inactive" */
fun TabSessionState.isActive(maxActiveTime: Long, now: Long = System.currentTimeMillis()): Boolean {
    val lastActiveTime = maxOf(lastAccess, createdAt)
    return (now - lastActiveTime <= maxActiveTime)
}
