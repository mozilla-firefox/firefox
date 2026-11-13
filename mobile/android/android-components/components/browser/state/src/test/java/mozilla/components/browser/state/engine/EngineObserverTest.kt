/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.engine

import android.content.Intent
import android.view.WindowManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertNotNull
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.CrashAction
import mozilla.components.browser.state.action.PictureInPictureAction
import mozilla.components.browser.state.action.ReaderAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.action.TrackingProtectionAction
import mozilla.components.browser.state.action.TranslationsAction
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.MediaSessionState
import mozilla.components.browser.state.state.SecurityInfo
import mozilla.components.browser.state.state.content.FindResultState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSessionState
import mozilla.components.concept.engine.HitResult
import mozilla.components.concept.engine.Settings
import mozilla.components.concept.engine.content.blocking.Tracker
import mozilla.components.concept.engine.history.HistoryItem
import mozilla.components.concept.engine.manifest.WebAppManifest
import mozilla.components.concept.engine.mediasession.MediaSession
import mozilla.components.concept.engine.permission.PermissionRequest
import mozilla.components.concept.engine.prompt.PromptRequest
import mozilla.components.concept.engine.translate.TranslationError
import mozilla.components.concept.engine.translate.TranslationOperation
import mozilla.components.concept.engine.translate.TranslationOptions
import mozilla.components.concept.engine.window.WindowRequest
import mozilla.components.concept.fetch.Header
import mozilla.components.concept.fetch.Headers.Names.E_TAG
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Response
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.mock
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.`when`

