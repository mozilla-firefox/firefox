/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.contextmenu

import android.content.ActivityNotFoundException
import android.content.ClipboardManager
import android.content.Context
import android.view.View
import androidx.compose.ui.text.style.TextOverflow
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.TestScope
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.EngineState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.content.ShareResourceState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.HitResult
import mozilla.components.feature.app.links.AppLinkRedirect
import mozilla.components.feature.app.links.AppLinksUseCases
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.eq
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.ui.widgets.SnackbarDelegate
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.anyBoolean
import org.mockito.Mockito
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.doThrow
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class ContextMenuCandidateTest {

    private lateinit var snackbarDelegate: TestSnackbarDelegate

    @Before
    fun setUp() {
        snackbarDelegate = TestSnackbarDelegate()
    }

    @Test
    fun `Default candidates sanity check`() {
        val candidates =
            ContextMenuCandidate.defaultCandidates(
                context = testContext,
                tabsUseCases = mock(),
                contextMenuUseCases = mock(),
                snackBarParentView = mock(),
                downloadsLocation = { "downloads" },
            )
        // Just a sanity check: When changing the list of default candidates be aware that this will affect all
        // consumers of this component using the default list.
        assertEquals(
            listOf(
                "mozac.feature.contextmenu.open_in_new_tab",
                "mozac.feature.contextmenu.open_in_private_tab",
                "mozac.feature.contextmenu.copy_link",
                "mozac.feature.contextmenu.copy_link_text",
                "mozac.feature.contextmenu.download_link",
                "mozac.feature.contextmenu.share_link",
                "mozac.feature.contextmenu.share_image",
                "mozac.feature.contextmenu.open_image_in_new_tab",
                "mozac.feature.contextmenu.copy_image",
                "mozac.feature.contextmenu.save_image",
                "mozac.feature.contextmenu.save_video",
                "mozac.feature.contextmenu.copy_image_location",
                "mozac.feature.contextmenu.add_to_contact",
                "mozac.feature.contextmenu.share_email",
                "mozac.feature.contextmenu.copy_email_address",
            ),
            candidates.map { it.id },
        )
    }

    @Test
    fun `Candidate 'Open Link in New Tab' showFor displayed in correct cases`() {
        val store = BrowserStore()
        val tabsUseCases = TabsUseCases(store)
        val parentView = CoordinatorLayout(testContext)
        val openInNewTab =
            ContextMenuCandidate.createOpenInNewTabCandidate(
                testContext,
                tabsUseCases,
                parentView,
                snackbarDelegate,
            )

        assertTrue(
            openInNewTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            openInNewTab.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertTrue(
            openInNewTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertFalse(
            openInNewTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertFalse(
            openInNewTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.VIDEO("https://www.mozilla.org"),
            )
        )
    }

    @Test
    fun `Candidate 'Open Link in New Tab' allows for an additional validation for it to be shown`() {
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val openInNewTab =
            ContextMenuCandidate.createOpenInNewTabCandidate(
                testContext,
                mock(),
                mock(),
                snackbarDelegate,
                additionalValidation,
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            openInNewTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            openInNewTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )
    }

    @Test
    fun `Candidate 'Open Link in New Tab' action properly executes for session with a contextId`() {
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org", contextId = "1"))),
                middleware = EngineMiddleware.create(engine = mock()),
            )

        val tabsUseCases = TabsUseCases(store)
        val parentView = CoordinatorLayout(testContext)
        val openInNewTab =
            ContextMenuCandidate.createOpenInNewTabCandidate(
                testContext,
                tabsUseCases,
                parentView,
                snackbarDelegate,
            )

        assertEquals(1, store.state.tabs.size)
        assertEquals("1", store.state.tabs.first().contextId)

        openInNewTab.action.invoke(store.state.tabs.first(), HitResult.UNKNOWN("https://firefox.com"))

        assertEquals(2, store.state.tabs.size)
        assertEquals("https://firefox.com", store.state.tabs.last().content.url)
        assertEquals("1", store.state.tabs.last().contextId)
    }

    @Test
    fun `Candidate 'Open Link in New Tab' action properly executes and shows snackbar`() {
        val store =
            BrowserStore(
                initialState = BrowserState(tabs = listOf(createTab("https://www.mozilla.org"))),
                middleware = EngineMiddleware.create(engine = mock()),
            )

        val tabsUseCases = TabsUseCases(store)
        val parentView = CoordinatorLayout(testContext)
        val openInNewTab =
            ContextMenuCandidate.createOpenInNewTabCandidate(
                testContext,
                tabsUseCases,
                parentView,
                snackbarDelegate,
            )

        assertEquals(1, store.state.tabs.size)
        assertFalse(snackbarDelegate.hasShownSnackbar)

        openInNewTab.action.invoke(store.state.tabs.first(), HitResult.UNKNOWN("https://firefox.com"))

        assertEquals(2, store.state.tabs.size)
        assertTrue(snackbarDelegate.hasShownSnackbar)
        assertNotNull(snackbarDelegate.lastActionListener)
        assertEquals("https://firefox.com", store.state.tabs.last().content.url)
    }

    @Test
    fun `Candidate 'Open Link in New Tab' snackbar action works`() {
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = mock(),
                        scope = TestScope(),
                    ),
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla")),
                        selectedTabId = "mozilla",
                    ),
            )
        val tabsUseCases = TabsUseCases(store)
        val parentView = CoordinatorLayout(testContext)

        val openInNewTab =
            ContextMenuCandidate.createOpenInNewTabCandidate(
                testContext,
                tabsUseCases,
                parentView,
                snackbarDelegate,
            )

        assertEquals(1, store.state.tabs.size)
        assertFalse(snackbarDelegate.hasShownSnackbar)

        openInNewTab.action.invoke(store.state.tabs.first(), HitResult.UNKNOWN("https://firefox.com"))

        assertEquals("https://www.mozilla.org", store.state.selectedTab!!.content.url)

        snackbarDelegate.lastActionListener!!.invoke(mock())

        assertEquals("https://firefox.com", store.state.selectedTab!!.content.url)
    }

    @Test
    fun `Candidate 'Open Link in New Tab' action properly handles link with an image`() {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla")),
                        selectedTabId = "mozilla",
                    ),
                middleware = EngineMiddleware.create(engine = mock()),
            )

        val tabsUseCases = TabsUseCases(store)
        val parentView = CoordinatorLayout(testContext)

        val openInNewTab =
            ContextMenuCandidate.createOpenInNewTabCandidate(
                testContext,
                tabsUseCases,
                parentView,
                snackbarDelegate,
            )

        assertEquals(1, store.state.tabs.size)

        openInNewTab.action.invoke(
            store.state.tabs.first(),
            HitResult.IMAGE_SRC("https://www.mozilla_src.org", "https://www.mozilla_uri.org"),
        )

        assertEquals("https://www.mozilla_uri.org", store.state.tabs.last().content.url)
    }

    @Test
    fun `Open Link in New Tab with text fragment`() {
        val middleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store =
            BrowserStore(
                middleware = listOf(middleware) + EngineMiddleware.create(engine = mock()),
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla")),
                        selectedTabId = "mozilla",
                    ),
            )

        val tabsUseCases = TabsUseCases(store)
        val parentView = CoordinatorLayout(testContext)

        val openInNewTab =
            ContextMenuCandidate.createOpenInNewTabCandidate(
                testContext,
                tabsUseCases,
                parentView,
                snackbarDelegate,
            )

        assertEquals(1, store.state.tabs.size)

        openInNewTab.action.invoke(
            store.state.tabs.first(),
            HitResult.UNKNOWN("https://www.mozilla.org"),
        )

        middleware.assertLastAction(EngineAction.LoadUrlAction::class) { action ->
            assertEquals("https://www.mozilla.org", action.url)
            assertEquals(true, action.textDirectiveUserActivation)
        }
    }

    @Test
    fun `Candidate 'Open Link in Private Tab' showFor displayed in correct cases`() {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla")),
                        selectedTabId = "mozilla",
                    )
            )

        val tabsUseCases = TabsUseCases(store)
        val parentView = CoordinatorLayout(testContext)
        val openInPrivateTab =
            ContextMenuCandidate.createOpenInPrivateTabCandidate(
                testContext,
                tabsUseCases,
                parentView,
                snackbarDelegate,
            )

        assertTrue(
            openInPrivateTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertTrue(
            openInPrivateTab.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertTrue(
            openInPrivateTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertFalse(
            openInPrivateTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertFalse(
            openInPrivateTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.VIDEO("https://www.mozilla.org"),
            )
        )
    }

    @Test
    fun `Candidate 'Open Link in Private Tab' allows for an additional validation for it to be shown`() {
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val openInPrivateTab =
            ContextMenuCandidate.createOpenInPrivateTabCandidate(
                testContext,
                mock(),
                mock(),
                snackbarDelegate,
                additionalValidation,
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            openInPrivateTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            openInPrivateTab.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            openInPrivateTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )
    }

    @Test
    fun `Candidate 'Open Link in Private Tab' action properly executes and shows snackbar`() {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla")),
                        selectedTabId = "mozilla",
                    ),
                middleware = EngineMiddleware.create(engine = mock()),
            )

        val tabsUseCases = TabsUseCases(store)
        val parentView = CoordinatorLayout(testContext)
        val openInPrivateTab =
            ContextMenuCandidate.createOpenInPrivateTabCandidate(
                testContext,
                tabsUseCases,
                parentView,
                snackbarDelegate,
            )

        assertEquals(1, store.state.tabs.size)
        assertFalse(snackbarDelegate.hasShownSnackbar)

        openInPrivateTab.action.invoke(store.state.tabs.first(), HitResult.UNKNOWN("https://firefox.com"))

        assertEquals(2, store.state.tabs.size)
        assertTrue(snackbarDelegate.hasShownSnackbar)
        assertNotNull(snackbarDelegate.lastActionListener)
        assertEquals("https://firefox.com", store.state.tabs.last().content.url)
    }

    @Test
    fun `Candidate 'Open Link in Private Tab' snackbar action works`() {
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = mock(),
                        scope = TestScope(),
                    ),
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla")),
                        selectedTabId = "mozilla",
                    ),
            )

        val tabsUseCases = TabsUseCases(store)
        val parentView = CoordinatorLayout(testContext)
        val openInPrivateTab =
            ContextMenuCandidate.createOpenInPrivateTabCandidate(
                testContext,
                tabsUseCases,
                parentView,
                snackbarDelegate,
            )

        assertEquals(1, store.state.tabs.size)
        assertFalse(snackbarDelegate.hasShownSnackbar)

        openInPrivateTab.action.invoke(store.state.tabs.first(), HitResult.UNKNOWN("https://firefox.com"))

        assertEquals("https://www.mozilla.org", store.state.selectedTab!!.content.url)
        assertEquals(2, store.state.tabs.size)

        snackbarDelegate.lastActionListener!!.invoke(mock())

        assertEquals("https://firefox.com", store.state.selectedTab!!.content.url)
    }

    @Test
    fun `Candidate 'Open Link in Private Tab' action properly handles link with an image`() {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla")),
                        selectedTabId = "mozilla",
                    ),
                middleware = EngineMiddleware.create(engine = mock()),
            )

        val tabsUseCases = TabsUseCases(store)
        val parentView = CoordinatorLayout(testContext)
        val openInPrivateTab =
            ContextMenuCandidate.createOpenInPrivateTabCandidate(
                testContext,
                tabsUseCases,
                parentView,
                snackbarDelegate,
            )

        assertEquals(1, store.state.tabs.size)
        openInPrivateTab.action.invoke(
            store.state.tabs.first(),
            HitResult.IMAGE_SRC("https://www.mozilla_src.org", "https://www.mozilla_uri.org"),
        )
        assertEquals("https://www.mozilla_uri.org", store.state.tabs.last().content.url)
    }

    @Test
    fun `Candidate 'Open Image in New Tab'`() {
        val store =
            BrowserStore(
                middleware =
                    EngineMiddleware.create(
                        engine = mock(),
                        scope = TestScope(),
                    ),
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla")),
                        selectedTabId = "mozilla",
                    ),
            )

        val tabsUseCases = TabsUseCases(store)
        val parentView = CoordinatorLayout(testContext)

        val openImageInTab =
            ContextMenuCandidate.createOpenImageInNewTabCandidate(
                testContext,
                tabsUseCases,
                parentView,
                snackbarDelegate,
            )

        // showFor

        assertFalse(
            openImageInTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            openImageInTab.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertTrue(
            openImageInTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertTrue(
            openImageInTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertFalse(
            openImageInTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.VIDEO("https://www.mozilla.org"),
            )
        )

        // action

        assertEquals(1, store.state.tabs.size)
        assertFalse(snackbarDelegate.hasShownSnackbar)

        openImageInTab.action.invoke(
            store.state.tabs.first(),
            HitResult.IMAGE_SRC("https://firefox.com", "https://getpocket.com"),
        )

        assertEquals(2, store.state.tabs.size)
        assertFalse(store.state.tabs.last().content.private)
        assertEquals("https://firefox.com", store.state.tabs.last().content.url)
        assertTrue(snackbarDelegate.hasShownSnackbar)
        assertNotNull(snackbarDelegate.lastActionListener)

        // Snackbar action

        assertEquals("https://www.mozilla.org", store.state.selectedTab!!.content.url)

        snackbarDelegate.lastActionListener!!.invoke(mock())

        assertEquals("https://firefox.com", store.state.selectedTab!!.content.url)
    }

    @Test
    fun `Candidate 'Open Image in New Tab' allows for an additional validation for it to be shown`() {
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val openImageInTab =
            ContextMenuCandidate.createOpenImageInNewTabCandidate(
                testContext,
                mock(),
                mock(),
                snackbarDelegate,
                additionalValidation,
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            openImageInTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertFalse(
            openImageInTab.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )
    }

    @Test
    fun `Candidate 'Open Image in New Tab' opens in private tab if session is private`() {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla", private = true)),
                        selectedTabId = "mozilla",
                    ),
                middleware = EngineMiddleware.create(engine = mock()),
            )

        val tabsUseCases = TabsUseCases(store)
        val parentView = CoordinatorLayout(testContext)

        val openImageInTab =
            ContextMenuCandidate.createOpenImageInNewTabCandidate(
                testContext,
                tabsUseCases,
                parentView,
                snackbarDelegate,
            )

        assertEquals(1, store.state.tabs.size)

        openImageInTab.action.invoke(
            store.state.tabs.first(),
            HitResult.IMAGE_SRC("https://firefox.com", "https://getpocket.com"),
        )

        assertEquals(2, store.state.tabs.size)
        assertTrue(store.state.tabs.last().content.private)
        assertEquals("https://firefox.com", store.state.tabs.last().content.url)
    }

    @Test
    fun `Candidate 'Open Image in New Tab' opens with the session's contextId`() {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla", contextId = "1")),
                        selectedTabId = "mozilla",
                    ),
                middleware = EngineMiddleware.create(engine = mock()),
            )

        val tabsUseCases = TabsUseCases(store)
        val parentView = CoordinatorLayout(testContext)

        val openImageInTab =
            ContextMenuCandidate.createOpenImageInNewTabCandidate(
                testContext,
                tabsUseCases,
                parentView,
                snackbarDelegate,
            )

        assertEquals(1, store.state.tabs.size)
        assertEquals("1", store.state.tabs.first().contextId)

        openImageInTab.action.invoke(
            store.state.tabs.first(),
            HitResult.IMAGE_SRC("https://firefox.com", "https://getpocket.com"),
        )

        assertEquals(2, store.state.tabs.size)
        assertEquals("https://firefox.com", store.state.tabs.last().content.url)
        assertEquals("1", store.state.tabs.last().contextId)
    }

    @Test
    fun `Candidate 'Save image'`() {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla", private = true)),
                        selectedTabId = "mozilla",
                    )
            )

        val saveImage =
            ContextMenuCandidate.createSaveImageCandidate(
                testContext,
                ContextMenuUseCases(store),
                downloadsLocation = { "downloads" },
            )

        // showFor

        assertFalse(
            saveImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            saveImage.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertTrue(
            saveImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertTrue(
            saveImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertFalse(
            saveImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.VIDEO("https://www.mozilla.org"),
            )
        )

        // action

        assertNull(store.state.tabs.first().content.download)

        saveImage.action.invoke(
            store.state.tabs.first(),
            HitResult.IMAGE_SRC(
                "https://www.mozilla.org/media/img/logos/firefox/logo-quantum.9c5e96634f92.png",
                "https://firefox.com",
            ),
        )

        assertNotNull(store.state.tabs.first().content.download)
        assertEquals(
            "https://www.mozilla.org/media/img/logos/firefox/logo-quantum.9c5e96634f92.png",
            store.state.tabs.first().content.download!!.url,
        )
        assertTrue(store.state.tabs.first().content.download!!.skipConfirmation)
        assertTrue(store.state.tabs.first().content.download!!.private)
    }

    @Test
    fun `Candidate 'Save image' allows for an additional validation for it to be shown`() {
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val saveImage =
            ContextMenuCandidate.createSaveImageCandidate(
                context = testContext,
                contextMenuUseCases = mock(),
                downloadsLocation = { "downloads" },
                additionalValidation = additionalValidation,
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            saveImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertFalse(
            saveImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )
    }

    @Test
    fun `Candidate 'Save video and audio'`() {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla", private = true)),
                        selectedTabId = "mozilla",
                    )
            )

        val saveVideoAudio =
            ContextMenuCandidate.createSaveVideoAudioCandidate(
                context = testContext,
                contextMenuUseCases = ContextMenuUseCases(store),
                downloadsLocation = { "downloads" },
            )

        // showFor

        assertFalse(
            saveVideoAudio.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            saveVideoAudio.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            saveVideoAudio.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertFalse(
            saveVideoAudio.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertTrue(
            saveVideoAudio.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.VIDEO("https://www.mozilla.org"),
            )
        )

        assertTrue(
            saveVideoAudio.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.AUDIO("https://www.mozilla.org"),
            )
        )

        // action

        assertNull(store.state.tabs.first().content.download)

        saveVideoAudio.action.invoke(
            store.state.tabs.first(),
            HitResult.AUDIO("https://developer.mozilla.org/media/examples/t-rex-roar.mp3"),
        )

        assertNotNull(store.state.tabs.first().content.download)
        assertEquals(
            "https://developer.mozilla.org/media/examples/t-rex-roar.mp3",
            store.state.tabs.first().content.download!!.url,
        )
        assertTrue(store.state.tabs.first().content.download!!.skipConfirmation)

        assertTrue(store.state.tabs.first().content.download!!.private)
    }

    @Test
    fun `Candidate 'Save video and audio' allows for an additional validation for it to be shown`() {
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val saveVideoAudio =
            ContextMenuCandidate.createSaveVideoAudioCandidate(
                context = testContext,
                contextMenuUseCases = mock(),
                additionalValidation = additionalValidation,
                downloadsLocation = { "downloads" },
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            saveVideoAudio.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.VIDEO("https://www.mozilla.org"),
            )
        )

        assertFalse(
            saveVideoAudio.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.AUDIO("https://www.mozilla.org"),
            )
        )
    }

    @Test
    fun `Candidate 'download link'`() {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla", private = true)),
                        selectedTabId = "mozilla",
                    )
            )

        val downloadLink =
            ContextMenuCandidate.createDownloadLinkCandidate(
                context = testContext,
                contextMenuUseCases = ContextMenuUseCases(store),
                downloadsLocation = { "downloads" },
            )

        // showFor

        assertTrue(
            downloadLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertTrue(
            downloadLink.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertTrue(
            downloadLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertFalse(
            downloadLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertFalse(
            downloadLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.VIDEO("https://www.mozilla.org"),
            )
        )

        assertFalse(
            downloadLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.PHONE("https://www.mozilla.org"),
            )
        )

        assertFalse(
            downloadLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.EMAIL("https://www.mozilla.org"),
            )
        )

        assertFalse(
            downloadLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.GEO("https://www.mozilla.org"),
            )
        )

        assertFalse(
            downloadLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org/firefox/products.html"),
            )
        )

        assertFalse(
            downloadLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org/firefox/products.htm"),
            )
        )

        // action

        assertNull(store.state.tabs.first().content.download)

        downloadLink.action.invoke(
            store.state.tabs.first(),
            HitResult.IMAGE_SRC(
                "https://www.mozilla.org/media/img/logos/firefox/logo-quantum.9c5e96634f92.png",
                "https://www.mozilla.org/en-US/privacy-policy.pdf",
            ),
        )

        assertNotNull(store.state.tabs.first().content.download)
        assertEquals(
            "https://www.mozilla.org/en-US/privacy-policy.pdf",
            store.state.tabs.first().content.download!!.url,
        )
        assertTrue(store.state.tabs.first().content.download!!.skipConfirmation)

        assertTrue(store.state.tabs.first().content.download!!.private)
    }

    @Test
    fun `Candidate 'download link' allows for an additional validation for it to be shown`() {
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val downloadLink =
            ContextMenuCandidate.createDownloadLinkCandidate(
                context = testContext,
                contextMenuUseCases = mock(),
                additionalValidation = additionalValidation,
                downloadsLocation = { "downloads" },
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            downloadLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            downloadLink.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            downloadLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )
    }

    @Test
    fun `Get link for image, video, audio gets title if title is set`() {
        val titleString = "test title"

        val hitResultImage = HitResult.IMAGE("https://www.mozilla.org", titleString)
        var title = hitResultImage.getLink()
        assertEquals(titleString, title)

        val hitResultVideo = HitResult.VIDEO("https://www.mozilla.org", titleString)
        title = hitResultVideo.getLink()
        assertEquals(titleString, title)

        val hitResultAudio = HitResult.AUDIO("https://www.mozilla.org", titleString)
        title = hitResultAudio.getLink()
        assertEquals(titleString, title)
    }

    @Test
    fun `Get link for image, video, audio gets URL if title is blank`() {
        val titleString = " "
        val url = "https://www.mozilla.org"

        val hitResultImage = HitResult.IMAGE(url, titleString)
        var title = hitResultImage.getLink()
        assertEquals(url, title)

        val hitResultVideo = HitResult.VIDEO(url, titleString)
        title = hitResultVideo.getLink()
        assertEquals(url, title)

        val hitResultAudio = HitResult.AUDIO(url, titleString)
        title = hitResultAudio.getLink()
        assertEquals(url, title)
    }

    @Test
    fun `Get link for image, video, audio gets URL if title is null`() {
        val titleString = null
        val url = "https://www.mozilla.org"

        val hitResultImage = HitResult.IMAGE(url, titleString)
        var title = hitResultImage.getLink()
        assertEquals(url, title)

        val hitResultVideo = HitResult.VIDEO(url, titleString)
        title = hitResultVideo.getLink()
        assertEquals(url, title)

        val hitResultAudio = HitResult.AUDIO(url, titleString)
        title = hitResultAudio.getLink()
        assertEquals(url, title)
    }

    @Test
    fun `Get link for image gets 'image' title if title is null and URL is longer than 2500 characters`() {
        val titleString = null
        val replacementString = "image"
        val url = "1".repeat(ContextMenuCandidate.MAX_TITLE_LENGTH + 1)

        val hitResultImage = HitResult.IMAGE(url, titleString)
        val title = hitResultImage.getLink()
        assertEquals(replacementString, title)
    }

    @Test
    fun `Get link for image gets URL if title is null and URL is not longer than 2500 characters`() {
        val titleString = null
        val url = "1".repeat(ContextMenuCandidate.MAX_TITLE_LENGTH)

        val hitResultImage = HitResult.IMAGE(url, titleString)
        val title = hitResultImage.getLink()
        assertEquals(url, title)
    }

    @Test
    fun `Candidate 'Share Link'`() {
        val context = spy(testContext)

        val shareLink = ContextMenuCandidate.createShareLinkCandidate(context)

        // showFor

        assertTrue(
            shareLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertTrue(
            shareLink.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertTrue(
            shareLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertTrue(
            shareLink.showFor(
                createTab("test://www.mozilla.org"),
                HitResult.UNKNOWN("test://www.mozilla.org"),
            )
        )

        assertTrue(
            shareLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertTrue(
            shareLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.VIDEO("https://www.mozilla.org"),
            )
        )

        // action

        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla", private = true)),
                        selectedTabId = "mozilla",
                    )
            )

        shareLink.action.invoke(
            store.state.tabs.first(),
            HitResult.IMAGE_SRC("https://firefox.com", "https://getpocket.com"),
        )

        verify(context).startActivity(any())
    }

    @Test
    fun `Candidate 'Share Link' allows for an additional validation for it to be shown`() {
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val shareLink =
            ContextMenuCandidate.createShareLinkCandidate(
                testContext,
                additionalValidation,
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            shareLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            shareLink.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            shareLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertFalse(
            shareLink.showFor(
                createTab("test://www.mozilla.org"),
                HitResult.UNKNOWN("test://www.mozilla.org"),
            )
        )

        assertFalse(
            shareLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertFalse(
            shareLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.VIDEO("https://www.mozilla.org"),
            )
        )
    }

    @Test
    fun `GIVEN share error WHEN invoking share link candidate THEN does not crash`() {
        val context = spy(testContext)
        var errorThrown = false

        doThrow(ActivityNotFoundException()).`when`(context).startActivity(any())
        val shareLink = ContextMenuCandidate.createShareLinkCandidate(context)

        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla", private = true)),
                        selectedTabId = "mozilla",
                    )
            )

        try {
            shareLink.action.invoke(
                store.state.tabs.first(),
                HitResult.IMAGE_SRC("https://firefox.com", "https://getpocket.com"),
            )
        } catch (e: Exception) {
            errorThrown = true
        }

        verify(context).startActivity(any())
        assertFalse(errorThrown)
    }

    @Test
    fun `Candidate 'Share image'`() {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(tabs = listOf(TabSessionState("123", ContentState(url = "https://www.mozilla.org"))))
            )
        val context = spy(testContext)

        val usecases = spy(ContextMenuUseCases(store))
        val shareUsecase: ContextMenuUseCases.InjectShareInternetResourceUseCase = mock()
        doReturn(shareUsecase).`when`(usecases).injectShareFromInternet
        val shareImage = ContextMenuCandidate.createShareImageCandidate(context, usecases)
        val shareStateCaptor = argumentCaptor<ShareResourceState.InternetResource>()
        // showFor

        assertTrue(
            shareImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertTrue(
            shareImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertFalse(
            shareImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.AUDIO("https://www.mozilla.org"),
            )
        )

        // action

        shareImage.action.invoke(
            store.state.tabs.first(),
            HitResult.IMAGE_SRC("https://firefox.com", "https://getpocket.com"),
        )

        verify(shareUsecase).invoke(eq("123"), shareStateCaptor.capture())
        assertEquals("https://firefox.com", shareStateCaptor.value.url)
        assertEquals(store.state.tabs.first().content.private, shareStateCaptor.value.private)
    }

    @Test
    fun `Candidate 'Share image' allows for an additional validation for it to be shown`() {
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val shareImage =
            ContextMenuCandidate.createShareImageCandidate(
                testContext,
                mock(),
                additionalValidation,
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            shareImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertFalse(
            shareImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )
    }

    @Test
    fun `Candidate 'Copy image'`() {
        val store =
            BrowserStore(
                initialState =
                    BrowserState(tabs = listOf(TabSessionState("123", ContentState(url = "https://www.mozilla.org"))))
            )
        val context = spy(testContext)

        val useCases = spy(ContextMenuUseCases(store))
        val copyUseCase: ContextMenuUseCases.InjectCopyInternetResourceUseCase = mock()
        doReturn(copyUseCase).`when`(useCases).injectCopyFromInternet
        val copyImage = ContextMenuCandidate.createCopyImageCandidate(context, useCases)
        val shareStateCaptor = argumentCaptor<ShareResourceState.InternetResource>()

        // showFor

        assertTrue(
            copyImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertTrue(
            copyImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertFalse(
            copyImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.AUDIO("https://www.mozilla.org"),
            )
        )

        // action

        copyImage.action.invoke(
            store.state.tabs.first(),
            HitResult.IMAGE_SRC("https://firefox.com", "https://getpocket.com"),
        )

        verify(copyUseCase).invoke(eq("123"), shareStateCaptor.capture())
        assertEquals("https://firefox.com", shareStateCaptor.value.url)
        assertEquals(store.state.tabs.first().content.private, shareStateCaptor.value.private)
    }

    @Test
    fun `Candidate 'Copy image' allows for an additional validation for it to be shown`() {
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val copyImage =
            ContextMenuCandidate.createCopyImageCandidate(
                testContext,
                mock(),
                additionalValidation,
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            copyImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertFalse(
            copyImage.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )
    }

    @Test
    fun `Candidate 'Copy Link'`() {
        val parentView = CoordinatorLayout(testContext)

        val copyLink =
            ContextMenuCandidate.createCopyLinkCandidate(
                testContext,
                parentView,
                snackbarDelegate,
            )

        // showFor

        assertTrue(
            copyLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertTrue(
            copyLink.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertTrue(
            copyLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertTrue(
            copyLink.showFor(
                createTab("test://www.mozilla.org"),
                HitResult.UNKNOWN("test://www.mozilla.org"),
            )
        )

        assertTrue(
            copyLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertTrue(
            copyLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.VIDEO("https://www.mozilla.org"),
            )
        )

        // action

        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla", private = true)),
                        selectedTabId = "mozilla",
                    )
            )

        copyLink.action.invoke(
            store.state.tabs.first(),
            HitResult.IMAGE_SRC("https://firefox.com", "https://getpocket.com"),
        )

        assertTrue(snackbarDelegate.hasShownSnackbar)

        val clipboardManager = testContext.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        assertEquals(
            "https://getpocket.com",
            clipboardManager.primaryClip!!.getItemAt(0).text,
        )
    }

    @Test
    fun `Candidate 'Copy link text' is shown for UNKNOWN HitResult with link text`() {
        val parentView = CoordinatorLayout(testContext)

        val copyLinkText =
            ContextMenuCandidate.createCopyLinkTextCandidate(
                testContext,
                parentView,
                snackbarDelegate,
            )

        assertTrue(
            "Copy link text is shown for HitResult.UNKNOWN with link text",
            copyLinkText.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN(src = "https://www.mozilla.org", linkText = "Mozilla"),
            ),
        )
    }

    @Test
    fun `Candidate 'Copy link text' not shown for UNKNOWN HitResult without link text`() {
        val parentView = CoordinatorLayout(testContext)

        val copyLinkText =
            ContextMenuCandidate.createCopyLinkTextCandidate(
                testContext,
                parentView,
                snackbarDelegate,
            )

        assertFalse(
            "Copy link text is not shown for HitResult.UNKNOWN with empty link text",
            copyLinkText.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN(src = "https://www.mozilla.org", linkText = ""),
            ),
        )
        assertFalse(
            "Copy link text is not shown for HitResult.UNKNOWN with null link text",
            copyLinkText.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN(src = "https://www.mozilla.org", linkText = null),
            ),
        )
    }

    @Test
    fun `Candidate 'Copy Link' for videos`() {
        val parentView = CoordinatorLayout(testContext)

        val copyLink =
            ContextMenuCandidate.createCopyLinkCandidate(
                testContext,
                parentView,
                snackbarDelegate,
            )

        assertTrue(
            copyLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.VIDEO("https://www.mozilla.org"),
            )
        )

        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla", private = true)),
                        selectedTabId = "mozilla",
                    )
            )

        copyLink.action.invoke(
            store.state.tabs.first(),
            HitResult.VIDEO(src = "https://www.video_test.com", title = "video_test"),
        )

        assertTrue(snackbarDelegate.hasShownSnackbar)

        val clipboardManager = testContext.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        assertEquals(
            "https://www.video_test.com",
            clipboardManager.primaryClip!!.getItemAt(0).text,
        )
    }

    @Test
    fun `Candidate 'Copy Link' allows for an additional validation for it to be shown`() {
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val copyLink =
            ContextMenuCandidate.createCopyLinkCandidate(
                testContext,
                mock(),
                snackbarDelegate,
                additionalValidation,
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            copyLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            copyLink.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            copyLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertFalse(
            copyLink.showFor(
                createTab("test://www.mozilla.org"),
                HitResult.UNKNOWN("test://www.mozilla.org"),
            )
        )

        assertFalse(
            copyLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertFalse(
            copyLink.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.VIDEO("https://www.mozilla.org"),
            )
        )
    }

    @Test
    fun `Candidate 'Copy Image Location'`() {
        val parentView = CoordinatorLayout(testContext)

        val copyImageLocation =
            ContextMenuCandidate.createCopyImageLocationCandidate(
                testContext,
                parentView,
                snackbarDelegate,
            )

        // showFor

        assertFalse(
            copyImageLocation.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            copyImageLocation.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertTrue(
            copyImageLocation.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertTrue(
            copyImageLocation.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )

        assertFalse(
            copyImageLocation.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.VIDEO("https://www.mozilla.org"),
            )
        )

        // action

        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla", private = true)),
                        selectedTabId = "mozilla",
                    )
            )

        copyImageLocation.action.invoke(
            store.state.tabs.first(),
            HitResult.IMAGE_SRC("https://firefox.com", "https://getpocket.com"),
        )

        assertTrue(snackbarDelegate.hasShownSnackbar)

        val clipboardManager = testContext.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        assertEquals(
            "https://firefox.com",
            clipboardManager.primaryClip!!.getItemAt(0).text,
        )
    }

    @Test
    fun `Candidate 'Copy Image Location' allows for an additional validation for it to be shown`() {
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val copyImageLocation =
            ContextMenuCandidate.createCopyImageLocationCandidate(
                testContext,
                mock(),
                snackbarDelegate,
                additionalValidation,
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            copyImageLocation.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE_SRC("https://www.mozilla.org", "https://www.mozilla.org"),
            )
        )

        assertFalse(
            copyImageLocation.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.IMAGE("https://www.mozilla.org"),
            )
        )
    }

    @Test
    fun `Candidate 'Open in external app'`() {
        val tab = createTab("https://www.mozilla.org")
        val getAppLinkRedirectMock: AppLinksUseCases.GetAppLinkRedirect = mock()

        doReturn(AppLinkRedirect(mock(), "", null, null))
            .`when`(getAppLinkRedirectMock)
            .invoke(eq("https://www.example.com"))

        doReturn(AppLinkRedirect(null, "", null, mock()))
            .`when`(getAppLinkRedirectMock)
            .invoke(eq("intent:www.example.com#Intent;scheme=https;package=org.mozilla.fenix;end"))

        doReturn(AppLinkRedirect(null, "", null, null))
            .`when`(getAppLinkRedirectMock)
            .invoke(eq("https://www.otherexample.com"))

        // This mock exists only to verify that it was called
        val openAppLinkRedirectMock: AppLinksUseCases.OpenAppLinkRedirect = mock()

        val appLinksUseCasesMock: AppLinksUseCases = mock()
        doReturn(getAppLinkRedirectMock).`when`(appLinksUseCasesMock).appLinkRedirectIncludeInstall
        doReturn(openAppLinkRedirectMock).`when`(appLinksUseCasesMock).openAppLink

        val openLinkInExternalApp =
            ContextMenuCandidate.createOpenInExternalAppCandidate(
                testContext,
                appLinksUseCasesMock,
            )

        // showFor

        assertTrue(
            openLinkInExternalApp.showFor(
                tab,
                HitResult.UNKNOWN("https://www.example.com"),
            )
        )

        assertTrue(
            openLinkInExternalApp.showFor(
                tab,
                HitResult.UNKNOWN("intent:www.example.com#Intent;scheme=https;package=org.mozilla.fenix;end"),
            )
        )

        assertTrue(
            openLinkInExternalApp.showFor(
                tab,
                HitResult.VIDEO("https://www.example.com"),
            )
        )

        assertTrue(
            openLinkInExternalApp.showFor(
                tab,
                HitResult.AUDIO("https://www.example.com"),
            )
        )

        assertFalse(
            openLinkInExternalApp.showFor(
                tab,
                HitResult.UNKNOWN("https://www.otherexample.com"),
            )
        )

        assertFalse(
            openLinkInExternalApp.showFor(
                tab,
                HitResult.VIDEO("https://www.otherexample.com"),
            )
        )

        assertFalse(
            openLinkInExternalApp.showFor(
                tab,
                HitResult.AUDIO("https://www.otherexample.com"),
            )
        )

        // action

        openLinkInExternalApp.action.invoke(
            tab,
            HitResult.UNKNOWN("https://www.example.com"),
        )

        openLinkInExternalApp.action.invoke(
            tab,
            HitResult.UNKNOWN("intent:www.example.com#Intent;scheme=https;package=org.mozilla.fenix;end"),
        )

        openLinkInExternalApp.action.invoke(
            tab,
            HitResult.UNKNOWN("https://www.otherexample.com"),
        )

        verify(openAppLinkRedirectMock, times(2)).invoke(any(), anyBoolean(), anyBoolean(), any())
    }

    @Test
    fun `Candidate 'Open in external app' allows for an additional validation for it to be shown`() {
        val tab = createTab("https://www.mozilla.org")
        val getAppLinkRedirectMock: AppLinksUseCases.GetAppLinkRedirect = mock()
        doReturn(AppLinkRedirect(mock(), "", null, null))
            .`when`(getAppLinkRedirectMock)
            .invoke(eq("https://www.example.com"))
        doReturn(AppLinkRedirect(null, "", null, mock()))
            .`when`(getAppLinkRedirectMock)
            .invoke(eq("intent:www.example.com#Intent;scheme=https;package=org.mozilla.fenix;end"))
        val openAppLinkRedirectMock: AppLinksUseCases.OpenAppLinkRedirect = mock()
        val appLinksUseCasesMock: AppLinksUseCases = mock()
        doReturn(getAppLinkRedirectMock).`when`(appLinksUseCasesMock).appLinkRedirectIncludeInstall
        doReturn(openAppLinkRedirectMock).`when`(appLinksUseCasesMock).openAppLink
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val openLinkInExternalApp =
            ContextMenuCandidate.createOpenInExternalAppCandidate(
                testContext,
                appLinksUseCasesMock,
                additionalValidation,
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            openLinkInExternalApp.showFor(
                tab,
                HitResult.UNKNOWN("https://www.example.com"),
            )
        )

        assertFalse(
            openLinkInExternalApp.showFor(
                tab,
                HitResult.UNKNOWN("intent:www.example.com#Intent;scheme=https;package=org.mozilla.fenix;end"),
            )
        )

        assertFalse(
            openLinkInExternalApp.showFor(
                tab,
                HitResult.VIDEO("https://www.example.com"),
            )
        )

        assertFalse(
            openLinkInExternalApp.showFor(
                tab,
                HitResult.AUDIO("https://www.example.com"),
            )
        )
    }

    @Test
    fun `Candidate 'Copy email address'`() {
        val parentView = CoordinatorLayout(testContext)

        val copyEmailAddress =
            ContextMenuCandidate.createCopyEmailAddressCandidate(
                testContext,
                parentView,
                snackbarDelegate,
            )

        // showFor

        assertTrue(
            copyEmailAddress.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("mailto:example@example.com"),
            )
        )

        assertTrue(
            copyEmailAddress.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("mailto:example.com"),
            )
        )

        assertFalse(
            copyEmailAddress.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            copyEmailAddress.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("example@example.com"),
            )
        )

        // action

        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla", private = true)),
                        selectedTabId = "mozilla",
                    )
            )

        copyEmailAddress.action.invoke(
            store.state.tabs.first(),
            HitResult.UNKNOWN("mailto:example@example.com"),
        )

        assertTrue(snackbarDelegate.hasShownSnackbar)

        val clipboardManager = testContext.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        assertEquals(
            "example@example.com",
            clipboardManager.primaryClip!!.getItemAt(0).text,
        )
    }

    @Test
    fun `Candidate 'Copy email address' allows for an additional validation for it to be shown`() {
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val copyEmailAddress =
            ContextMenuCandidate.createCopyEmailAddressCandidate(
                testContext,
                mock(),
                snackbarDelegate,
                additionalValidation,
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            copyEmailAddress.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("mailto:example@example.com"),
            )
        )

        assertFalse(
            copyEmailAddress.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("mailto:example.com"),
            )
        )
    }

    @Test
    fun `Candidate 'Share email address'`() {
        val context = spy(testContext)

        val shareEmailAddress = ContextMenuCandidate.createShareEmailAddressCandidate(context)

        // showFor

        assertTrue(
            shareEmailAddress.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("mailto:example@example.com"),
            )
        )

        assertTrue(
            shareEmailAddress.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("mailto:example.com"),
            )
        )

        assertFalse(
            shareEmailAddress.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            shareEmailAddress.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("example@example.com"),
            )
        )

        // action

        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla", private = true)),
                        selectedTabId = "mozilla",
                    )
            )

        shareEmailAddress.action.invoke(
            store.state.tabs.first(),
            HitResult.UNKNOWN("mailto:example@example.com"),
        )

        verify(context).startActivity(any())
    }

    @Test
    fun `Candidate 'Share email address' allows for an additional validation for it to be shown`() {
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val shareEmailAddress =
            ContextMenuCandidate.createShareEmailAddressCandidate(
                testContext,
                additionalValidation,
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            shareEmailAddress.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("mailto:example@example.com"),
            )
        )

        assertFalse(
            shareEmailAddress.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("mailto:example.com"),
            )
        )
    }

    @Test
    fun `Candidate 'Add to contacts'`() {
        val context = spy(testContext)

        val addToContacts = ContextMenuCandidate.createAddContactCandidate(context)

        // showFor

        assertTrue(
            addToContacts.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("mailto:example@example.com"),
            )
        )

        assertTrue(
            addToContacts.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("mailto:example.com"),
            )
        )

        assertFalse(
            addToContacts.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("https://www.mozilla.org"),
            )
        )

        assertFalse(
            addToContacts.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("example@example.com"),
            )
        )

        // action

        val store =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(createTab("https://www.mozilla.org", id = "mozilla", private = true)),
                        selectedTabId = "mozilla",
                    )
            )

        addToContacts.action.invoke(
            store.state.tabs.first(),
            HitResult.UNKNOWN("mailto:example@example.com"),
        )

        verify(context).startActivity(any())
    }

    @Test
    fun `Candidate 'Add to contacts' allows for an additional validation for it to be shown`() {
        val additionalValidation = { _: SessionState, _: HitResult -> false }
        val addToContacts =
            ContextMenuCandidate.createAddContactCandidate(
                testContext,
                additionalValidation,
            )

        // By default in the below cases the candidate will be shown. 'additionalValidation' changes that.

        assertFalse(
            addToContacts.showFor(
                createTab("https://www.mozilla.org"),
                HitResult.UNKNOWN("mailto:example@example.com"),
            )
        )

        assertFalse(
            addToContacts.showFor(
                createTab("https://www.mozilla.org", private = true),
                HitResult.UNKNOWN("mailto:example.com"),
            )
        )
    }

    @Test
    fun `GIVEN SessionState with null EngineSession WHEN isUrlSchemeAllowed is called THEN it returns true`() {
        val sessionState =
            TabSessionState(
                content = mock(),
                engineState = EngineState(engineSession = null),
            )

        assertTrue(sessionState.isUrlSchemeAllowed("http://mozilla.org"))
    }

    @Test
    fun `GIVEN SessionState with no blocked url schemes WHEN isUrlSchemeAllowed is called THEN it returns true`() {
        val noBlockedUrlSchemesEngineSession = Mockito.mock(EngineSession::class.java)
        doReturn(emptyList<String>()).`when`(noBlockedUrlSchemesEngineSession).getBlockedSchemes()
        val sessionState =
            TabSessionState(
                content = mock(),
                engineState = EngineState(engineSession = noBlockedUrlSchemesEngineSession),
            )

        assertTrue(sessionState.isUrlSchemeAllowed("http://mozilla.org"))
    }

    @Test
    fun `GIVEN SessionState with blocked url schemes WHEN isUrlSchemeAllowed is called THEN it returns false if the url has that scheme`() {
        val engineSessionWithBlockedUrlScheme = Mockito.mock(EngineSession::class.java)
        doReturn(listOf("http")).`when`(engineSessionWithBlockedUrlScheme).getBlockedSchemes()
        val sessionState =
            TabSessionState(
                content = mock(),
                engineState = EngineState(engineSession = engineSessionWithBlockedUrlScheme),
            )

        assertFalse(sessionState.isUrlSchemeAllowed("http://mozilla.org"))
        assertFalse(sessionState.isUrlSchemeAllowed("hTtP://mozilla.org"))
        assertFalse(sessionState.isUrlSchemeAllowed("HttP://www.mozilla.org"))
        assertTrue(sessionState.isUrlSchemeAllowed("www.mozilla.org"))
        assertTrue(sessionState.isUrlSchemeAllowed("https://mozilla.org"))
        assertTrue(sessionState.isUrlSchemeAllowed("mozilla.org"))
        assertTrue(sessionState.isUrlSchemeAllowed("/mozilla.org"))
        assertTrue(sessionState.isUrlSchemeAllowed("content://http://mozilla.org"))
    }

    @Test
    fun `GIVEN SessionState with blocked url schemes WHEN isUrlSchemeAllowed is called THEN it returns true if the url does not have that scheme`() {
        val engineSessionWithBlockedUrlScheme = Mockito.mock(EngineSession::class.java)
        doReturn(listOf("http")).`when`(engineSessionWithBlockedUrlScheme).getBlockedSchemes()
        val sessionState =
            TabSessionState(
                content = mock(),
                engineState = EngineState(engineSession = engineSessionWithBlockedUrlScheme),
            )

        assertTrue(sessionState.isUrlSchemeAllowed("https://mozilla.org"))
    }

    @Test
    fun `GIVEN IMAGE_SRC with title combinations WHEN getTitleOrLink is called THEN it returns the title or link`() {
        HitResult.IMAGE_SRC("https://mozilla.org/image.png", "https://mozilla.org", "Image title").also { result ->
            assertEquals("https://mozilla.org\n\nImage title", result.getDisplayTitle())
        }

        HitResult.IMAGE_SRC("https://mozilla.org/image.png", "https://mozilla.org", " ").also { result ->
            assertEquals("https://mozilla.org", result.getDisplayTitle())
        }

        HitResult.IMAGE_SRC("https://mozilla.org/image.png", "https://mozilla.org").also { result ->
            assertEquals("https://mozilla.org", result.getDisplayTitle())
        }

        HitResult.IMAGE_SRC("https://mozilla.org/image.png", "https://mozilla.org", "Image title").also { result ->
            assertEquals("https://mozilla.org", result.getLink())
        }
    }
}

private class TestSnackbarDelegate : SnackbarDelegate {
    var hasShownSnackbar = false
    var lastActionListener: ((v: View) -> Unit)? = null

    override fun show(
        snackBarParentView: View,
        text: Int,
        subText: String?,
        subTextOverflow: TextOverflow?,
        duration: Int,
        isError: Boolean,
        action: Int,
        withDismissAction: Boolean,
        listener: ((v: View) -> Unit)?,
    ) {
        hasShownSnackbar = true
        lastActionListener = listener
    }

    override fun show(
        snackBarParentView: View,
        text: String,
        subText: String?,
        subTextOverflow: TextOverflow?,
        duration: Int,
        isError: Boolean,
        action: String?,
        withDismissAction: Boolean,
        listener: ((v: View) -> Unit)?,
    ) = show(snackBarParentView, 0, "", null, duration, isError, 0, false, listener)
}
