/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.usecases

import androidx.core.net.toUri
import io.mockk.every
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import mozilla.components.browser.state.action.ShareResourceAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.NativeShareSheet
import org.mozilla.fenix.components.share.ShareSheetChooserAction
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.components.usecases.fake.FakeShareSheetLauncher
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
class ShareUseCasesTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private lateinit var shareSheetLauncher: FakeShareSheetLauncher
    private lateinit var settings: Settings
    private lateinit var browserStore: BrowserStore
    private lateinit var shareUseCases: ShareUseCases
    private var navigatedToShareFragment = false

    private val navigateToShareFragment: () -> Unit = { navigatedToShareFragment = true }

    @Before
    fun setUp() {
        shareSheetLauncher = FakeShareSheetLauncher()
        settings =
            mockk(relaxed = true) {
                every { nativeShareSheetEnabled } returns true
            }
        browserStore = BrowserStore(BrowserState(tabs = listOf(createTab(url = "https://mozilla.org", id = "123"))))
        shareUseCases =
            ShareUseCases(
                browserStore = browserStore,
                shareSheetLauncher = shareSheetLauncher,
                settings = settings,
            )
        navigatedToShareFragment = false
    }

    @Test
    fun `GIVEN current tab is a PDF WHEN shareUrl is called THEN PDF share action is dispatched and telemetry is not recorded`() {
        val pdfTab =
            createTab(url = "https://mozilla.org/file.pdf", id = "pdf").let {
                it.copy(content = it.content.copy(isPdf = true))
            }
        val pdfStore = spyk(BrowserStore(BrowserState(tabs = listOf(pdfTab))))
        shareUseCases = ShareUseCases(pdfStore, shareSheetLauncher, settings)

        shareUseCases.shareUrl(
            id = "pdf",
            url = "https://mozilla.org/file.pdf",
            title = "PDF",
            source = ShareSource.BROWSER_MENU,
            navigateToShareFragment = navigateToShareFragment,
        )

        verify { pdfStore.dispatch(any<ShareResourceAction.AddShareAction>()) }
        assertTrue(shareSheetLauncher.urlShares.isEmpty())
        assertFalse(navigatedToShareFragment)
        assertNull(NativeShareSheet.shown.testGetValue())
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN a single url WHEN shareUrl is called THEN system share sheet is launched and telemetry is recorded`() {
        assertNull(NativeShareSheet.shown.testGetValue())

        shareUseCases.shareUrl(
            id = "123",
            url = "https://mozilla.org",
            title = "Mozilla",
            source = ShareSource.BROWSER_MENU,
            navigateToShareFragment = navigateToShareFragment,
        )

        assertEquals(
            listOf(
                FakeShareSheetLauncher.UrlShare(
                    id = "123",
                    longUrl = "https://mozilla.org",
                    title = "Mozilla",
                    isPrivate = false,
                    isCustomTab = false,
                    chooserActions =
                        listOf(
                            ShareSheetChooserAction.SAVE_PDF,
                            ShareSheetChooserAction.PRINT,
                            ShareSheetChooserAction.SEND_TO_DEVICES,
                            ShareSheetChooserAction.QR_CODE,
                        ),
                )
            ),
            shareSheetLauncher.urlShares,
        )
        assertFalse(navigatedToShareFragment)

        val events = NativeShareSheet.shown.testGetValue()
        assertEquals(1, events?.size)
        assertEquals("browser_menu", events?.single()?.extra?.get("source"))
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN text and subject WHEN shareUrl is called THEN they are forwarded to the system share sheet`() {
        shareUseCases.shareUrl(
            id = null,
            url = "https://mozilla.org",
            title = "Mozilla",
            source = ShareSource.DEEP_LINK,
            text = "Check this out",
            subject = "A subject",
            navigateToShareFragment = navigateToShareFragment,
        )

        assertEquals(
            listOf(
                FakeShareSheetLauncher.UrlShare(
                    id = null,
                    longUrl = "https://mozilla.org",
                    title = "Mozilla",
                    isPrivate = false,
                    isCustomTab = false,
                    text = "Check this out",
                    subject = "A subject",
                    chooserActions =
                        listOf(
                            ShareSheetChooserAction.SAVE_PDF,
                            ShareSheetChooserAction.PRINT,
                            ShareSheetChooserAction.SEND_TO_DEVICES,
                            ShareSheetChooserAction.QR_CODE,
                        ),
                )
            ),
            shareSheetLauncher.urlShares,
        )
        assertFalse(navigatedToShareFragment)
    }

    @Config(sdk = [34])
    @Test
    fun `GIVE CUSTOM_TAB_MENU source WHEN shareUrl is called THEN telemetry is recorded`() {
        assertNull(NativeShareSheet.shown.testGetValue())

        shareUseCases.shareUrl(
            id = "123",
            url = "https://mozilla.org",
            title = "Mozilla",
            source = ShareSource.CUSTOM_TAB_MENU,
            isCustomTab = true,
            navigateToShareFragment = navigateToShareFragment,
        )

        val events = NativeShareSheet.shown.testGetValue()
        assertEquals(1, events?.size)
        assertEquals("custom_tab_menu", events?.single()?.extra?.get("source"))
        assertFalse(navigatedToShareFragment)
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN native share sheet is disabled WHEN shareUrl is called THEN navigate to share fragment and telemetry is not recorded`() {
        every { settings.nativeShareSheetEnabled } returns false

        shareUseCases.shareUrl(
            id = "123",
            url = "https://mozilla.org",
            title = "Mozilla",
            source = ShareSource.BROWSER_MENU,
            navigateToShareFragment = navigateToShareFragment,
        )

        assertTrue(shareSheetLauncher.urlShares.isEmpty())
        assertTrue(navigatedToShareFragment)
        assertNull(NativeShareSheet.shown.testGetValue())
    }

    @Config(sdk = [33])
    @Test
    fun `GIVEN native share sheet is not supported WHEN shareUrl is called THEN navigate to share fragment and telemetry is not recorded`() {
        shareUseCases.shareUrl(
            id = "123",
            url = "https://mozilla.org",
            title = "Mozilla",
            source = ShareSource.BROWSER_MENU,
            navigateToShareFragment = navigateToShareFragment,
        )

        assertTrue(shareSheetLauncher.urlShares.isEmpty())
        assertTrue(navigatedToShareFragment)
        assertNull(NativeShareSheet.shown.testGetValue())
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN url is null WHEN shareUrl is called THEN navigate to share fragment and no telemetry`() {
        shareUseCases.shareUrl(
            id = "123",
            url = null,
            title = "Mozilla",
            source = ShareSource.BROWSER_MENU,
            navigateToShareFragment = navigateToShareFragment,
        )

        assertTrue(shareSheetLauncher.urlShares.isEmpty())
        assertTrue(navigatedToShareFragment)
        assertNull(NativeShareSheet.shown.testGetValue())
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN a list of share data and subject WHEN shareItems is called THEN system share sheet is launched`() {
        val items = listOf(ShareData(url = "https://mozilla.org", title = "Mozilla", private = false))

        assertNull(NativeShareSheet.shown.testGetValue())

        shareUseCases.shareItems(
            items = items,
            source = ShareSource.HOME,
            subject = "My collection",
            navigateToShareFragment = navigateToShareFragment,
        )

        assertEquals(
            listOf(
                FakeShareSheetLauncher.ItemsShare(
                    items = items,
                    isPrivate = false,
                    subject = "My collection",
                )
            ),
            shareSheetLauncher.itemsShares,
        )

        val events = NativeShareSheet.shown.testGetValue()
        assertEquals(1, events?.size)
        assertEquals("home", events?.single()?.extra?.get("source"))
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN chooserActions, a thumbnailUri and text WHEN shareItems is called THEN they are forwarded to the system share sheet`() {
        val items = listOf(ShareData(url = "https://mozilla.org", title = "Mozilla", private = false))
        val thumbnailUri = "content://thumbnail".toUri()

        shareUseCases.shareItems(
            items = items,
            source = ShareSource.TABS_TRAY,
            subject = "My tab group",
            chooserActions = ShareSheetChooserAction.tabChooserActions,
            thumbnailUri = thumbnailUri,
            text = "Check this out",
            navigateToShareFragment = navigateToShareFragment,
        )

        assertEquals(
            listOf(
                FakeShareSheetLauncher.ItemsShare(
                    items = items,
                    isPrivate = false,
                    subject = "My tab group",
                    chooserActions = ShareSheetChooserAction.tabChooserActions,
                    thumbnailUri = thumbnailUri,
                    text = "Check this out",
                )
            ),
            shareSheetLauncher.itemsShares,
        )
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN native share sheet is disabled WHEN shareItems is called THEN navigate to share fragment and telemetry is not recorded`() {
        every { settings.nativeShareSheetEnabled } returns false

        shareUseCases.shareItems(
            items = listOf(ShareData(url = "https://mozilla.org", private = false)),
            source = ShareSource.HOME,
            navigateToShareFragment = navigateToShareFragment,
        )

        assertTrue(shareSheetLauncher.itemsShares.isEmpty())
        assertTrue(navigatedToShareFragment)
        assertNull(NativeShareSheet.shown.testGetValue())
    }

    @Config(sdk = [33])
    @Test
    fun `GIVEN native share sheet is not supported WHEN shareItems is called THEN navigate to share fragment and telemetry is not recorded`() {
        shareUseCases.shareItems(
            items = listOf(ShareData(url = "https://mozilla.org", private = false)),
            source = ShareSource.HOME,
            navigateToShareFragment = navigateToShareFragment,
        )

        assertTrue(shareSheetLauncher.itemsShares.isEmpty())
        assertTrue(navigatedToShareFragment)
        assertNull(NativeShareSheet.shown.testGetValue())
    }
}