@RunWith(AndroidJUnit4::class)
class EngineObserverTest {
    // TO DO: add tests for product URL after a test endpoint is implemented in desktop (Bug 1846341)
    @Test
    fun engineSessionObserver() = runTest {
        val engineSession =
            object : EngineSession() {
                override val settings: Settings = mock()

                override fun goBack(userInteraction: Boolean) {}

                override fun goForward(userInteraction: Boolean) {}

                override fun goToHistoryIndex(index: Int) {}

                override fun reload(flags: LoadUrlFlags) {}

                override fun stopLoading() {}

                override fun restoreState(state: EngineSessionState): Boolean {
                    return false
                }

                override fun flushSessionState() {}

                override fun updateTrackingProtection(policy: TrackingProtectionPolicy) {}

                override fun toggleDesktopMode(enable: Boolean, reload: Boolean) {
                    notifyObservers { onDesktopModeChange(enable) }
                }

                override fun checkForPdfViewer(
                    onResult: (Boolean) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun addSignatureToPdf(
                    text: String,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun getBrokenSiteReport(
                    onResult: (JSONObject) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun sendGleanBrokenSiteReport(
                    details: JSONObject?,
                    description: String?,
                    reason: String,
                    url: String,
                    sendTabSpecificInfo: Boolean,
                    sendBlockedUrls: Boolean,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun getWebCompatInfo(
                    onResult: (JSONObject) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun sendMoreWebCompatInfo(
                    info: JSONObject,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun requestTranslate(
                    fromLanguage: String,
                    toLanguage: String,
                    options: TranslationOptions?,
                ) {}

                override fun requestTranslationRestore() {}

                override fun getNeverTranslateSiteSetting(
                    onResult: (Boolean) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun setNeverTranslateSiteSetting(
                    setting: Boolean,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun findAll(text: String) {}

                override fun findNext(forward: Boolean) {}

                override fun clearFindMatches() {}

                override fun exitFullScreenMode() {}

                override fun purgeHistory() {}

                override fun loadData(data: String, mimeType: String, encoding: String) {
                    notifyObservers { onLocationChange(data, false) }
                    notifyObservers { onProgress(100) }
                    notifyObservers { onLoadingStateChange(true) }
                    notifyObservers { onNavigationStateChange(true, true) }
                }

                override fun requestPdfToDownload() = Unit

                override fun requestPrintContent() = Unit

                override fun processBackPressed(onResult: (Boolean) -> Unit) {}

                override fun loadUrl(
                    url: String,
                    parent: EngineSession?,
                    flags: LoadUrlFlags,
                    additionalHeaders: Map<String, String>?,
                    originalInput: String?,
                    textDirectiveUserActivation: Boolean,
                ) {
                    notifyObservers { onLocationChange(url, false) }
                    notifyObservers { onProgress(100) }
                    notifyObservers { onLoadingStateChange(true) }
                    notifyObservers { onNavigationStateChange(true, true) }
                }
            }

        val store = BrowserStore()
        store.dispatch(TabListAction.AddTabAction(createTab("https://www.mozilla.org", id = "mozilla")))

        engineSession.register(createEngineObserver(store = store, scope = this))
        engineSession.loadUrl("http://mozilla.org")
        engineSession.toggleDesktopMode(true)
        testScheduler.advanceUntilIdle()

        assertEquals("http://mozilla.org", store.state.selectedTab?.content?.url)
        assertEquals(100, store.state.selectedTab?.content?.progress)
        assertEquals(true, store.state.selectedTab?.content?.loading)

        val tab = store.state.findTab("mozilla")
        assertNotNull(tab)
        assertTrue(tab.content.canGoForward)
        assertTrue(tab.content.canGoBack)
    }

    @Test
    fun engineSessionObserverWithSecurityChanges() = runTest {
        val engineSession =
            object : EngineSession() {
                override val settings: Settings = mock()

                override fun goBack(userInteraction: Boolean) {}

                override fun goForward(userInteraction: Boolean) {}

                override fun goToHistoryIndex(index: Int) {}

                override fun stopLoading() {}

                override fun reload(flags: LoadUrlFlags) {}

                override fun restoreState(state: EngineSessionState): Boolean {
                    return false
                }

                override fun flushSessionState() {}

                override fun updateTrackingProtection(policy: TrackingProtectionPolicy) {}

                override fun toggleDesktopMode(enable: Boolean, reload: Boolean) {}

                override fun checkForPdfViewer(
                    onResult: (Boolean) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun addSignatureToPdf(
                    text: String,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun getBrokenSiteReport(
                    onResult: (JSONObject) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun sendGleanBrokenSiteReport(
                    details: JSONObject?,
                    description: String?,
                    reason: String,
                    url: String,
                    sendTabSpecificInfo: Boolean,
                    sendBlockedUrls: Boolean,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun getWebCompatInfo(
                    onResult: (JSONObject) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun sendMoreWebCompatInfo(
                    info: JSONObject,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun requestTranslate(
                    fromLanguage: String,
                    toLanguage: String,
                    options: TranslationOptions?,
                ) {}

                override fun requestTranslationRestore() {}

                override fun getNeverTranslateSiteSetting(
                    onResult: (Boolean) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun setNeverTranslateSiteSetting(
                    setting: Boolean,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun findAll(text: String) {}

                override fun findNext(forward: Boolean) {}

                override fun clearFindMatches() {}

                override fun exitFullScreenMode() {}

                override fun purgeHistory() {}

                override fun loadData(data: String, mimeType: String, encoding: String) {}

                override fun requestPdfToDownload() = Unit

                override fun requestPrintContent() = Unit

                override fun processBackPressed(onResult: (Boolean) -> Unit) {}

                override fun loadUrl(
                    url: String,
                    parent: EngineSession?,
                    flags: LoadUrlFlags,
                    additionalHeaders: Map<String, String>?,
                    originalInput: String?,
                    textDirectiveUserActivation: Boolean,
                ) {
                    if (url.startsWith("https://")) {
                        notifyObservers { onSecurityChange(true, "host", "issuer", null) }
                    } else {
                        notifyObservers { onSecurityChange(false) }
                    }
                }
            }

        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla")))
            )

        engineSession.register(createEngineObserver(store = store, scope = this))

        engineSession.loadUrl("http://mozilla.org")
        testScheduler.advanceUntilIdle()

        assertEquals(SecurityInfo.Insecure(), store.state.tabs[0].content.securityInfo)

        engineSession.loadUrl("https://mozilla.org")
        testScheduler.advanceUntilIdle()

        assertEquals(SecurityInfo.Secure("host", "issuer"), store.state.tabs[0].content.securityInfo)
    }

    @Test
    fun engineSessionObserverWithTrackingProtection() = runTest {
        val engineSession =
            object : EngineSession() {
                override val settings: Settings = mock()

                override fun goBack(userInteraction: Boolean) {}

                override fun goForward(userInteraction: Boolean) {}

                override fun goToHistoryIndex(index: Int) {}

                override fun stopLoading() {}

                override fun reload(flags: LoadUrlFlags) {}

                override fun restoreState(state: EngineSessionState): Boolean {
                    return false
                }

                override fun flushSessionState() {}

                override fun updateTrackingProtection(policy: TrackingProtectionPolicy) {}

                override fun toggleDesktopMode(enable: Boolean, reload: Boolean) {}

                override fun checkForPdfViewer(
                    onResult: (Boolean) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun addSignatureToPdf(
                    text: String,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun getBrokenSiteReport(
                    onResult: (JSONObject) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun sendGleanBrokenSiteReport(
                    details: JSONObject?,
                    description: String?,
                    reason: String,
                    url: String,
                    sendTabSpecificInfo: Boolean,
                    sendBlockedUrls: Boolean,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun getWebCompatInfo(
                    onResult: (JSONObject) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun sendMoreWebCompatInfo(
                    info: JSONObject,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun requestTranslate(
                    fromLanguage: String,
                    toLanguage: String,
                    options: TranslationOptions?,
                ) {}

                override fun requestTranslationRestore() {}

                override fun getNeverTranslateSiteSetting(
                    onResult: (Boolean) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun setNeverTranslateSiteSetting(
                    setting: Boolean,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun loadUrl(
                    url: String,
                    parent: EngineSession?,
                    flags: LoadUrlFlags,
                    additionalHeaders: Map<String, String>?,
                    originalInput: String?,
                    textDirectiveUserActivation: Boolean,
                ) {}

                override fun loadData(data: String, mimeType: String, encoding: String) {}

                override fun requestPdfToDownload() = Unit

                override fun requestPrintContent() = Unit

                override fun findAll(text: String) {}

                override fun findNext(forward: Boolean) {}

                override fun clearFindMatches() {}

                override fun exitFullScreenMode() {}

                override fun purgeHistory() {}

                override fun processBackPressed(onResult: (Boolean) -> Unit) {}
            }
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla")))
            )
        val observer = createEngineObserver(store = store, scope = this)
        engineSession.register(observer)

        val tracker1 = Tracker("tracker1", emptyList())
        val tracker2 = Tracker("tracker2", emptyList())

        observer.onTrackerBlocked(tracker1)
        testScheduler.advanceUntilIdle()

        assertEquals(listOf(tracker1), store.state.tabs[0].trackingProtection.blockedTrackers)

        observer.onTrackerBlocked(tracker2)
        testScheduler.advanceUntilIdle()

        assertEquals(listOf(tracker1, tracker2), store.state.tabs[0].trackingProtection.blockedTrackers)
    }

    @Test
    fun `WHEN the first page load is complete, set the translations initialized`() = runTest {
        val engineSession =
            object : EngineSession() {
                override val settings: Settings = mock()

                override fun goBack(userInteraction: Boolean) {}

                override fun goForward(userInteraction: Boolean) {}

                override fun goToHistoryIndex(index: Int) {}

                override fun stopLoading() {}

                override fun reload(flags: LoadUrlFlags) {}

                override fun restoreState(state: EngineSessionState): Boolean {
                    return false
                }

                override fun flushSessionState() {}

                override fun updateTrackingProtection(policy: TrackingProtectionPolicy) {}

                override fun toggleDesktopMode(enable: Boolean, reload: Boolean) {}

                override fun checkForPdfViewer(
                    onResult: (Boolean) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun addSignatureToPdf(
                    text: String,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun getBrokenSiteReport(
                    onResult: (JSONObject) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun sendGleanBrokenSiteReport(
                    details: JSONObject?,
                    description: String?,
                    reason: String,
                    url: String,
                    sendTabSpecificInfo: Boolean,
                    sendBlockedUrls: Boolean,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun getWebCompatInfo(
                    onResult: (JSONObject) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun sendMoreWebCompatInfo(
                    info: JSONObject,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun requestTranslate(
                    fromLanguage: String,
                    toLanguage: String,
                    options: TranslationOptions?,
                ) {}

                override fun requestTranslationRestore() {}

                override fun getNeverTranslateSiteSetting(
                    onResult: (Boolean) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun setNeverTranslateSiteSetting(
                    setting: Boolean,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun findAll(text: String) {}

                override fun findNext(forward: Boolean) {}

                override fun clearFindMatches() {}

                override fun exitFullScreenMode() {}

                override fun purgeHistory() {}

                override fun loadData(data: String, mimeType: String, encoding: String) {}

                override fun requestPdfToDownload() = Unit

                override fun requestPrintContent() = Unit

                override fun processBackPressed(onResult: (Boolean) -> Unit) {}

                override fun loadUrl(
                    url: String,
                    parent: EngineSession?,
                    flags: LoadUrlFlags,
                    additionalHeaders: Map<String, String>?,
                    originalInput: String?,
                    textDirectiveUserActivation: Boolean,
                ) {
                    notifyObservers { onProgress(100) }
                }
            }

        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla")))
            )

        assertEquals(false, store.state.translationsInitialized)

        engineSession.register(createEngineObserver(store = store, scope = this))

        engineSession.loadUrl("https://mozilla.org")

        testScheduler.advanceUntilIdle()

        assertEquals(true, store.state.translationsInitialized)
    }

    @Test
    fun `WHEN the first page load is not complete, do not set the translations initialized`() = runTest {
        val engineSession =
            object : EngineSession() {
                override val settings: Settings = mock()

                override fun goBack(userInteraction: Boolean) {}

                override fun goForward(userInteraction: Boolean) {}

                override fun goToHistoryIndex(index: Int) {}

                override fun stopLoading() {}

                override fun reload(flags: LoadUrlFlags) {}

                override fun restoreState(state: EngineSessionState): Boolean {
                    return false
                }

                override fun flushSessionState() {}

                override fun updateTrackingProtection(policy: TrackingProtectionPolicy) {}

                override fun toggleDesktopMode(enable: Boolean, reload: Boolean) {}

                override fun checkForPdfViewer(
                    onResult: (Boolean) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun addSignatureToPdf(
                    text: String,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun getBrokenSiteReport(
                    onResult: (JSONObject) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun sendGleanBrokenSiteReport(
                    details: JSONObject?,
                    description: String?,
                    reason: String,
                    url: String,
                    sendTabSpecificInfo: Boolean,
                    sendBlockedUrls: Boolean,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun getWebCompatInfo(
                    onResult: (JSONObject) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun sendMoreWebCompatInfo(
                    info: JSONObject,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun requestTranslate(
                    fromLanguage: String,
                    toLanguage: String,
                    options: TranslationOptions?,
                ) {}

                override fun requestTranslationRestore() {}

                override fun getNeverTranslateSiteSetting(
                    onResult: (Boolean) -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun setNeverTranslateSiteSetting(
                    setting: Boolean,
                    onResult: () -> Unit,
                    onException: (Throwable) -> Unit,
                ) {}

                override fun findAll(text: String) {}

                override fun findNext(forward: Boolean) {}

                override fun clearFindMatches() {}

                override fun exitFullScreenMode() {}

                override fun purgeHistory() {}

                override fun loadData(data: String, mimeType: String, encoding: String) {}

                override fun requestPdfToDownload() = Unit

                override fun requestPrintContent() = Unit

                override fun processBackPressed(onResult: (Boolean) -> Unit) {}

                override fun loadUrl(
                    url: String,
                    parent: EngineSession?,
                    flags: LoadUrlFlags,
                    additionalHeaders: Map<String, String>?,
                    originalInput: String?,
                    textDirectiveUserActivation: Boolean,
                ) {
                    notifyObservers { onProgress(80) }
                }
            }

        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla")))
            )

        assertEquals(false, store.state.translationsInitialized)

        engineSession.register(createEngineObserver(store = store, scope = this))

        engineSession.loadUrl("https://mozilla.org")

        assertEquals(false, store.state.translationsInitialized)
    }

    @Test
    fun `Do not initialize the translations flow if the page load is not complete`() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val state: BrowserState = mock()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        `when`(state.translationsInitialized).thenReturn(false)

        val observer = createEngineObserver(store = store, scope = this)

        observer.onProgress(80)

        captureActionsMiddleware.assertNotDispatched(TranslationsAction.InitTranslationsBrowserState::class)
    }

    @Test
    fun `Initialize the translations flow if page load is complete and it is not yet initialized`() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val state = BrowserState(translationsInitialized = false)
        val store = BrowserStore(state, middleware = listOf(captureActionsMiddleware))

        val observer = createEngineObserver(store = store, scope = this)

        observer.onProgress(PAGE_LOAD_COMPLETION_PROGRESS)
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.findFirstAction(TranslationsAction.InitTranslationsBrowserState::class)
    }

    @Test
    fun `Do not initialize the translations flow if it is already initialized`() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val state: BrowserState = mock()
        val store = BrowserStore(state, middleware = listOf(captureActionsMiddleware))
        `when`(state.translationsInitialized).thenReturn(true)

        val observer = createEngineObserver(store = store, scope = this)

        observer.onProgress(100)

        captureActionsMiddleware.assertNotDispatched(TranslationsAction.InitTranslationsBrowserState::class)
    }

    @Test
    fun engineSessionObserverExcludedOnTrackingProtection() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer = createEngineObserver(store = store, scope = this)

        observer.onExcludedOnTrackingProtectionChange(true)
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(TrackingProtectionAction.ToggleExclusionListAction::class) { action
            ->
            assertEquals("mozilla", action.tabId)
            assertTrue(action.excluded)
        }
    }

    @Test
    fun `WHEN onTranslatePageChange is called THEN dispatch a TranslationsAction SetTranslateProcessingAction`() =
        runTest {
            val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
            val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
            val observer = createEngineObserver(store = store, scope = this)

            observer.onTranslatePageChange()
            testScheduler.advanceUntilIdle()

            captureActionsMiddleware.assertFirstAction(TranslationsAction.SetTranslateProcessingAction::class) { action
                ->
                assertEquals("mozilla", action.tabId)
                assertFalse(action.isProcessing)
            }
        }

    @Test
    fun `WHEN onTranslateComplete is called THEN dispatch a TranslationsAction TranslateSuccessAction`() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer = createEngineObserver(store = store, scope = this)

        observer.onTranslateComplete(operation = TranslationOperation.TRANSLATE)
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(TranslationsAction.TranslateSuccessAction::class) { action ->
            assertEquals("mozilla", action.tabId)
            assertEquals(TranslationOperation.TRANSLATE, action.operation)
        }
    }

    @Test
    fun `WHEN onTranslateException is called THEN dispatch a TranslationsAction TranslateExceptionAction`() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer = createEngineObserver(store = store, scope = this)
        val exception = TranslationError.UnknownError(Exception())

        observer.onTranslateException(operation = TranslationOperation.TRANSLATE, exception)
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(TranslationsAction.TranslateExceptionAction::class) { action ->
            assertEquals("mozilla", action.tabId)
            assertEquals(exception, action.translationError)
            assertEquals(TranslationOperation.TRANSLATE, action.operation)
        }
    }

    @Test
    fun engineObserverClearsWebsiteTitleIfNewPageStartsLoading() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                    title = "Hello World",
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)
        observer.onTitleChange("Mozilla")
        testScheduler.advanceUntilIdle()

        assertEquals("Mozilla", store.state.tabs[0].content.title)

        observer.onLocationChange("https://getpocket.com", false)
        testScheduler.advanceUntilIdle()

        assertEquals("", store.state.tabs[0].content.title)
    }

    @Test
    fun `EngineObserver does not clear title if the URL did not change`() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                    title = "Hello World",
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)

        observer.onTitleChange("Mozilla")
        testScheduler.advanceUntilIdle()

        assertEquals("Mozilla", store.state.tabs[0].content.title)

        observer.onLocationChange("https://www.mozilla.org", false)
        testScheduler.advanceUntilIdle()

        assertEquals("Mozilla", store.state.tabs[0].content.title)
    }

    @Test
    fun `EngineObserver does not clear title if the URL changes hash`() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                    title = "Hello World",
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)

        observer.onTitleChange("Mozilla")
        testScheduler.advanceUntilIdle()

        assertEquals("Mozilla", store.state.tabs[0].content.title)

        observer.onLocationChange("https://www.mozilla.org/#something", false)
        testScheduler.advanceUntilIdle()

        assertEquals("Mozilla", store.state.tabs[0].content.title)
    }

    @Test
    fun `EngineObserver clears previewImageUrl if new page starts loading`() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                    title = "Hello World",
                                )
                            )
                    )
            )
        val previewImageUrl = "https://test.com/og-image-url"

        val observer = createEngineObserver(store = store, scope = this)
        observer.onPreviewImageChange(previewImageUrl)
        testScheduler.advanceUntilIdle()

        assertEquals(previewImageUrl, store.state.tabs[0].content.previewImageUrl)

        observer.onLocationChange("https://getpocket.com", false)
        testScheduler.advanceUntilIdle()

        assertNull(store.state.tabs[0].content.previewImageUrl)
    }

    @Test
    fun `EngineObserver does not clear previewImageUrl if the URL did not change`() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                    title = "Hello World",
                                )
                            )
                    )
            )
        val previewImageUrl = "https://test.com/og-image-url"

        val observer = createEngineObserver(store = store, scope = this)

        observer.onPreviewImageChange(previewImageUrl)
        testScheduler.advanceUntilIdle()

        assertEquals(previewImageUrl, store.state.tabs[0].content.previewImageUrl)

        observer.onLocationChange("https://www.mozilla.org", false)
        testScheduler.advanceUntilIdle()

        assertEquals(previewImageUrl, store.state.tabs[0].content.previewImageUrl)

        observer.onLocationChange("https://www.mozilla.org/#something", false)
        testScheduler.advanceUntilIdle()

        assertEquals(previewImageUrl, store.state.tabs[0].content.previewImageUrl)
    }

    @Test
    fun engineObserverClearsBlockedTrackersIfNewPageStartsLoading() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)

        val tracker1 = Tracker("tracker1")
        val tracker2 = Tracker("tracker2")

        observer.onTrackerBlocked(tracker1)
        observer.onTrackerBlocked(tracker2)
        testScheduler.advanceUntilIdle()

        assertEquals(listOf(tracker1, tracker2), store.state.tabs[0].trackingProtection.blockedTrackers)

        observer.onLoadingStateChange(true)
        testScheduler.advanceUntilIdle()

        assertEquals(emptyList<String>(), store.state.tabs[0].trackingProtection.blockedTrackers)
    }

    @Test
    fun engineObserverClearsLoadedTrackersIfNewPageStartsLoading() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)

        val tracker1 = Tracker("tracker1")
        val tracker2 = Tracker("tracker2")

        observer.onTrackerLoaded(tracker1)
        observer.onTrackerLoaded(tracker2)
        testScheduler.advanceUntilIdle()

        assertEquals(listOf(tracker1, tracker2), store.state.tabs[0].trackingProtection.loadedTrackers)

        observer.onLoadingStateChange(true)
        testScheduler.advanceUntilIdle()

        assertEquals(emptyList<String>(), store.state.tabs[0].trackingProtection.loadedTrackers)
    }

    @Test
    fun engineObserverClearsWebAppManifestIfNewPageStartsLoading() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                )
                            )
                    )
            )

        val manifest = WebAppManifest(name = "Mozilla", startUrl = "https://mozilla.org")

        val observer = createEngineObserver(store = store, scope = this)

        observer.onWebAppManifestLoaded(manifest)
        testScheduler.advanceUntilIdle()

        assertEquals(manifest, store.state.tabs[0].content.webAppManifest)

        observer.onLocationChange("https://getpocket.com", false)
        testScheduler.advanceUntilIdle()

        assertNull(store.state.tabs[0].content.webAppManifest)
    }

    @Test
    fun engineObserverClearsContentPermissionRequestIfNewPageStartsLoading() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)

        val request: PermissionRequest = mock()

        store.dispatch(ContentAction.UpdatePermissionsRequest("mozilla", request))
        testScheduler.advanceUntilIdle()

        assertEquals(listOf(request), store.state.tabs[0].content.permissionRequestsList)

        observer.onLocationChange("https://getpocket.com", false)
        testScheduler.advanceUntilIdle()

        assertEquals(emptyList<PermissionRequest>(), store.state.tabs[0].content.permissionRequestsList)
    }

    @Test
    fun engineObserverDoesNotClearContentPermissionRequestIfSamePageStartsLoading() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)

        val request: PermissionRequest = mock()

        store.dispatch(ContentAction.UpdatePermissionsRequest("mozilla", request))

        assertEquals(listOf(request), store.state.tabs[0].content.permissionRequestsList)

        observer.onLocationChange("https://www.mozilla.org/hello.html", false)

        assertEquals(listOf(request), store.state.tabs[0].content.permissionRequestsList)
    }

    @Test
    fun engineObserverDoesNotClearWebAppManifestIfNewPageInStartUrlScope() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                )
                            )
                    )
            )

        val manifest = WebAppManifest(name = "Mozilla", startUrl = "https://www.mozilla.org")

        val observer = createEngineObserver(store = store, scope = this)

        observer.onWebAppManifestLoaded(manifest)
        testScheduler.advanceUntilIdle()

        assertEquals(manifest, store.state.tabs[0].content.webAppManifest)

        observer.onLocationChange("https://www.mozilla.org/hello.html", false)

        assertEquals(manifest, store.state.tabs[0].content.webAppManifest)
    }

    @Test
    fun engineObserverDoesNotClearWebAppManifestIfNewPageInScope() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                )
                            )
                    )
            )

        val manifest =
            WebAppManifest(
                name = "Mozilla",
                startUrl = "https://www.mozilla.org",
                scope = "https://www.mozilla.org/hello/",
            )

        val observer = createEngineObserver(store = store, scope = this)

        observer.onWebAppManifestLoaded(manifest)
        testScheduler.advanceUntilIdle()

        assertEquals(manifest, store.state.tabs[0].content.webAppManifest)

        observer.onLocationChange("https://www.mozilla.org/hello/page2.html", false)
        testScheduler.advanceUntilIdle()

        assertEquals(manifest, store.state.tabs[0].content.webAppManifest)

        observer.onLocationChange("https://www.mozilla.org/hello.html", false)
        testScheduler.advanceUntilIdle()

        assertNull(store.state.tabs[0].content.webAppManifest)
    }

    @Test
    fun engineObserverPassingHitResult() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)

        val hitResult = HitResult.UNKNOWN("data://foobar")

        observer.onLongPress(hitResult)
        testScheduler.advanceUntilIdle()

        assertEquals(hitResult, store.state.tabs[0].content.hitResult)
    }

    @Test
    fun engineObserverClearsFindResults() = runTest {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware))
        val observer = createEngineObserver(tabId = "tab", store = store, scope = this)

        observer.onFindResult(0, 1, false)
        testScheduler.advanceUntilIdle()
        middleware.assertFirstAction(ContentAction.AddFindResultAction::class) { action ->
            assertEquals("tab", action.sessionId)
            assertEquals(FindResultState(0, 1, false), action.findResult)
        }

        observer.onFind("mozilla")
        testScheduler.advanceUntilIdle()
        middleware.assertLastAction(ContentAction.ClearFindResultsAction::class) { action ->
            assertEquals("tab", action.sessionId)
        }
    }

    @Test
    fun engineObserverClearsFindResultIfNewPageStartsLoading() = runTest {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware))
        val observer = createEngineObserver(tabId = "tab-id", store = store, scope = this)

        observer.onFindResult(0, 1, false)
        testScheduler.advanceUntilIdle()
        middleware.assertFirstAction(ContentAction.AddFindResultAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertEquals(FindResultState(0, 1, false), action.findResult)
        }

        observer.onFindResult(1, 2, true)
        testScheduler.advanceUntilIdle()
        middleware.assertLastAction(ContentAction.AddFindResultAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertEquals(FindResultState(1, 2, true), action.findResult)
        }

        observer.onLoadingStateChange(true)
        testScheduler.advanceUntilIdle()
        middleware.assertLastAction(ContentAction.ClearFindResultsAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
        }
    }

    @Test
    fun engineObserverClearsRefreshCanceledIfNewPageStartsLoading() = runTest {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware))
        val observer =
            EngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )

        observer.onRepostPromptCancelled()
        testScheduler.advanceUntilIdle()
        middleware.assertFirstAction(ContentAction.UpdateRefreshCanceledStateAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertTrue(action.refreshCanceled)
        }

        observer.onLoadingStateChange(true)
        testScheduler.advanceUntilIdle()
        middleware.assertLastAction(ContentAction.UpdateRefreshCanceledStateAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertFalse(action.refreshCanceled)
        }
    }

    @Test
    fun engineObserverHandlesOnRepostPromptCancelled() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer =
            EngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )

        observer.onRepostPromptCancelled()
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(ContentAction.UpdateRefreshCanceledStateAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertTrue(action.refreshCanceled)
        }
    }

    @Test
    fun engineObserverHandlesOnBeforeUnloadDenied() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer =
            EngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )

        observer.onBeforeUnloadPromptDenied()
        testScheduler.advanceUntilIdle()
        captureActionsMiddleware.assertFirstAction(ContentAction.UpdateRefreshCanceledStateAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertTrue(action.refreshCanceled)
        }

        captureActionsMiddleware.assertFirstAction(ContentAction.UpdateRefreshCanceledStateAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertTrue(action.refreshCanceled)
        }
    }

    @Test
    fun engineObserverNotifiesFullscreenMode() = runTest {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware))
        val observer =
            EngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )

        observer.onFullScreenChange(true)
        testScheduler.advanceUntilIdle()
        middleware.assertFirstAction(ContentAction.FullScreenChangedAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertTrue(action.fullScreenEnabled)
        }

        observer.onFullScreenChange(false)
        testScheduler.advanceUntilIdle()
        middleware.assertLastAction(ContentAction.FullScreenChangedAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertFalse(action.fullScreenEnabled)
        }
    }

    @Test
    fun engineObserverNotifiesDesktopMode() = runTest {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware))
        val observer =
            EngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )

        observer.onDesktopModeChange(true)
        testScheduler.advanceUntilIdle()
        middleware.assertFirstAction(ContentAction.UpdateTabDesktopMode::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertTrue(action.enabled)
        }

        observer.onDesktopModeChange(false)
        testScheduler.advanceUntilIdle()
        middleware.assertLastAction(ContentAction.UpdateTabDesktopMode::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertFalse(action.enabled)
        }
    }

    @Test
    fun engineObserverNotifiesMetaViewportFitChange() = runTest {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware))
        val observer =
            EngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )

        observer.onMetaViewportFitChanged(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT)
        testScheduler.advanceUntilIdle()

        middleware.assertFirstAction(ContentAction.ViewportFitChangedAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertEquals(
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT,
                action.layoutInDisplayCutoutMode,
            )
        }

        observer.onMetaViewportFitChanged(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES)
        testScheduler.advanceUntilIdle()

        middleware.assertLastAction(ContentAction.ViewportFitChangedAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertEquals(
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES,
                action.layoutInDisplayCutoutMode,
            )
        }

        observer.onMetaViewportFitChanged(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER)
        testScheduler.advanceUntilIdle()

        middleware.assertLastAction(ContentAction.ViewportFitChangedAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertEquals(
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER,
                action.layoutInDisplayCutoutMode,
            )
        }

        observer.onMetaViewportFitChanged(123)
        testScheduler.advanceUntilIdle()

        middleware.assertLastAction(ContentAction.ViewportFitChangedAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertEquals(123, action.layoutInDisplayCutoutMode)
        }
    }

    @Test
    fun engineObserverNotifiesWebAppManifest() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)
        val manifest =
            WebAppManifest(
                name = "Minimal",
                startUrl = "/",
            )

        observer.onWebAppManifestLoaded(manifest)
        testScheduler.advanceUntilIdle()

        assertEquals(manifest, store.state.tabs[0].content.webAppManifest)
    }

    @Test
    fun engineSessionObserverWithContentPermissionRequests() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val permissionRequest: PermissionRequest = mock()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer =
            EngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )
        val action =
            ContentAction.UpdatePermissionsRequest(
                "tab-id",
                permissionRequest,
            )
        observer.onContentPermissionRequest(permissionRequest)
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(ContentAction.UpdatePermissionsRequest::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertEquals(permissionRequest, action.permissionRequest)
        }
    }

    @Test
    fun engineSessionObserverWithAppPermissionRequests() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val permissionRequest: PermissionRequest = mock()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer =
            EngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )
        val action =
            ContentAction.UpdateAppPermissionsRequest(
                "tab-id",
                permissionRequest,
            )

        observer.onAppPermissionRequest(permissionRequest)
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(ContentAction.UpdateAppPermissionsRequest::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertEquals(permissionRequest, action.appPermissionRequest)
        }
    }

    @Test
    fun engineObserverHandlesPromptRequest() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val promptRequest: PromptRequest = mock<PromptRequest.SingleChoice>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer =
            EngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )

        observer.onPromptRequest(promptRequest)
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(ContentAction.UpdatePromptRequestAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertEquals(promptRequest, action.promptRequest)
        }
    }

    @Test
    fun engineObserverHandlesOnPromptUpdate() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val promptRequest: PromptRequest = mock<PromptRequest.SingleChoice>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer =
            EngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )
        val previousPromptUID = "prompt-uid"

        observer.onPromptUpdate(previousPromptUID, promptRequest)
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(ContentAction.ReplacePromptRequestAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertEquals(previousPromptUID, action.previousPromptUid)
            assertEquals(promptRequest, action.promptRequest)
        }
    }

    @Test
    fun engineObserverHandlesWindowRequest() = runTest {
        val windowRequest: WindowRequest = mock()
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))

        val observer =
            EngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )

        observer.onWindowRequest(windowRequest)
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(ContentAction.UpdateWindowRequestAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertEquals(windowRequest, action.windowRequest)
        }
    }

    @Test
    fun engineObserverHandlesFirstContentfulPaint() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer =
            createEngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )

        observer.onFirstContentfulPaint()
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(ContentAction.UpdateFirstContentfulPaintStateAction::class) { action
            ->
            assertEquals("tab-id", action.sessionId)
            assertTrue(action.firstContentfulPaint)
        }
    }

    @Test
    fun engineObserverHandlesPaintStatusReset() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer =
            EngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )

        observer.onPaintStatusReset()
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(ContentAction.UpdateFirstContentfulPaintStateAction::class) { action
            ->
            assertEquals("tab-id", action.sessionId)
            assertFalse(action.firstContentfulPaint)
        }
    }

    @Test
    fun engineObserverHandlesOnShowDynamicToolbar() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer =
            EngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )

        observer.onShowDynamicToolbar()
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(ContentAction.UpdateExpandedToolbarStateAction::class) { action ->
            assertEquals("tab-id", action.sessionId)
            assertTrue(action.expanded)
        }
    }

    @Test
    fun `onMediaActivated will update the store`() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                )
                            )
                    )
            )

        val observer =
            createEngineObserver(
                tabId = "mozilla",
                store = store,
                scope = this,
            )
        val mediaSessionController: MediaSession.Controller = mock()

        assertNull(store.state.tabs[0].mediaSessionState)

        observer.onMediaActivated(mediaSessionController)
        testScheduler.advanceUntilIdle()

        val observedMediaSessionState = store.state.tabs[0].mediaSessionState
        assertNotNull(observedMediaSessionState)
        assertEquals(mediaSessionController, observedMediaSessionState.controller)
    }

    @Test
    fun `onMediaDeactivated will update the store`() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                    mediaSessionState = MediaSessionState(controller = mock()),
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)

        assertNotNull(store.state.findTab("mozilla")?.mediaSessionState)

        observer.onMediaDeactivated()
        testScheduler.advanceUntilIdle()

        val observedMediaSessionState = store.state.findTab("mozilla")?.mediaSessionState
        assertNull(observedMediaSessionState)
    }

    @Test
    fun `onMediaMetadataChanged will update the store`() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                    mediaSessionState = MediaSessionState(controller = mock()),
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)
        val mediaSessionController: MediaSession.Controller = mock()
        val metaData: MediaSession.Metadata = MediaSession.Metadata(getArtwork = { null })

        observer.onMediaActivated(mediaSessionController)
        observer.onMediaMetadataChanged(metaData)
        testScheduler.advanceUntilIdle()

        val observedMediaSessionState = store.state.findTab("mozilla")?.mediaSessionState
        assertNotNull(observedMediaSessionState)
        assertEquals(mediaSessionController, observedMediaSessionState.controller)
        assertEquals(metaData, observedMediaSessionState.metadata)
    }

    @Test
    fun `onMediaPlaybackStateChanged will update the store`() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                    mediaSessionState = MediaSessionState(controller = mock()),
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)
        val mediaSessionController: MediaSession.Controller = mock()
        val playbackState: MediaSession.PlaybackState = MediaSession.PlaybackState.PLAYING

        observer.onMediaActivated(mediaSessionController)
        observer.onMediaPlaybackStateChanged(playbackState)
        testScheduler.advanceUntilIdle()

        val observedMediaSessionState = store.state.findTab("mozilla")?.mediaSessionState
        assertNotNull(observedMediaSessionState)
        assertEquals(mediaSessionController, observedMediaSessionState.controller)
        assertEquals(playbackState, observedMediaSessionState.playbackState)
    }

    @Test
    fun `onMediaFeatureChanged will update the store`() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                    mediaSessionState = MediaSessionState(controller = mock()),
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)
        val mediaSessionController: MediaSession.Controller = mock()
        val playFeature: MediaSession.Feature = MediaSession.Feature(1L)

        observer.onMediaActivated(mediaSessionController)
        observer.onMediaFeatureChanged(playFeature)
        testScheduler.advanceUntilIdle()

        val observedMediaSessionState = store.state.findTab("mozilla")?.mediaSessionState
        assertNotNull(observedMediaSessionState)
        assertEquals(mediaSessionController, observedMediaSessionState.controller)
        assertEquals(playFeature, observedMediaSessionState.features)
    }

    @Test
    fun `onMediaPositionStateChanged will update the store`() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                    mediaSessionState = MediaSessionState(controller = mock()),
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)
        val mediaSessionController: MediaSession.Controller = mock()
        val positionState: MediaSession.PositionState = MediaSession.PositionState()

        observer.onMediaActivated(mediaSessionController)
        observer.onMediaPositionStateChanged(positionState)
        testScheduler.advanceUntilIdle()

        val observedMediaSessionState = store.state.findTab("mozilla")?.mediaSessionState
        assertNotNull(observedMediaSessionState)
        assertEquals(mediaSessionController, observedMediaSessionState.controller)
        assertEquals(positionState, observedMediaSessionState.positionState)
    }

    @Test
    fun `onMediaMuteChanged will update the store`() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                    mediaSessionState = MediaSessionState(controller = mock()),
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)
        val mediaSessionController: MediaSession.Controller = mock()

        observer.onMediaActivated(mediaSessionController)
        observer.onMediaMuteChanged(true)
        testScheduler.advanceUntilIdle()

        val observedMediaSessionState = store.state.findTab("mozilla")?.mediaSessionState
        assertNotNull(observedMediaSessionState)
        assertEquals(mediaSessionController, observedMediaSessionState.controller)
        assertEquals(true, observedMediaSessionState.muted)
    }

    @Test
    fun `onMediaFullscreenChanged will update the store`() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                    mediaSessionState = MediaSessionState(controller = mock()),
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)
        val mediaSessionController: MediaSession.Controller = mock()
        val elementMetadata: MediaSession.ElementMetadata = MediaSession.ElementMetadata()

        observer.onMediaActivated(mediaSessionController)
        observer.onMediaFullscreenChanged(true, elementMetadata)
        testScheduler.advanceUntilIdle()

        val observedMediaSessionState = store.state.findTab("mozilla")?.mediaSessionState
        assertNotNull(observedMediaSessionState)
        assertEquals(mediaSessionController, observedMediaSessionState.controller)
        assertEquals(true, observedMediaSessionState.fullscreen)
        assertEquals(elementMetadata, observedMediaSessionState.elementMetadata)
    }

    @Test
    fun `updates are ignored when media session is deactivated`() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)
        val elementMetadata: MediaSession.ElementMetadata = MediaSession.ElementMetadata()

        observer.onMediaFullscreenChanged(true, elementMetadata)

        assertNull(store.state.findTab("mozilla")?.mediaSessionState)

        observer.onMediaMuteChanged(true)
        assertNull(store.state.findTab("mozilla")?.mediaSessionState)
    }

    @Test
    fun `onExternalResource will update the store`() = runTest {
        val response =
            mock<Response> {
                `when`(headers).thenReturn(MutableHeaders(listOf(Header(E_TAG, "12345"))))
            }

        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "mozilla",
                                    mediaSessionState = MediaSessionState(controller = mock()),
                                )
                            )
                    )
            )

        val observer = createEngineObserver(store = store, scope = this)

        observer.onExternalResource(
            url = "mozilla.org/file.txt",
            fileName = "file.txt",
            userAgent = "userAgent",
            contentType = "text/plain",
            isPrivate = true,
            contentLength = 100L,
            response = response,
        )
        testScheduler.advanceUntilIdle()

        val tab = store.state.findTab("mozilla")!!

        assertEquals("mozilla.org/file.txt", tab.content.download?.url)
        assertEquals("file.txt", tab.content.download?.fileName)
        assertEquals("userAgent", tab.content.download?.userAgent)
        assertEquals("text/plain", tab.content.download?.contentType)
        assertEquals("12345", tab.content.download?.etag)
        assertEquals(100L, tab.content.download?.contentLength)
        assertEquals(true, tab.content.download?.private)
        assertEquals(response, tab.content.download?.response)
    }

    @Test
    fun `onExternalResource with negative contentLength`() = runTest {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs =
                            listOf(
                                createTab(
                                    url = "https://www.mozilla.org",
                                    id = "test-tab",
                                    mediaSessionState = MediaSessionState(controller = mock()),
                                )
                            )
                    )
            )

        val observer =
            createEngineObserver(
                tabId = "test-tab",
                store = store,
                scope = this,
            )

        observer.onExternalResource(url = "mozilla.org/file.txt", contentLength = -1)

        val tab = store.state.findTab("test-tab")!!

        assertNull(tab.content.download?.contentLength)
    }

    @Test
    fun `onCrashStateChanged will update session and notify observer`() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer =
            createEngineObserver(
                tabId = "test-id",
                store = store,
                scope = this,
            )

        observer.onCrash()
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(CrashAction.SessionCrashedAction::class) { action ->
            assertEquals("test-id", action.tabId)
        }
    }

    @Test
    fun `onLocationChange does not clear search terms`() = runTest {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware))

        val observer =
            createEngineObserver(
                tabId = "test-id",
                store = store,
                scope = this,
            )
        observer.onLocationChange("https://www.mozilla.org/en-US/", false)
        testScheduler.advanceUntilIdle()

        middleware.assertNotDispatched(ContentAction.UpdateSearchTermsAction::class)
    }

    @Test
    fun `onLoadRequest clears search terms for requests triggered by web content`() = runTest {
        val url = "https://www.mozilla.org"

        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware))

        val observer =
            createEngineObserver(
                tabId = "test-id",
                store = store,
                scope = this,
            )
        observer.onLoadRequest(url = url, triggeredByRedirect = false, triggeredByWebContent = true)
        testScheduler.advanceUntilIdle()

        middleware.assertFirstAction(ContentAction.UpdateSearchTermsAction::class) { action ->
            assertEquals("", action.searchTerms)
            assertEquals("test-id", action.sessionId)
        }
    }

    @Test
    @Suppress("DEPRECATION") // Session observable is deprecated
    fun `onLoadRequest notifies session observers`() = runTest {
        val url = "https://www.mozilla.org"
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))

        val observer =
            createEngineObserver(
                tabId = "test-id",
                store = store,
                scope = this,
            )
        observer.onLoadRequest(url = url, triggeredByRedirect = true, triggeredByWebContent = false)
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(ContentAction.UpdateLoadRequestAction::class) { action ->
            assertEquals("test-id", action.sessionId)
            assertEquals(url, action.loadRequest.url)
            assertFalse(action.loadRequest.triggeredByUser)
            assertTrue(action.loadRequest.triggeredByRedirect)
        }
    }

    @Test
    fun `onLoadRequest does not clear search terms for requests not triggered by user interacting with web content`() =
        runTest {
            val url = "https://www.mozilla.org"

            val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
            val store = BrowserStore(middleware = listOf(middleware))

            val observer =
                createEngineObserver(
                    tabId = "test-id",
                    store = store,
                    scope = this,
                )
            observer.onLoadRequest(url = url, triggeredByRedirect = false, triggeredByWebContent = false)
            testScheduler.advanceUntilIdle()

            middleware.assertNotDispatched(ContentAction.UpdateSearchTermsAction::class)
        }

    @Test
    fun `onLaunchIntentRequest dispatches UpdateAppIntentAction`() = runTest {
        val url = "https://www.mozilla.org"
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()

        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer =
            createEngineObserver(
                tabId = "test-id",
                store = store,
                scope = this,
            )
        val intent: Intent = mock()
        observer.onLaunchIntentRequest(
            url = url,
            appIntent = intent,
            fallbackUrl = null,
            appName = null,
            sourceUrl = "https://www.example.org",
        )
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(ContentAction.UpdateAppIntentAction::class) { action ->
            assertEquals("test-id", action.sessionId)
            assertEquals(url, action.appIntent.url)
            assertEquals(intent, action.appIntent.appIntent)
            assertNull(action.appIntent.appName)
            assertNull(action.appIntent.fallbackUrl)
            assertEquals("https://www.example.org", action.appIntent.sourceUrl)
        }
    }

    @Test
    fun `onNavigateBack clears search terms when navigating back`() = runTest {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware))

        val observer =
            createEngineObserver(
                tabId = "test-id",
                store = store,
                scope = this,
            )
        observer.onNavigateBack()
        testScheduler.advanceUntilIdle()

        middleware.assertFirstAction(ContentAction.UpdateSearchTermsAction::class) { action ->
            assertEquals("", action.searchTerms)
            assertEquals("test-id", action.sessionId)
        }
    }

    @Test
    fun `WHEN navigating forward THEN search terms are cleared`() = runTest {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware))

        val observer =
            createEngineObserver(
                tabId = "test-id",
                store = store,
                scope = this,
            )
        observer.onNavigateForward()
        testScheduler.advanceUntilIdle()

        middleware.assertFirstAction(ContentAction.UpdateSearchTermsAction::class) { action ->
            assertEquals("", action.searchTerms)
            assertEquals("test-id", action.sessionId)
        }
    }

    @Test
    fun `WHEN navigating to history index THEN search terms are cleared`() = runTest {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware))

        val observer =
            createEngineObserver(
                tabId = "test-id",
                store = store,
                scope = this,
            )
        observer.onGotoHistoryIndex()
        testScheduler.advanceUntilIdle()

        middleware.assertFirstAction(ContentAction.UpdateSearchTermsAction::class) { action ->
            assertEquals("", action.searchTerms)
            assertEquals("test-id", action.sessionId)
        }
    }

    @Test
    fun `WHEN loading data THEN the search terms are cleared`() = runTest {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(middleware))

        val observer =
            createEngineObserver(
                tabId = "test-id",
                store = store,
                scope = this,
            )
        observer.onLoadData()
        testScheduler.advanceUntilIdle()

        middleware.assertFirstAction(ContentAction.UpdateSearchTermsAction::class) { action ->
            assertEquals("", action.searchTerms)
            assertEquals("test-id", action.sessionId)
        }
    }

    @Test
    fun `GIVEN a search is not performed WHEN loading the URL THEN the search terms are cleared`() = runTest {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla"))),
                middleware = listOf(middleware),
            )

        store.dispatch(ContentAction.UpdateIsSearchAction("mozilla", false))

        val observer =
            createEngineObserver(
                tabId = "test-id",
                store = store,
                scope = this,
            )
        observer.onLoadUrl()
        testScheduler.advanceUntilIdle()

        middleware.assertLastAction(ContentAction.UpdateSearchTermsAction::class) { action ->
            assertEquals("", action.searchTerms)
            assertEquals("test-id", action.sessionId)
        }
    }

    @Test
    fun `GIVEN a search is performed WHEN loading the URL THEN the search terms are cleared`() = runTest {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "test-id"))),
                middleware = listOf(middleware),
            )

        store.dispatch(ContentAction.UpdateIsSearchAction("test-id", true))

        val observer =
            createEngineObserver(
                tabId = "test-id",
                store = store,
                scope = this,
            )
        observer.onLoadUrl()
        testScheduler.advanceUntilIdle()

        middleware.assertLastAction(ContentAction.UpdateIsSearchAction::class) { action ->
            assertEquals(false, action.isSearch)
            assertEquals("test-id", action.sessionId)
        }
    }

    @Test
    fun `GIVEN a search is performed WHEN the location is changed without user interaction THEN the search terms are not cleared`() =
        runTest {
            val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
            val store =
                BrowserStore(
                    initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", id = "test-id"))),
                    middleware = listOf(middleware),
                )

            store.dispatch(ContentAction.UpdateIsSearchAction("test-id", true))

            val observer =
                createEngineObserver(
                    tabId = "test-id",
                    store = store,
                    scope = this,
                )
            observer.onLocationChange("testUrl", false)
            testScheduler.advanceUntilIdle()

            middleware.assertNotDispatched(ContentAction.UpdateSearchTermsAction::class)
        }

    @Test
    fun `onHistoryStateChanged dispatches UpdateHistoryStateAction`() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer =
            createEngineObserver(
                tabId = "test-id",
                store = store,
                scope = this,
            )

        observer.onHistoryStateChanged(emptyList(), 0)
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(ContentAction.UpdateHistoryStateAction::class) { action ->
            assertEquals("test-id", action.sessionId)
            assertTrue(action.historyList.isEmpty())
            assertEquals(0, action.currentIndex)
        }

        observer.onHistoryStateChanged(
            listOf(
                HistoryItem("Firefox", "https://firefox.com"),
                HistoryItem("Mozilla", "http://mozilla.org"),
            ),
            1,
        )
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertLastAction(ContentAction.UpdateHistoryStateAction::class) { action ->
            assertEquals("test-id", action.sessionId)
            assertEquals(1, action.currentIndex)
            assertEquals(
                listOf(
                    HistoryItem("Firefox", "https://firefox.com"),
                    HistoryItem("Mozilla", "http://mozilla.org"),
                ),
                action.historyList,
            )
        }
    }

    @Test
    fun `onScrollChange dispatches UpdateReaderScrollYAction`() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer =
            createEngineObserver(
                tabId = "tab-id",
                store = store,
                scope = this,
            )

        observer.onScrollChange(4321, 1234)
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(ReaderAction.UpdateReaderScrollYAction::class) { action ->
            assertEquals("tab-id", action.tabId)
            assertEquals(1234, action.scrollY)
        }
    }

    @Test
    fun `onEnterPictureInPicture dispatches EnterAction`() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer = createEngineObserver(tabId = "tab-id", store = store, scope = this)
        val session: EngineSession = mock()

        observer.onEnterPictureInPicture(session)
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(PictureInPictureAction.EnterAction::class) { action ->
            assertEquals("tab-id", action.tabId)
            assertEquals(session, action.engineSession)
        }
    }

    @Test
    fun `onPictureInPictureRequestFinished dispatches ConsumeRequestAction`() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer = createEngineObserver(tabId = "tab-id", store = store, scope = this)

        observer.onPictureInPictureRequestFinished()
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(PictureInPictureAction.ConsumeRequestAction::class) { action ->
            assertEquals("tab-id", action.tabId)
        }
    }

    @Test
    fun `onExitPictureInPicture dispatches ExitAction`() = runTest {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store = BrowserStore(middleware = listOf(captureActionsMiddleware))
        val observer = createEngineObserver(tabId = "tab-id", store = store, scope = this)

        observer.onExitPictureInPicture()
        testScheduler.advanceUntilIdle()

        captureActionsMiddleware.assertFirstAction(PictureInPictureAction.ExitAction::class) { action ->
            assertEquals("tab-id", action.tabId)
        }
    }

    @Test
    fun `equality between tracking protection policies`() {
        val strict = EngineSession.TrackingProtectionPolicy.strict()
        val recommended = EngineSession.TrackingProtectionPolicy.recommended()
        val none = EngineSession.TrackingProtectionPolicy.none()
        val custom =
            EngineSession.TrackingProtectionPolicy.select(
                trackingCategories = emptyArray(),
                cookiePolicy = EngineSession.TrackingProtectionPolicy.CookiePolicy.ACCEPT_ONLY_FIRST_PARTY,
                cookiePurging = true,
                strictSocialTrackingProtection = true,
            )
        val custom2 =
            EngineSession.TrackingProtectionPolicy.select(
                trackingCategories = emptyArray(),
                cookiePolicy = EngineSession.TrackingProtectionPolicy.CookiePolicy.ACCEPT_ONLY_FIRST_PARTY,
                cookiePurging = true,
                strictSocialTrackingProtection = true,
            )

        val customNone =
            EngineSession.TrackingProtectionPolicy.select(
                trackingCategories = none.trackingCategories,
                cookiePolicy = none.cookiePolicy,
                cookiePurging = none.cookiePurging,
                strictSocialTrackingProtection = false,
            )

        assertTrue(strict == EngineSession.TrackingProtectionPolicy.strict())
        assertTrue(recommended == EngineSession.TrackingProtectionPolicy.recommended())
        assertTrue(none == EngineSession.TrackingProtectionPolicy.none())
        assertTrue(custom == custom2)

        assertFalse(strict == EngineSession.TrackingProtectionPolicy.strict().forPrivateSessionsOnly())
        assertFalse(recommended == EngineSession.TrackingProtectionPolicy.recommended().forPrivateSessionsOnly())
        assertFalse(custom == custom2.forPrivateSessionsOnly())

        assertFalse(strict == EngineSession.TrackingProtectionPolicy.strict().forRegularSessionsOnly())
        assertFalse(recommended == EngineSession.TrackingProtectionPolicy.recommended().forRegularSessionsOnly())
        assertFalse(custom == custom2.forRegularSessionsOnly())

        assertFalse(none == customNone)
    }

    private fun createEngineObserver(
        tabId: String = "mozilla",
        store: BrowserStore,
        scope: CoroutineScope,
    ): EngineObserver = EngineObserver(tabId, store, scope)
}
