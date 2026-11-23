/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions", "TooGenericExceptionCaught")

package org.mozilla.fenix.ui.robots

import android.content.Context
import android.net.Uri
import android.os.SystemClock
import android.util.Log
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.ComposeTimeoutException
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onLast
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performTouchInput
import androidx.core.net.toUri
import androidx.test.espresso.Espresso.closeSoftKeyboard
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.matcher.RootMatchers.isDialog
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.uiautomator.By
import androidx.test.uiautomator.By.text
import androidx.test.uiautomator.UiObject
import androidx.test.uiautomator.UiObjectNotFoundException
import androidx.test.uiautomator.UiSelector
import androidx.test.uiautomator.Until
import java.time.LocalDate
import mozilla.components.browser.errorpages.R as errorpagesR
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.toolbar.R as toolbarR
import mozilla.components.compose.base.R as composeBaseR
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_PROGRESSBAR
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_URL
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_URL_BOX
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.TABS_COUNTER
import mozilla.components.concept.engine.mediasession.MediaSession
import mozilla.components.feature.app.links.R as applinksR
import mozilla.components.feature.contextmenu.R as contextmenuR
import mozilla.components.feature.downloads.R as downloadsR
import mozilla.components.feature.prompts.R as promptsR
import mozilla.components.feature.summarize.R as summarizeR
import mozilla.components.lib.crash.store.CrashReportOption
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.mozilla.fenix.Config
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.AppAndSystemHelper.registerAndCleanupIdlingResources
import org.mozilla.fenix.helpers.Constants.RETRY_COUNT
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertItemIsChecked
import org.mozilla.fenix.helpers.MatcherHelper.assertItemTextEquals
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectIsGone
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithClassNameAndContainingDescription
import org.mozilla.fenix.helpers.MatcherHelper.itemWithDescription
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdAndText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.SessionLoadedIdlingResource
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeShort
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.openMainMenuAndAwaitBottomSheet
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.TestHelper.waitForAppWindowToBeUpdated
import org.mozilla.fenix.helpers.TestHelper.waitForObjects
import org.mozilla.fenix.helpers.click
import org.mozilla.fenix.helpers.ext.waitNotNull
import org.mozilla.fenix.home.ui.HomepageTestTag.HOMEPAGE
import org.mozilla.fenix.pdf.PdfToolsTestTag
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.tabstray.TabsTrayTestTag
import org.mozilla.fenix.webcompat.BrokenSiteReporterTestTags
import org.mozilla.fenix.webcompat.BrokenSiteReporterTestTags.BROKEN_SITE_REPORTER_CHOOSE_REASON_BUTTON
import org.mozilla.fenix.webcompat.BrokenSiteReporterTestTags.BROKEN_SITE_REPORTER_REASON_OPTION
import org.mozilla.fenix.webcompat.BrokenSiteReporterTestTags.BROKEN_SITE_REPORTER_SEND_BUTTON

class BrowserRobot(private val composeTestRule: ComposeTestRule) {
    private lateinit var sessionLoadedIdlingResource: SessionLoadedIdlingResource

    fun waitForPageToLoad(pageLoadWaitingTime: Long = waitingTime) {
        Log.i(TAG, "waitForPageToLoad: Waiting for page load to complete.")
        try {
            composeTestRule.waitUntilNodeCount(hasTestTag(ADDRESSBAR_PROGRESSBAR), 1, pageLoadWaitingTime)
        } catch (_: ComposeTimeoutException) {
            Log.i(TAG, "waitForPageToLoad: Progress bar never appeared; assuming page already loaded.")
            return
        }
        try {
            composeTestRule.waitUntil(pageLoadWaitingTime) {
                val node =
                    composeTestRule
                        .onAllNodesWithTag(ADDRESSBAR_PROGRESSBAR)
                        .fetchSemanticsNodes(atLeastOneRootRequired = false)
                        .firstOrNull() ?: return@waitUntil false
                val rangeInfo = node.config.getOrNull(SemanticsProperties.ProgressBarRangeInfo)
                rangeInfo != null && rangeInfo.current >= rangeInfo.range.endInclusive
            }
        } catch (_: ComposeTimeoutException) {
            Log.w(
                TAG,
                "waitForPageToLoad: Progress bar did not reach 100% within ${pageLoadWaitingTime}ms. " +
                    "Continuing; expected for crash/error pages, subsequent assertions will catch genuine load failures.",
            )
        }
    }

    fun verifyCurrentPrivateSession(context: Context) {
        val selectedTab = context.components.core.store.state.selectedTab
        Log.i(TAG, "verifyCurrentPrivateSession: Trying to verify that current browsing session is private")
        assertTrue("Current session is private", selectedTab?.content?.private ?: false)
        Log.i(TAG, "verifyCurrentPrivateSession: Verified that current browsing session is private")
    }

    fun verifyUrl(url: String) {
        Log.i(TAG, "verifyUrl: Trying to verify $url")

        val expectedText = url.replace("http://", "")
        val textMatcher = hasText(expectedText, substring = true, ignoreCase = true)
        try {
            composeTestRule.waitUntil(waitingTimeShort) {
                composeTestRule.onAllNodesWithTag(ADDRESSBAR_URL, useUnmergedTree = true).fetchSemanticsNodes().any {
                    textMatcher.matches(it)
                }
            }
        } catch (_: ComposeTimeoutException) {
            Log.i(TAG, "verifyUrl [$url] failed because: ")
            composeTestRule
                .onAllNodesWithTag(ADDRESSBAR_URL, useUnmergedTree = true)
                .fetchSemanticsNodes()
                .forEachIndexed { index, node ->
                    val text = node.config.getOrNull(SemanticsProperties.Text)?.joinToString("")
                    Log.i(TAG, "verifyUrl: Node[$index] with tag '$ADDRESSBAR_URL' has text: '$text'")
                }
        }
    }

    fun verifyHelpUrl() {
        try {
            verifyUrl("support.mozilla.org/en-US/products/mobile")
        } catch (e: AssertionError) {
            Log.i(TAG, "verifyHelpUrl: AssertionError caught, checking redirect URL")
            verifyUrl(
                SupportUtils.getSumoURLForTopic(
                        appContext,
                        SupportUtils.SumoTopic.HELP,
                    )
                    .replace("https://", "")
            )
        }
    }

    fun verifyWhatsNewURL() {
        try {
            verifyUrl("firefox.com/en-US/firefox/android/")
        } catch (e: AssertionError) {
            Log.i(TAG, "verifyWhatsNewURL: AssertionError caught, checking redirect URL")
            val redirectURL = SupportUtils.WHATS_NEW_URL.toUri()
            verifyUrl(redirectURL.authority?.removePrefix("www.") + redirectURL.encodedPath)
        }
    }

    fun verifyRateOnGooglePlayURL() {
        verifyUrl("play.google.com/store/apps/details?id=org.mozilla.fenix")
    }

    fun verifyCustomSearchEngineLearnMoreURL() {
        try {
            verifyUrl("support.mozilla.org/en-US/kb/manage-my-default-search-engines-firefox-android")
        } catch (e: AssertionError) {
            Log.i(TAG, "verifyCustomSearchEngineLearnMoreLink: AssertionError caught, checking redirect URL")
            verifyUrl(
                SupportUtils.getSumoURLForTopic(
                        appContext,
                        SupportUtils.SumoTopic.CUSTOM_SEARCH_ENGINES,
                    )
                    .replace("https://", "")
            )
        }
    }

    fun verifyETPLearnMoreURL() {
        // Get and log the URL in case there are failures in the future
        try {
            verifyUrl("support.mozilla.org/en-US/kb/tracking-protection-firefox-android")
        } catch (e: AssertionError) {
            Log.i(TAG, "verifyETPLearnMoreURL: AssertionError caught, checking redirect URL")
            verifyUrl("support.mozilla.org/en-US/kb/enhanced-tracking-protection-firefox-android")
        } catch (e: AssertionError) {
            verifyUrl(
                SupportUtils.getSumoURLForTopic(appContext, SupportUtils.SumoTopic.TOTAL_COOKIE_PROTECTION)
                    .replace("https://", "")
            )
        }
    }

    fun verifyCrossOriginStorageLearnMoreURL() {
        Log.i(TAG, "verifyCrossOriginStorageLearnMoreURL: Trying to verify cross origin storage URL")
        verifyUrl("docs/Web/API/Storage_Access_API")
        Log.i(TAG, "verifyCrossOriginStorageLearnMoreURL: Verified cross origin storage URL")
    }

    fun verifySponsoredShortcutsLearnMoreURL() {
        try {
            verifyUrl("support.mozilla.org/en-US/kb/sponsor-privacy")
        } catch (e: AssertionError) {
            Log.i(TAG, "verifySponsoredShortcutsURL: AssertionError caught, checking redirect URL")
            verifyUrl(
                SupportUtils.getSumoURLForTopic(appContext, SupportUtils.SumoTopic.SPONSOR_PRIVACY)
                    .replace("https://", "")
            )
        }
    }

    /* Asserts that the text within DOM element with ID="testContent" has the given text, i.e.
     *  document.querySelector('#testContent').innerText == expectedText
     *
     */
    fun verifyPageContent(expectedText: String) {
        mDevice.waitNotNull(
            Until.findObject(By.res("$packageName:id/engineView")),
            waitingTime,
        )

        mDevice.waitForIdle()

        for (i in 1..RETRY_COUNT) {
            Log.i(TAG, "verifyPageContent: Started try #$i")
            sessionLoadedIdlingResource = SessionLoadedIdlingResource()
            try {
                registerAndCleanupIdlingResources(sessionLoadedIdlingResource) {
                    mDevice.waitForIdle()
                    assertTrue(
                        itemWithResId("$packageName:id/engineView")
                            .getChild(UiSelector().textContains(expectedText))
                            .waitForExists(waitingTimeLong)
                    )
                }
                break
            } catch (e: AssertionError) {
                Log.i(TAG, "verifyPageContent: AssertionError caught on try #$i, reloading page")
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    browserScreen(composeTestRule) {}
                        .openThreeDotMenu {}
                        .clickRefreshButton {
                            waitForPageToLoad()
                        }
                }
            }
        }
    }

    fun verifyTheTranslationBannerIsDisplayed(translatedFrom: String, translatedTo: String) {
        Log.i(TAG, "verifyTheTranslationBannerIsDisplayed: Trying to verify the Translations banner is displayed.")
        composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.translation_toolbar_expand_action))
            .assertIsDisplayed()
        composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.translation_toolbar_close_action))
            .assertIsDisplayed()
        composeTestRule
            .onNodeWithText(
                getStringResource(R.string.translation_toolbar_translated_from_and_to, translatedFrom, translatedTo)
            )
            .assertIsDisplayed()
        Log.i(TAG, "verifyTheTranslationBannerIsDisplayed: Trying to verified the Translations banner is displayed.")
    }

    fun verifyDRMControlledContentPageContent(expectedText: String) {
        Log.i(
            TAG,
            "verifyDRMControlledContentPageContent: Trying to verify that the DRM controlled content is: $expectedText",
        )
        mDevice.findObject(By.textContains(expectedText))
        Log.i(TAG, "verifyDRMControlledContentPageContent: Verified that the DRM controlled content is: $expectedText")
    }

    fun verifyTextFragmentsPageContent(expectedText: String) {
        for (i in 1..RETRY_COUNT) {
            Log.i(TAG, "verifyTextFragmentsPageContent: Started try #$i")
            try {
                assertUIObjectExists(
                    itemWithResId("$packageName:id/engineView").getChild(UiSelector().textContains(expectedText))
                )

                break
            } catch (e: AssertionError) {
                Log.i(TAG, "verifyTextFragmentsPageContent: AssertionError caught on try #$i, reloading page")
                if (i == RETRY_COUNT) throw e
                browserScreen(composeTestRule) {}
                    .openThreeDotMenu {}
                    .clickRefreshButton {
                        waitForPageToLoad()
                    }
            }
        }
    }

    /**
     * Verifies the expected view of the tab crash reporter.
     *
     * @param crashReportOption - User's crash reporting setting, default is [CrashReportOption.Ask].
     */
    fun verifyTabCrashReporterView(crashReportOption: CrashReportOption = CrashReportOption.Ask) {
        mDevice.waitForIdle()
        for (i in 1..RETRY_COUNT) {
            Log.i(TAG, "verifyTabCrashReporterView: Started try #$i")
            try {
                assertUIObjectExists(itemWithResId("$packageName:id/crash_tab_image"), waitingTime = waitingTimeLong)
                assertUIObjectExists(itemWithText(getStringResource(R.string.tab_crash_title_2)))
                verifyCrashReportOption(crashReportOption)
                assertUIObjectExists(itemWithResId("$packageName:id/restoreTabButton"))
                assertUIObjectExists(itemWithResId("$packageName:id/closeTabButton"))

                break
            } catch (e: AssertionError) {
                Log.i(TAG, "verifyTabCrashReporterView: AssertionError caught, executing fallback methods")
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser("about:crashcontent".toUri()) {}
                }
            }
        }
    }

    /**
     * Toggles the "Send to Mozilla" checkbox in the tab crash reporter.
     *
     * @param expectedCheckedState The checkbox state expected after the toggle.
     */
    fun toggleTabCrashReporterCheckbox(expectedCheckedState: Boolean) {
        clickPageObject(composeTestRule, itemWithResId("$packageName:id/sendCrashCheckbox"))
        assertItemIsChecked(
            isChecked = expectedCheckedState,
            appItems = arrayOf(itemWithResId("$packageName:id/sendCrashCheckbox")),
        )
    }

    private fun verifyCrashReportOption(crashReportOption: CrashReportOption) {
        when (crashReportOption) {
            CrashReportOption.Auto -> {
                assertUIObjectExists(itemWithText(getStringResource(R.string.tab_crash_send_report)))
                assertItemIsChecked(
                    isChecked = true,
                    appItems = arrayOf(itemWithText(getStringResource(R.string.tab_crash_send_report))),
                )
            }
            CrashReportOption.Never -> {
                assertUIObjectIsGone(itemWithText(getStringResource(R.string.tab_crash_send_report)))
            }
            CrashReportOption.Ask -> {
                assertUIObjectExists(itemWithText(getStringResource(R.string.tab_crash_send_report)))
                assertItemIsChecked(
                    isChecked = true,
                    appItems = arrayOf(itemWithText(getStringResource(R.string.tab_crash_send_report))),
                )
            }
        }
    }

    // Verifies the information displayed on the about:cache page
    fun verifyNetworkCacheIsEmpty(storage: String) {
        val memorySection = mDevice.findObject(UiSelector().description(storage))

        val gridView =
            if (storage == "memory") {
                memorySection.getFromParent(UiSelector().className("android.widget.GridView").index(2))
            } else {
                memorySection.getFromParent(UiSelector().className("android.widget.GridView").index(4))
            }

        val cacheSizeInfo =
            gridView.getChild(UiSelector().text("Number of entries:")).getFromParent(UiSelector().text("0"))

        for (i in 1..RETRY_COUNT) {
            try {
                assertUIObjectExists(cacheSizeInfo)
                break
            } catch (e: AssertionError) {
                browserScreen(this@BrowserRobot.composeTestRule) {}.openThreeDotMenu {}.clickRefreshButton {}
            }
        }
    }

    fun verifyTabCounter(numberOfOpenTabs: String, isPrivateBrowsingEnabled: Boolean = false) {
        if (isPrivateBrowsingEnabled) {
            Log.i(TAG, "verifyTabCounter: Trying to verify that the number of open private tabs is : $numberOfOpenTabs")
            composeTestRule
                .onNodeWithContentDescription("Private Tabs Open: $numberOfOpenTabs. Tap to switch tabs.")
                .assertIsDisplayed()
            Log.i(TAG, "verifyTabCounter: Verified that the number of open private tabs is : $numberOfOpenTabs")
        } else {
            Log.i(TAG, "verifyTabCounter: Trying to verify that the number of open tabs is : $numberOfOpenTabs")
            composeTestRule
                .onNodeWithContentDescription("Non-private Tabs Open: $numberOfOpenTabs. Tap to switch tabs.")
                .assertIsDisplayed()
            Log.i(TAG, "verifyTabCounter: Verified that the number of open tabs is : $numberOfOpenTabs")
        }
    }

    fun verifyContextMenuForLocalHostLinks(containsURL: Uri) {
        // If the link is directing to another local asset the "Download link" option is not available
        // If the link is not re-directing to an external app the "Open link in external app" option is not available
        assertUIObjectExists(
            contextMenuLinkUrl(containsURL.toString()),
            contextMenuOpenLinkInNewTab(),
            contextMenuOpenLinkInPrivateTab(),
            contextMenuCopyLink(),
            contextMenuShareLink(),
        )
    }

    fun verifyContextMenuForLinksToOtherApps(containsURL: String) {
        // If the link is re-directing to an external app the "Open link in external app" option is available
        // If the link is not directing to another local asset the "Download link" option is not available
        assertUIObjectExists(
            contextMenuLinkUrl(containsURL),
            contextMenuOpenLinkInNewTab(),
            contextMenuOpenLinkInPrivateTab(),
            contextMenuCopyLink(),
            contextMenuDownloadLink(),
            contextMenuShareLink(),
            contextMenuOpenInExternalApp(),
        )
    }

    fun verifyContextMenuForLinksToOtherHosts(containsURL: Uri) {
        // If the link is re-directing to another host the "Download link" option is available
        // If the link is not re-directing to an external app the "Open link in external app" option is not available
        assertUIObjectExists(
            contextMenuLinkUrl(containsURL.toString()),
            contextMenuOpenLinkInNewTab(),
            contextMenuOpenLinkInPrivateTab(),
            contextMenuCopyLink(),
            contextMenuDownloadLink(),
            contextMenuShareLink(),
        )
    }

    fun verifyLinkImageContextMenuItems(containsURL: Uri) {
        mDevice.waitNotNull(Until.findObject(By.textContains(containsURL.toString())))
        mDevice.waitNotNull(
            Until.findObject(text("Open link in new tab")),
            waitingTime,
        )
        mDevice.waitNotNull(
            Until.findObject(text("Open link in private tab")),
            waitingTime,
        )
        mDevice.waitNotNull(Until.findObject(text("Copy link")), waitingTime)
        mDevice.waitNotNull(Until.findObject(text("Share link")), waitingTime)
        mDevice.waitNotNull(
            Until.findObject(text("Open image in new tab")),
            waitingTime,
        )
        mDevice.waitNotNull(Until.findObject(text("Save image")), waitingTime)
        mDevice.waitNotNull(
            Until.findObject(text("Copy image location")),
            waitingTime,
        )
    }

    fun verifyTabHistorySheetIsDisplayed(isDisplayed: Boolean) {
        assertUIObjectExists(
            itemWithResId("$packageName:id/tabHistoryRecyclerView"),
            exists = isDisplayed,
        )
    }

    fun verifyTabHistoryContainsWebsite(websiteUrl: String, isDisplayed: Boolean) {
        Log.i(TAG, "verifyTabHistoryContainsWebsite: Trying to verify that tab history contains website: $websiteUrl")
        val historyList = itemWithResId("$packageName:id/tabHistoryRecyclerView")
        val websiteItem =
            historyList.getChild(
                // selector =
                UiSelector().resourceId("$packageName:id/site_list_item").childSelector(UiSelector().text(websiteUrl))
            )
        assertUIObjectExists(
            websiteItem,
            exists = isDisplayed,
        )
        Log.i(TAG, "verifyTabHistoryContainsWebsite: Verified that the tab history contains website: $websiteUrl")
    }

    fun verifyNavURLBarHidden() = assertUIObjectIsGone(navURLBar())

    fun verifyMenuButton() {
        Log.i(TAG, "verifyMenuButton: Trying to verify main menu button is displayed")
        composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.content_description_menu))
            .assertIsDisplayed()
        Log.i(TAG, "verifyMenuButton: Verified main menu button is displayed")
    }

    fun verifyNoLinkImageContextMenuItems(containsURL: Uri) {
        mDevice.waitNotNull(Until.findObject(By.textContains(containsURL.toString())))
        mDevice.waitNotNull(
            Until.findObject(text("Open image in new tab")),
            waitingTime,
        )
        mDevice.waitNotNull(Until.findObject(text("Save image")), waitingTime)
        mDevice.waitNotNull(
            Until.findObject(text("Copy image location")),
            waitingTime,
        )
    }

    fun verifyNotificationDotOnMainMenu() = assertUIObjectExists(itemWithResId("$packageName:id/notification_dot"))

    fun dismissContentContextMenu() {
        Log.i(TAG, "dismissContentContextMenu: Trying to click device back button")
        mDevice.pressBack()
        Log.i(TAG, "dismissContentContextMenu: Clicked device back button")
        assertUIObjectExists(itemWithResId("$packageName:id/engineView"))
    }

    fun createBookmark(url: Uri, folder: String? = null) {
        navigationToolbar(this@BrowserRobot.composeTestRule) {}
            .enterURLAndEnterToBrowser(url) {
                // needs to wait for the right url to load before saving a bookmark
                verifyUrl(url.toString())
            }
            .openThreeDotMenu {}
            .clickBookmarkThisPageButton {}
            .takeIf { !folder.isNullOrBlank() }
            ?.let {
                it.openThreeDotMenu {}
                    .clickEditBookmarkButton {
                        setParentFolder(folder!!)
                        navigateUp()
                    }
            }
    }

    fun longClickPDFImage() =
        longClickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("pdfjs_internal_id_13R"))

    fun verifyPDFReaderToolbarItems() {
        // Bug 2081247 to switch to new toolbar for testing
        if (!Config.channel.isNightlyOrDebug) {
            assertUIObjectExists(itemWithResIdContainingText("download", "Download"))
            return
        }

        Log.i(TAG, "verifyPDFReaderToolbarItems: Waiting for $waitingTime until the PDF tools bar exists")
        composeTestRule.waitUntilAtLeastOneExists(hasTestTag(PdfToolsTestTag.BAR), waitingTime)
        Log.i(TAG, "verifyPDFReaderToolbarItems: Waited for $waitingTime until the PDF tools bar exists")
        listOf(
                PdfToolsTestTag.DOWNLOAD_BUTTON,
                PdfToolsTestTag.PRINT_BUTTON,
                PdfToolsTestTag.SHARE_BUTTON,
            )
            .forEach { testTag ->
                Log.i(TAG, "verifyPDFReaderToolbarItems: Trying to verify that $testTag is displayed")
                composeTestRule.onNodeWithTag(testTag).assertIsDisplayed()
                Log.i(TAG, "verifyPDFReaderToolbarItems: Verified that $testTag is displayed")
            }
    }

    fun clickSubmitLoginButton() {
        clickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("submit"))
        assertUIObjectIsGone(itemWithResId("submit"))
        Log.i(TAG, "clickSubmitLoginButton: Waiting for device to be idle for $waitingTimeLong ms")
        mDevice.waitForIdle(waitingTimeLong)
        Log.i(TAG, "clickSubmitLoginButton: Waited for device to be idle for $waitingTimeLong ms")
    }

    fun enterPassword(password: String) {
        clickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("password"))
        setPageObjectText(this@BrowserRobot.composeTestRule, itemWithResId("password"), password)

        assertUIObjectIsGone(itemWithText(password))
    }

    /**
     * Get the current playback state of the currently selected tab. The result may be null if there if the currently
     * playing media tab cannot be found in [store]
     *
     * @param store [BrowserStore] from which to get data about the current tab's state.
     * @return nullable [MediaSession.PlaybackState] indicating the media playback state for the current tab.
     */
    private fun getCurrentPlaybackState(store: BrowserStore): MediaSession.PlaybackState? {
        return store.state.selectedTab?.mediaSessionState?.playbackState
    }

    /**
     * Asserts that in [waitingTime] the playback state of the current tab will be [expectedState], failing the
     * assertion otherwise.
     *
     * @param store [BrowserStore] from which to get data about the current tab's state.
     * @param expectedState [MediaSession.PlaybackState] the playback state that will be asserted
     */
    fun assertPlaybackState(store: BrowserStore, expectedState: MediaSession.PlaybackState) {
        val startMills = SystemClock.uptimeMillis()
        var currentMills: Long = 0
        while (currentMills <= waitingTime) {
            if (expectedState == getCurrentPlaybackState(store)) return
            currentMills = SystemClock.uptimeMillis() - startMills
        }
        fail("Playback did not moved to state: $expectedState")
    }

    fun swipeNavBarRight(tabUrl: String) {
        // failing to swipe on Firebase sometimes, so it tries again
        try {
            Log.i(TAG, "swipeNavBarRight: Try block")
            Log.i(TAG, "swipeNavBarRight: Trying to perform swipe right action on navigation toolbar")
            itemWithResId("$packageName:id/composable_toolbar").swipeRight(2)
            Log.i(TAG, "swipeNavBarRight: Performed swipe right action on navigation toolbar")
            assertUIObjectIsGone(itemWithText(tabUrl))
        } catch (e: AssertionError) {
            Log.i(TAG, "swipeNavBarRight: AssertionError caught, executing fallback methods")
            Log.i(TAG, "swipeNavBarRight: Trying to perform swipe right action on navigation toolbar")
            itemWithResId("$packageName:id/composable_toolbar").swipeRight(2)
            Log.i(TAG, "swipeNavBarRight: Performed swipe right action on navigation toolbar")
            assertUIObjectIsGone(itemWithText(tabUrl))
        }
    }

    fun swipeNavBarLeft(tabUrl: String) {
        // failing to swipe on Firebase sometimes, so it tries again
        try {
            Log.i(TAG, "swipeNavBarLeft: Try block")
            Log.i(TAG, "swipeNavBarLeft: Trying to perform swipe left action on navigation toolbar")
            itemWithResId("$packageName:id/composable_toolbar").swipeLeft(2)
            Log.i(TAG, "swipeNavBarLeft: Performed swipe left action on navigation toolbar")
            assertUIObjectIsGone(itemWithText(tabUrl))
        } catch (e: AssertionError) {
            Log.i(TAG, "swipeNavBarLeft: AssertionError caught, executing fallback methods")
            Log.i(TAG, "swipeNavBarLeft: Trying to perform swipe left action on navigation toolbar")
            itemWithResId("$packageName:id/composable_toolbar").swipeLeft(2)
            Log.i(TAG, "swipeNavBarLeft: Performed swipe left action on navigation toolbar")
            assertUIObjectIsGone(itemWithText(tabUrl))
        }
    }

    fun clickSuggestedLoginsButton() {
        for (i in 1..RETRY_COUNT) {
            try {
                Log.i(TAG, "clickSuggestedLoginsButton: Started try #$i")
                mDevice.waitForObjects(suggestedLogins())
                Log.i(TAG, "clickSuggestedLoginsButton: Trying to click suggested logins button")
                suggestedLogins().click()
                Log.i(TAG, "clickSuggestedLoginsButton: Clicked suggested logins button")
                mDevice.waitForObjects(suggestedLogins())
                break
            } catch (e: UiObjectNotFoundException) {
                Log.i(TAG, "clickSuggestedLoginsButton: UiObjectNotFoundException caught, executing fallback methods")
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    clickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("username"))
                }
            }
        }
    }

    fun clickSuggestedLogin(userName: String) {
        Log.i(TAG, "clickSuggestedLogin: Waiting for the suggested user name: $userName to exist")
        this@BrowserRobot.composeTestRule.waitUntilAtLeastOneExists(hasText(userName))
        Log.i(TAG, "clickSuggestedLogin: Waited for the suggested user name: $userName to exist")
        Log.i(TAG, "clickSuggestedLogin: Trying to click $userName login suggestion")
        this@BrowserRobot.composeTestRule.onNodeWithText(userName).performClick()
        Log.i(TAG, "clickSuggestedLogin: Clicked $userName login suggestion")
    }

    fun setTextForApartmentTextBox(apartment: String) {
        Log.i(TAG, "setTextForApartmentTextBox: Trying to set the text for the apartment text box to: $apartment")
        itemWithResId("apartment").setText(apartment)
        Log.i(TAG, "setTextForApartmentTextBox: The text for the apartment text box was set to: $apartment")
    }

    fun clearAddressForm() {
        clearTextFieldItem(itemWithResId("streetAddress"))
        clearTextFieldItem(itemWithResId("city"))
        clearTextFieldItem(itemWithResId("country"))
        clearTextFieldItem(itemWithResId("zipCode"))
        clearTextFieldItem(itemWithResId("telephone"))
        clearTextFieldItem(itemWithResId("email"))
    }

    fun clickSelectAddressButton() {
        for (i in 1..RETRY_COUNT) {
            try {
                Log.i(TAG, "clickSelectAddressButton: Started try #$i")
                assertUIObjectExists(selectAddressButton())
                Log.i(
                    TAG,
                    "clickSelectAddressButton: Trying to click the select address button and wait for $waitingTime ms for a new window",
                )
                selectAddressButton().clickAndWaitForNewWindow(waitingTime)
                Log.i(
                    TAG,
                    "clickSelectAddressButton: Clicked the select address button and waited for $waitingTime ms for a new window",
                )

                break
            } catch (e: AssertionError) {
                Log.i(TAG, "clickSelectAddressButton: AssertionError caught, executing fallback methods")
                // Retrying to trigger the prompt, in case we hit https://bugzilla.mozilla.org/show_bug.cgi?id=1816869
                // This should be removed when the bug is fixed.
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    clickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("city"))
                    clickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("country"))
                }
            }
        }
    }

    fun clickAddressFormFieldAndVerifyAutofillSuggestionExists() {
        for (i in 1..RETRY_COUNT) {
            try {
                Log.i(TAG, "clickAddressFormFieldAndVerifyAutofillSuggestionExists: Started try #$i")
                clickPageObject(composeTestRule, itemWithResId("streetAddress"))
                assertUIObjectExists(selectAddressButton())
                break
            } catch (e: AssertionError) {
                Log.i(TAG, "clickAddressFormFieldAndVerifyAutofillSuggestionExists: AssertionError caught on try #$i")
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    // De-focus streetAddress first so the next click re-triggers Gecko autofill
                    clickPageObject(composeTestRule, itemWithResId("city"))
                }
            }
        }
    }

    fun verifySelectAddressButtonExists(exists: Boolean) = assertUIObjectExists(selectAddressButton(), exists = exists)

    fun changeCreditCardExpiryDate(expiryDate: String) {
        Log.i(TAG, "changeCreditCardExpiryDate: Trying to set credit card expiry date to: $expiryDate")
        itemWithResId("expiryMonthAndYear").setText(expiryDate)
        Log.i(TAG, "changeCreditCardExpiryDate: Credit card expiry date was set to: $expiryDate")
    }

    fun clickCreditCardNumberTextBox() {
        Log.i(
            TAG,
            "clickCreditCardNumberTextBox: Waiting for $waitingTime ms until finding the credit card number text box",
        )
        mDevice.wait(Until.findObject(By.res("cardNumber")), waitingTime)
        Log.i(
            TAG,
            "clickCreditCardNumberTextBox: Waited for $waitingTime ms until the credit card number text box was found",
        )
        Log.i(TAG, "clickCreditCardNumberTextBox: Trying to click the credit card number text box")
        mDevice.findObject(By.res("cardNumber")).click()
        Log.i(TAG, "clickCreditCardNumberTextBox: Clicked the credit card number text box")
        Log.i(TAG, "clickCreditCardNumberTextBox: Waiting for $waitingTimeShort ms for $appName window to be updated")
        mDevice.waitForWindowUpdate(appName, waitingTimeShort)
        Log.i(TAG, "clickCreditCardNumberTextBox: Waited for $waitingTimeShort ms for $appName window to be updated")
    }

    fun clickCreditCardFormSubmitButton() {
        Log.i(
            TAG,
            "clickCreditCardFormSubmitButton: Trying to click the credit card form submit button and wait for $waitingTime ms for a new window",
        )
        itemWithResId("submit").clickAndWaitForNewWindow(waitingTime)
        Log.i(
            TAG,
            "clickCreditCardFormSubmitButton: Clicked the credit card form submit button and waited for $waitingTime ms for a new window",
        )
    }

    fun fillAndSaveCreditCard(cardNumber: String, cardName: String, expiryMonthAndYear: String) {
        Log.i(TAG, "fillAndSaveCreditCard: Tying to set credit card number to: $cardNumber")
        itemWithResId("cardNumber").setText(cardNumber)
        Log.i(TAG, "fillAndSaveCreditCard: Credit card number was set to: $cardNumber")
        waitForAppWindowToBeUpdated()
        Log.i(TAG, "fillAndSaveCreditCard: Trying to set credit card name to: $cardName")
        itemWithResId("nameOnCard").setText(cardName)
        Log.i(TAG, "fillAndSaveCreditCard: Credit card name was set to: $cardName")
        waitForAppWindowToBeUpdated()
        Log.i(TAG, "fillAndSaveCreditCard: Trying to set credit card expiry month and year to: $expiryMonthAndYear")
        itemWithResId("expiryMonthAndYear").setText(expiryMonthAndYear)
        Log.i(TAG, "fillAndSaveCreditCard: Credit card expiry month and year were set to: $expiryMonthAndYear")
        waitForAppWindowToBeUpdated()
        Log.i(
            TAG,
            "fillAndSaveCreditCard: Trying to click the credit card form submit button and wait for $waitingTime ms for a new window",
        )
        itemWithResId("submit").clickAndWaitForNewWindow(waitingTime)
        Log.i(
            TAG,
            "fillAndSaveCreditCard: Clicked the credit card form submit button and waited for $waitingTime ms for a new window",
        )
        waitForPageToLoad()
        Log.i(TAG, "fillAndSaveCreditCard: Waiting for $waitingTime ms for $packageName window to be updated")
        mDevice.waitForWindowUpdate(packageName, waitingTime)
        Log.i(TAG, "fillAndSaveCreditCard: Waited for $waitingTime ms for $packageName window to be updated")
    }

    fun clickNegativeSaveCreditCardPromptButton() = onView(withId(promptsR.id.save_cancel)).inRoot(isDialog()).click()

    fun verifyUpdateOrSaveCreditCardPromptExists(exists: Boolean) =
        assertUIObjectExists(
            itemWithResId("$packageName:id/save_credit_card_header"),
            exists = exists,
        )

    fun verifySelectCreditCardPromptExists(exists: Boolean) =
        assertUIObjectExists(selectCreditCardButton(), exists = exists)

    fun verifyCreditCardSuggestion(vararg creditCardNumbers: String) {
        for (creditCardNumber in creditCardNumbers) {
            assertUIObjectExists(
                itemWithResIdContainingText(
                    "$packageName:id/credit_card_number",
                    creditCardNumber,
                )
            )
        }
    }

    fun verifySuggestedUserName(userName: String) {
        Log.i(TAG, "verifySuggestedUserName: Waiting for the suggested user name: $userName to exist")
        this@BrowserRobot.composeTestRule.waitUntilAtLeastOneExists(hasText(userName))
        Log.i(TAG, "verifySuggestedUserName: Waited for the suggested user name: $userName to exist")
        Log.i(TAG, "verifySuggestedUserName: Trying to assert that user name: $userName exists")
        this@BrowserRobot.composeTestRule.onNodeWithText(userName).assertExists()
        Log.i(TAG, "verifySuggestedUserName: Asserted that user name: $userName exists")
    }

    fun verifyPrefilledLoginCredentials(userName: String, password: String, credentialsArePrefilled: Boolean) {
        // Sometimes the assertion of the pre-filled logins fails so we are re-trying after refreshing the page
        for (i in 1..RETRY_COUNT) {
            try {
                Log.i(TAG, "verifyPrefilledLoginCredentials: Started try #$i")
                mDevice.waitForObjects(itemWithResId("username"))
                assertItemTextEquals(
                    itemWithResId("username"),
                    expectedText = userName,
                    isEqual = credentialsArePrefilled,
                )
                mDevice.waitForObjects(itemWithResId("password"))
                assertItemTextEquals(
                    itemWithResId("password"),
                    expectedText = password,
                    isEqual = credentialsArePrefilled,
                )

                break
            } catch (e: AssertionError) {
                Log.i(TAG, "verifyPrefilledLoginCredentials: AssertionError caught, executing fallback methods")
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    browserScreen(this@BrowserRobot.composeTestRule) {}
                        .openThreeDotMenu {}
                        .clickRefreshButton {
                            clearTextFieldItem(itemWithResId("username"))
                            clickSuggestedLoginsButton()
                            verifySuggestedUserName(userName)
                            clickSuggestedLogin(userName)
                            clickPageObject(this.composeTestRule, itemWithResId("togglePassword"))
                        }
                }
            }
        }
    }

    fun verifyAutofilledAddress(streetAddress: String) {
        mDevice.waitForObjects(itemWithResIdAndText("streetAddress", streetAddress))
        assertUIObjectExists(itemWithResIdAndText("streetAddress", streetAddress))
    }

    fun verifyManuallyFilledAddress(apartment: String) {
        mDevice.waitForObjects(itemWithResIdAndText("apartment", apartment))
        assertUIObjectExists(itemWithResIdAndText("apartment", apartment))
    }

    fun verifyAutofilledCreditCard(creditCardNumber: String) {
        mDevice.waitForObjects(itemWithResIdAndText("cardNumber", creditCardNumber))
        assertUIObjectExists(itemWithResIdAndText("cardNumber", creditCardNumber))
    }

    fun verifySaveLoginPromptIsDisplayed() =
        assertUIObjectExists(itemWithResId("$packageName:id/feature_prompt_login_fragment"))

    fun verifySaveLoginPromptIsNotDisplayed() =
        assertUIObjectExists(
            itemWithResId("$packageName:id/feature_prompt_login_fragment"),
            exists = false,
        )

    fun verifyTrackingProtectionWebContent(state: String) {
        for (i in 1..RETRY_COUNT) {
            try {
                Log.i(TAG, "verifyTrackingProtectionWebContent: Started try #$i")
                assertUIObjectExists(itemContainingText(state), waitingTime = waitingTimeLong)

                break
            } catch (e: AssertionError) {
                Log.i(TAG, "verifyTrackingProtectionWebContent: AssertionError caught, executing fallback methods")
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    Log.e(TAG, "On try $i, trackers are not: $state")

                    browserScreen(composeTestRule) {}.openThreeDotMenu {}.clickRefreshButton {}
                }
            }
        }
    }

    fun selectTime(hour: Int, minute: Int) {
        Log.i(TAG, "selectTime: Trying to select time picker hour: $hour and minute: $minute AM")
        itemWithClassNameAndContainingDescription(
                "android.widget.TextView",
                "$hour o'clock",
            )
            .click()
        waitForAppWindowToBeUpdated()
        itemWithClassNameAndContainingDescription(
                "android.widget.TextView",
                "$minute minutes",
            )
            .click()
        itemWithResIdContainingText(
                "$packageName:id/material_clock_period_am_button",
                "AM",
            )
            .click()
        Log.i(TAG, "selectTime: Selected time picker hour: $hour and minute: $minute AM")
    }

    fun verifySelectedDate() {
        val currentDate = LocalDate.now()
        val currentDay = currentDate.dayOfMonth
        val currentMonth = currentDate.month
        val currentYear = currentDate.year

        for (i in 1..RETRY_COUNT) {
            try {
                Log.i(TAG, "verifySelectedDate: Started try #$i")
                assertUIObjectExists(itemContainingText("Selected date is: $currentDate"))

                break
            } catch (e: AssertionError) {
                Log.i(TAG, "verifySelectedDate: AssertionError caught, executing fallback methods")
                Log.e(TAG, "Selected time isn't displayed ${e.localizedMessage}")

                clickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("calendar"))
                clickPageObject(
                    this@BrowserRobot.composeTestRule,
                    itemWithDescription("$currentDay $currentMonth $currentYear"),
                )
                clickPageObject(this@BrowserRobot.composeTestRule, itemContainingText("OK"))
                clickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("submitDate"))
            }
        }

        assertUIObjectExists(itemContainingText("Selected date is: $currentDate"))
    }

    fun verifyNoDateIsSelected() {
        val currentDate = LocalDate.now()
        assertTrue(itemContainingText("Selected date is: $currentDate").waitUntilGone(waitingTime))
    }

    fun verifySelectedTime(hour: Int, minute: Int) {
        for (i in 1..RETRY_COUNT) {
            try {
                Log.i(TAG, "verifySelectedTime: Started try #$i")
                assertUIObjectExists(itemContainingText("Selected time is: $hour:$minute"))

                break
            } catch (e: AssertionError) {
                Log.i(TAG, "verifySelectedTime: AssertionError caught, executing fallback methods")
                Log.e(TAG, "Selected time isn't displayed ${e.localizedMessage}")

                clickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("clock"))
                clickPageObject(this@BrowserRobot.composeTestRule, itemContainingText("CLEAR"))
                clickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("clock"))
                selectTime(hour, minute)
                clickPageObject(this@BrowserRobot.composeTestRule, itemContainingText("OK"))
                clickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("submitTime"))
            }
        }
        assertUIObjectExists(itemContainingText("Selected time is: $hour:$minute"))
    }

    fun verifySelectedColor(hexValue: String) {
        for (i in 1..RETRY_COUNT) {
            try {
                Log.i(TAG, "verifySelectedColor: Started try #$i")
                assertUIObjectExists(itemContainingText("Selected color is: $hexValue"))

                break
            } catch (e: AssertionError) {
                Log.i(TAG, "verifySelectedColor: AssertionError caught, executing fallback methods")
                Log.e(TAG, "Selected color isn't displayed ${e.localizedMessage}")

                clickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("colorPicker"))
                clickPageObject(this@BrowserRobot.composeTestRule, itemWithDescription(hexValue))
                clickPageObject(this@BrowserRobot.composeTestRule, itemContainingText("SET"))
                clickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("submitColor"))
            }
        }

        assertUIObjectExists(itemContainingText("Selected color is: $hexValue"))
    }

    fun verifySelectedDropDownOption(optionName: String) {
        for (i in 1..RETRY_COUNT) {
            try {
                Log.i(TAG, "verifySelectedDropDownOption: Started try #$i")
                Log.i(
                    TAG,
                    "verifySelectedDropDownOption: Waiting for $waitingTime ms for \"Submit drop down option\" form button to exist",
                )
                mDevice
                    .findObject(UiSelector().textContains("Submit drop down option").resourceId("submitOption"))
                    .waitForExists(waitingTime)
                Log.i(
                    TAG,
                    "verifySelectedDropDownOption: Waited for $waitingTime ms for \"Submit drop down option\" form button to exist",
                )
                assertUIObjectExists(itemContainingText("Selected option is: $optionName"))

                break
            } catch (e: AssertionError) {
                Log.i(TAG, "verifySelectedDropDownOption: AssertionError caught, executing fallback methods")
                Log.e(TAG, "Selected option isn't displayed ${e.localizedMessage}")

                clickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("dropDown"))
                clickPageObject(this@BrowserRobot.composeTestRule, itemContainingText(optionName))
                clickPageObject(this@BrowserRobot.composeTestRule, itemWithResId("submitOption"))
            }
        }

        assertUIObjectExists(itemContainingText("Selected option is: $optionName"))
    }

    fun verifyNoTimeIsSelected(hour: Int, minute: Int) =
        assertUIObjectExists(itemContainingText("Selected date is: $hour:$minute"), exists = false)

    fun verifyColorIsNotSelected(hexValue: String) =
        assertUIObjectExists(itemContainingText("Selected date is: $hexValue"), exists = false)

    fun verifyOpenLinkInAnotherAppPrompt(appName: String) {
        assertUIObjectExists(
            itemContainingText(
                getStringResource(
                    applinksR.string.mozac_feature_applinks_normal_confirm_dialog_title_with_app_name_2,
                    appName,
                )
            ),
            waitingTime = waitingTimeLong,
        )
    }

    fun verifyOpenLinkInAnotherAppPromptIsNotShown() {
        assertUIObjectIsGone(
            itemContainingText(
                getStringResource(
                    applinksR.string.mozac_feature_applinks_normal_confirm_dialog_title_with_app_name_2,
                    appName,
                )
            )
        )
    }

    fun verifyAppLinksPromptCheckbox(exists: Boolean = true) {
        assertUIObjectExists(
            itemContainingText(
                getStringResource(applinksR.string.mozac_feature_applinks_confirm_dialog_checkbox_label)
            ),
            exists = exists,
        )
    }

    fun verifyPrivateBrowsingOpenLinkInAnotherAppPrompt(appName: String, url: String, pageObject: UiObject) {
        for (i in 1..RETRY_COUNT) {
            try {
                Log.i(TAG, "verifyPrivateBrowsingOpenLinkInAnotherAppPrompt: Started try #$i")
                assertUIObjectExists(
                    itemContainingText(
                        getStringResource(
                            applinksR.string.mozac_feature_applinks_normal_confirm_dialog_title_with_app_name_2,
                            appName,
                        )
                    ),
                    itemContainingText(url),
                )

                break
            } catch (e: AssertionError) {
                Log.i(
                    TAG,
                    "verifyPrivateBrowsingOpenLinkInAnotherAppPrompt: AssertionError caught, executing fallback methods",
                )
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    browserScreen(this@BrowserRobot.composeTestRule) {}
                        .openThreeDotMenu {}
                        .clickRefreshButton {
                            waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
                            clickPageObject(this.composeTestRule, pageObject)
                        }
                }
            }
        }
    }

    fun verifyFindInPageBar(exists: Boolean) =
        assertUIObjectExists(
            itemWithResId("$packageName:id/findInPageView"),
            exists = exists,
        )

    fun verifyConnectionErrorMessage() =
        assertUIObjectExists(
            itemContainingText(getStringResource(errorpagesR.string.mozac_browser_errorpages_connection_failure_title)),
            itemWithResId("errorTryAgain"),
        )

    fun verifyAddressNotFoundErrorMessage() =
        assertUIObjectExists(
            itemContainingText(getStringResource(errorpagesR.string.mozac_browser_errorpages_unknown_host_title)),
            itemWithResId("errorTryAgain"),
        )

    fun verifyNoInternetConnectionErrorMessage() =
        assertUIObjectExists(
            itemContainingText(getStringResource(errorpagesR.string.mozac_browser_errorpages_no_internet_title_2)),
            itemWithResId("errorTryAgain"),
        )

    fun verifyOpenLinksInAppsCFRExists(exists: Boolean) {
        for (i in 1..RETRY_COUNT) {
            try {
                Log.i(TAG, "verifyOpenLinksInAppsCFRExists: Started try #$i")
                assertUIObjectExists(
                    itemWithResId("$packageName:id/banner_container"),
                    itemWithResIdContainingText(
                        "$packageName:id/banner_info_message",
                        getStringResource(R.string.open_in_app_cfr_info_message_2),
                    ),
                    itemWithResIdContainingText(
                        "$packageName:id/dismiss",
                        getStringResource(R.string.open_in_app_cfr_negative_button_text),
                    ),
                    itemWithResIdContainingText(
                        "$packageName:id/action",
                        getStringResource(R.string.open_in_app_cfr_positive_button_text),
                    ),
                    exists = exists,
                )
            } catch (e: AssertionError) {
                Log.i(TAG, "verifyOpenLinksInAppsCFRExists: AssertionError caught, executing fallback methods")
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    browserScreen(composeTestRule) {}
                        .openThreeDotMenu {}
                        .clickRefreshButton {
                            waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
                        }
                }
            }
        }
    }

    fun verifySurveyButtonDoesNotExist() =
        assertUIObjectIsGone(itemWithText(getStringResource(R.string.preferences_take_survey)))

    fun clickOpenLinksInAppsDismissCFRButton() {
        Log.i(
            TAG,
            "clickOpenLinksInAppsDismissCFRButton: Trying to click the open links in apps banner \"Dismiss\" button",
        )
        itemWithResIdContainingText(
                "$packageName:id/dismiss",
                getStringResource(R.string.open_in_app_cfr_negative_button_text),
            )
            .click()
        Log.i(TAG, "clickOpenLinksInAppsDismissCFRButton: Clicked the open links in apps banner \"Dismiss\" button")
    }

    fun clickTakeSurveyButton() {
        val button = mDevice.findObject(UiSelector().text(getStringResource(R.string.preferences_take_survey)))
        button.waitForExists(waitingTime)
        button.click()
    }

    fun clickNoThanksSurveyButton() {
        val button = mDevice.findObject(UiSelector().text(getStringResource(R.string.preferences_not_take_survey)))
        button.waitForExists(waitingTime)
        button.click()
    }

    fun longClickToolbar() {
        Log.i(TAG, "longClickToolbar: Waiting for compose rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "longClickToolbar: Waited for compose rule to be idle")

        Log.i(TAG, "longClickToolbar: Trying to long click the navigation toolbar")
        composeTestRule
            .onAllNodesWithTag(ADDRESSBAR_URL_BOX, useUnmergedTree = true)
            .onLast()
            .assertExists()
            .performTouchInput {
                longClick()
            }
        Log.i(TAG, "longClickToolbar: Long clicked the navigation toolbar")
    }

    fun clickDisplayModeToolbarContextMenuItem(contextMenuItemDescription: String) {
        Log.i(
            TAG,
            "clickDisplayModeToolbarContextMenuItem: Trying to click context menu item: $contextMenuItemDescription",
        )
        composeTestRule.onNodeWithContentDescription(contextMenuItemDescription).performClick()
        Log.i(TAG, "clickDisplayModeToolbarContextMenuItem: Clicked context menu item: $contextMenuItemDescription")
    }

    fun verifyDownloadPromptIsDismissed() =
        assertUIObjectExists(
            itemWithResId("$packageName:id/viewDynamicDownloadDialog"),
            exists = false,
        )

    fun verifyCancelPrivateDownloadsPrompt(numberOfActiveDownloads: String) {
        assertUIObjectExists(
            itemWithResIdContainingText(
                "$packageName:id/title",
                getStringResource(
                    downloadsR.string.mozac_feature_downloads_cancel_active_downloads_warning_content_title
                ),
            ),
            itemWithResIdContainingText(
                "$packageName:id/body",
                "If you close all Private tabs now, $numberOfActiveDownloads download will be canceled. Are you sure you want to leave Private Browsing?",
            ),
            itemWithResIdContainingText(
                "$packageName:id/deny_button",
                getStringResource(downloadsR.string.mozac_feature_downloads_cancel_active_private_downloads_deny),
            ),
            itemWithResIdContainingText(
                "$packageName:id/accept_button",
                getStringResource(downloadsR.string.mozac_feature_downloads_cancel_active_downloads_accept),
            ),
        )
    }

    fun clickStayInPrivateBrowsingPromptButton() {
        Log.i(
            TAG,
            "clickStayInPrivateBrowsingPromptButton: Trying to click the \"STAY IN PRIVATE BROWSING\" prompt button",
        )
        itemWithResIdContainingText(
                "$packageName:id/deny_button",
                getStringResource(downloadsR.string.mozac_feature_downloads_cancel_active_private_downloads_deny),
            )
            .click()
        Log.i(TAG, "clickStayInPrivateBrowsingPromptButton: Clicked the \"STAY IN PRIVATE BROWSING\" prompt button")
    }

    fun clickCancelPrivateDownloadsPromptButton() {
        Log.i(TAG, "clickCancelPrivateDownloadsPromptButton: Trying to click the \"CANCEL DOWNLOADS\" prompt button")
        itemWithResIdContainingText(
                "$packageName:id/accept_button",
                getStringResource(downloadsR.string.mozac_feature_downloads_cancel_active_downloads_accept),
            )
            .click()
        Log.i(TAG, "clickCancelPrivateDownloadsPromptButton: Clicked the \"CANCEL DOWNLOADS\" prompt button")
        Log.i(
            TAG,
            "clickCancelPrivateDownloadsPromptButton: Waiting for $waitingTime ms for $packageName window to be updated",
        )
        mDevice.waitForWindowUpdate(packageName, waitingTime)
        Log.i(
            TAG,
            "clickCancelPrivateDownloadsPromptButton: Waited for $waitingTime ms for $packageName window to be updated",
        )
    }

    fun fillPdfForm(name: String) {
        // Set PDF form text for the text box
        Log.i(TAG, "fillPdfForm: Trying to set the text of the PDF form text box to: $name")
        itemWithResId("pdfjs_internal_id_10R").setText(name)
        Log.i(TAG, "fillPdfForm: PDF form text box text was set to: $name")
        Log.i(TAG, "fillPdfForm: Waiting for $waitingTime ms for $packageName window to be updated")
        mDevice.waitForWindowUpdate(packageName, waitingTime)
        Log.i(TAG, "fillPdfForm: Waited for $waitingTime ms for $packageName window to be updated")

        // Close the keyboard
        Log.i(TAG, "fillPdfForm: Trying to close the keyboard using device back button")
        mDevice.pressBack()
        Log.i(TAG, "fillPdfForm: Closed the keyboard using device back button")
        Log.i(TAG, "fillPdfForm: Waiting for $waitingTime ms for $packageName window to be updated")
        mDevice.waitForWindowUpdate(packageName, waitingTime)
        Log.i(TAG, "fillPdfForm: Waited for $waitingTime ms for $packageName window to be updated")

        // Click PDF form check box
        Log.i(TAG, "fillPdfForm: Trying to click the PDF form check box")
        itemWithResId("pdfjs_internal_id_11R").click()
        Log.i(TAG, "fillPdfForm: Clicked PDF form check box")
    }

    fun getCurrentUrl(): String {
        val searchBar = searchBar()
        waitForPageToLoad()
        return searchBar.getText()
    }

    fun selectToAlwaysOpenDownloadedFileWithApp(appName: String) {
        Log.i(TAG, "selectToAlwaysOpenDownloadedFileWithApp: Trying to click $appName from the \"Open with\" prompt")
        itemWithResIdContainingText("android:id/text1", appName).click()
        Log.i(TAG, "selectToAlwaysOpenDownloadedFileWithApp: Clicked $appName from the \"Open with\" prompt")
        Log.i(
            TAG,
            "selectToAlwaysOpenDownloadedFileWithApp: Trying to click the \"Always\" button from the \"Open with\" prompt",
        )
        itemWithResId("android:id/button_always").click()
        Log.i(
            TAG,
            "selectToAlwaysOpenDownloadedFileWithApp: Clicked the \"Always\" button from the \"Open with\" prompt",
        )
    }

    fun verifyWebCompatReporterViewItems(websiteURL: String) {
        Log.i(TAG, "verifyWebCompatReporterViewItems: Trying to verify that the \"URL\" header is displayed")
        this@BrowserRobot.composeTestRule
            .onNodeWithText(getStringResource(R.string.webcompat_reporter_label_url))
            .performScrollTo()
            .assertIsDisplayed()
        Log.i(TAG, "verifyWebCompatReporterViewItems: Verified that the \"URL\" header is displayed")
        Log.i(TAG, "verifyWebCompatReporterViewItems: Trying to verify that the $websiteURL url is displayed")
        this@BrowserRobot.composeTestRule.onNodeWithText(websiteURL).performScrollTo().assertIsDisplayed()
        Log.i(TAG, "verifyWebCompatReporterViewItems: Verified that the $websiteURL url is displayed")
        Log.i(
            TAG,
            "verifyWebCompatReporterViewItems: Trying to verify that the \"What’s not working?\" header is displayed",
        )
        this@BrowserRobot.composeTestRule
            .onNodeWithText(getStringResource(R.string.webcompat_reporter_label_whats_broken_3))
            .performScrollTo()
            .assertIsDisplayed()
        Log.i(TAG, "verifyWebCompatReporterViewItems: Verified that the \"What’s not working?\" header is displayed")
        Log.i(
            TAG,
            "verifyWebCompatReporterViewItems: Trying to verify that the report broken site description is displayed",
        )
        this@BrowserRobot.composeTestRule
            .onNodeWithContentDescription(
                getStringResource(
                    R.string.webcompat_reporter_description_3,
                    appName,
                    getStringResource(R.string.webcompat_reporter_learn_more),
                ) + " " + getStringResource(composeBaseR.string.mozac_compose_base_link_text_links_available)
            )
            .performScrollTo()
            .assertIsDisplayed()
        Log.i(TAG, "verifyWebCompatReporterViewItems: Verified that the report broken site description is displayed")
    }

    fun verifyWhatIsBrokenField(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "verifyWhatIsBrokenField: Trying to verify that the \"What’s broken?\" header is displayed")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.webcompat_reporter_label_whats_broken_3))
            .performScrollTo()
            .assertIsDisplayed()
        Log.i(TAG, "verifyWhatIsBrokenField: Verified that the \"What’s not working?\" header is displayed")
    }

    fun verifyChooseReasonErrorMessageIsNotDisplayed(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "verifyChooseReasonErrorMessageIsNotDisplayed: Waiting for Compose to finish updating the UI")
        Log.i(
            TAG,
            "verifyChooseReasonErrorMessageIsNotDisplayed: Trying to verify that the \"Please choose a reason\" error message is not displayed",
        )
        composeTestRule.onNodeWithTag(BROKEN_SITE_REPORTER_CHOOSE_REASON_BUTTON).assertDoesNotExist()
        Log.i(
            TAG,
            "verifyChooseReasonErrorMessageIsNotDisplayed: Verified that the \"Please choose a reason\" error message is not displayed",
        )
    }

    fun verifyBrokenSiteProblemDescriptionField(isDisplayed: Boolean) {
        val descriptionLabel = getStringResource(R.string.webcompat_reporter_label_mandatory_description)

        if (isDisplayed) {
            Log.i(TAG, "verifyBrokenSiteProblemDescriptionField: Verifying description field is displayed")
            this@BrowserRobot.composeTestRule.onNodeWithText(descriptionLabel).performScrollTo().assertIsDisplayed()
        } else {
            Log.i(TAG, "verifyBrokenSiteProblemDescriptionField: Verifying description field does not exist")
            this@BrowserRobot.composeTestRule.onNodeWithText(descriptionLabel).assertDoesNotExist()
        }
    }

    fun clickClearSelectedReason(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "clickClearSelectedReason: Trying to clear the selected reason")
        composeTestRule
            .onNodeWithTag(BrokenSiteReporterTestTags.BROKEN_SITE_REPORTER_SELECTED_REASON)
            .performScrollTo()
            .performClick()
        Log.i(TAG, "clickClearSelectedReason: Cleared the selected reason")
    }

    fun clickSiteDoesNotLoadReason(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "clickSiteDoesNotLoadReason: Trying to click the \"Site doesn’t load\" reason option")
        composeTestRule.onNodeWithText(getStringResource(R.string.webcompat_reporter_label_url)).performScrollTo()
        val reasonText = getStringResource(R.string.webcompat_reporter_reason_load)
        composeTestRule
            .onNodeWithTag("$BROKEN_SITE_REPORTER_REASON_OPTION-$reasonText")
            .performScrollTo()
            .performClick()
        Log.i(TAG, "clickSiteDoesNotLoadReason: Clicked the \"Site doesn’t load\" reason option")
    }

    fun clickBrokenSiteFormSendButton(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "clickBrokenSiteFormSendButton: Trying to close the keyboard.")
        closeSoftKeyboard()
        Log.i(TAG, "clickBrokenSiteFormSendButton: Closed the keyboard.")
        Log.i(TAG, "clickBrokenSiteFormSendButton: Trying to click the \"Send\" button")

        composeTestRule.onNodeWithTag(BROKEN_SITE_REPORTER_SEND_BUTTON).performScrollTo().performClick()

        composeTestRule.waitForIdle()

        Log.i(TAG, "clickBrokenSiteFormSendButton: Clicked the \"Send\" button")
    }

    fun describeBrokenSiteProblem(problemDescription: String) {
        Log.i(TAG, "describeBrokenSiteProblem: Trying to click the description field")
        this@BrowserRobot.composeTestRule
            .onNodeWithTag(BrokenSiteReporterTestTags.BROKEN_SITE_REPORTER_DESCRIPTION_INPUT)
            .performScrollTo()
            .performClick()
        Log.i(TAG, "describeBrokenSiteProblem: Clicked the description field")

        Log.i(TAG, "describeBrokenSiteProblem: Trying to set the text to $problemDescription")
        this@BrowserRobot.composeTestRule
            .onNodeWithTag(BrokenSiteReporterTestTags.BROKEN_SITE_REPORTER_DESCRIPTION_INPUT)
            .performScrollTo()
            .performTextInput(problemDescription)
        Log.i(TAG, "describeBrokenSiteProblem: Set the text to $problemDescription")
    }

    fun verifyBrokenSiteProblem(problemDescription: String, isDisplayed: Boolean) {
        if (isDisplayed) {
            Log.i(
                TAG,
                "verifyBrokenSiteProblem: Trying to verify that the $problemDescription broken site problem is displayed",
            )
            this@BrowserRobot.composeTestRule.onNodeWithText(problemDescription).assertIsDisplayed()
            Log.i(
                TAG,
                "verifyBrokenSiteProblem: Verified that the $problemDescription broken site problem is displayed",
            )
        } else {
            Log.i(
                TAG,
                "verifyBrokenSiteProblem: Trying to verify that the $problemDescription broken site problem does not exist",
            )
            this@BrowserRobot.composeTestRule.onNodeWithText(problemDescription).assertDoesNotExist()
            Log.i(
                TAG,
                "verifyBrokenSiteProblem: Verified that the $problemDescription broken site problem does not exist",
            )
        }
    }

    fun verifySendButtonIsEnabled(isEnabled: Boolean) {
        if (isEnabled) {
            Log.i(TAG, "verifySendButtonIsEnabled: Trying to verify that the the \"Send\" button is enabled")
            this@BrowserRobot.composeTestRule
                .onNodeWithTag(BROKEN_SITE_REPORTER_SEND_BUTTON)
                .performScrollTo()
                .assertIsEnabled()
            Log.i(TAG, "verifySendButtonIsEnabled: Verified that the the \"Send\" button is enabled")
        } else {
            Log.i(TAG, "verifySendButtonIsEnabled: Trying to verify that the the \"Send\" button is not enabled")
            this@BrowserRobot.composeTestRule
                .onNodeWithTag(BROKEN_SITE_REPORTER_SEND_BUTTON)
                .performScrollTo()
                .assertIsNotEnabled()
            Log.i(TAG, "verifySendButtonIsEnabled: Verified that the the \"Send\" button is not enabled")
        }
    }

    fun verifyWebCompatPageItemExists(itemText: String, isSmartBlockFixesItem: Boolean = false) {
        for (i in 1..RETRY_COUNT) {
            try {
                Log.i(TAG, "verifyWebCompatPageContent: Started try #$i")
                assertUIObjectExists(itemContainingText(itemText))

                break
            } catch (e: AssertionError) {
                Log.i(TAG, "verifyWebCompatPageContent: verifyWebCompatPageContent caught, executing fallback methods")
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    browserScreen(this@BrowserRobot.composeTestRule) {}
                        .openThreeDotMenu {}
                        .clickRefreshButton {
                            waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
                        }
                    if (isSmartBlockFixesItem) {
                        clickWebCompatPageItem("SmartBlock Fixes")
                    }
                }
            }
        }
    }

    fun clickWebCompatPageItem(itemText: String) {
        clickPageObject(composeTestRule, itemWithText(itemText))
        waitForAppWindowToBeUpdated()
    }

    fun verifyETPShieldIconIsDisplayed(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "verifyETPShieldIconIsDisplayed: Trying to verify that the \"Shield icon\" is displayed")
        composeTestRule
            .onNodeWithContentDescription(
                getStringResource(toolbarR.string.mozac_browser_toolbar_content_description_site_info)
            )
            .assertIsDisplayed()
        Log.i(TAG, "verifyETPShieldIconIsDisplayed: Verified that the \"Shield icon\" was displayed")
    }

    fun verifyTheSummarizeCFR(shouldBeDisplayed: Boolean = true) {
        if (shouldBeDisplayed) {
            Log.i(
                TAG,
                "verifyTheSummarizeCFR: Trying to verify the \"Shake your device to summarize this page in seconds\" CFR message is displayed.",
            )
            composeTestRule.summarizeCFRMessage().assertIsDisplayed()
            Log.i(
                TAG,
                "verifyTheSummarizeCFR: Verified the \"Shake your device to summarize this page in seconds\" CFR message is displayed.",
            )
            Log.i(
                TAG,
                "verifyTheSummarizeCFR: Trying to verify the \"Shake your device to summarize this page in seconds\" CFR dismiss button is displayed.",
            )
            composeTestRule.summarizeCFDismissButton().assertIsDisplayed()
            Log.i(
                TAG,
                "verifyTheSummarizeCFR: Verified the \"Shake your device to summarize this page in seconds\" CFR dismiss button is displayed.",
            )
        } else {
            Log.i(
                TAG,
                "verifyTheSummarizeCFR: Trying to verify the \"Shake your device to summarize this page in seconds\" CFR message is NOT displayed.",
            )
            composeTestRule.summarizeCFRMessage().assertDoesNotExist()
            Log.i(
                TAG,
                "verifyTheSummarizeCFR: Verified the \"Shake your device to summarize this page in seconds\" CFR message is NOT displayed.",
            )
        }
    }

    fun clickTheDismissButtonOnSummarizeCFR() {
        Log.i(TAG, "clickTheDismissButtonOnSummarizeCFR: Waiting for the summarize CFR dismiss button to exist")
        composeTestRule.waitUntilAtLeastOneExists(hasTestTag("cfr.dismiss"), waitingTime)
        Log.i(TAG, "clickTheDismissButtonOnSummarizeCFR: Waited for the summarize CFR dismiss button to exist")
        Log.i(TAG, "clickTheDismissButtonOnSummarizeCFR: Trying to click the \"X\" button on the summarize CFR")
        composeTestRule.summarizeCFDismissButton().performClick()
        Log.i(TAG, "clickTheDismissButtonOnSummarizeCFR: Clicked the \"X\" button on the summarize CFR")
    }

    fun clickStayInAppPromptButton() {
        val stayInAppButton =
            itemContainingText(getStringResource(R.string.applinks_prompt_negative_button, argument = appName))
        if (stayInAppButton.exists()) {
            stayInAppButton.click()
        }
    }

    fun verifyTheSummarizedBottomSheet(isSuccessful: Boolean = false) {
        if (isSuccessful) {
            Log.i(
                TAG,
                "verifyTheSummarizedBottomSheet: Trying to verify that the \"Summary by Firefox\" bottom sheet is displayed.",
            )
            composeTestRule.summarizedBottomSheet().assertIsDisplayed()
            Log.i(
                TAG,
                "verifyTheSummarizedBottomSheet: Verified that the \"Summary by Firefox\" bottom sheet is displayed.",
            )
        } else {
            Log.i(
                TAG,
                "verifyTheSummarizedBottomSheet: Trying to verify that the \"Can't summarize right now\" error message is displayed.",
            )
            composeTestRule.summarizeErrorMessage().assertIsDisplayed()
            Log.i(
                TAG,
                "verifyTheSummarizedBottomSheet: Verified that the \"Can't summarize right now\" error message is displayed.",
            )
        }
    }

    fun verifyTheSignInToSummarizeBottomSheet() {
        Log.i(
            TAG,
            "verifyTheSignInToSummarizeBottomSheet: Trying to verify that the \"Sign in to summarize\" bottom sheet is displayed.",
        )
        composeTestRule
            .onNodeWithText(getStringResource(summarizeR.string.mozac_summarize_fxa_sign_in_title))
            .assertIsDisplayed()
        composeTestRule
            .onNodeWithText(
                getStringResource(summarizeR.string.mozac_summarize_fxa_sign_in_message) +
                    " " +
                    getStringResource(summarizeR.string.mozac_summarize_learn_more_link)
            )
            .assertIsDisplayed()
        composeTestRule
            .onNodeWithText(getStringResource(summarizeR.string.mozac_summarize_fxa_sign_in_button_positive))
            .assertIsDisplayed()
        composeTestRule
            .onNodeWithText(getStringResource(summarizeR.string.mozac_summarize_error_dissmiss))
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheSignInToSummarizeBottomSheet: Verified that the \"Sign in to summarize\" bottom sheet is displayed.",
        )
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        fun openThreeDotMenu(interact: ThreeDotMenuMainRobot.() -> Unit): ThreeDotMenuMainRobot.Transition {
            openMainMenuAndAwaitBottomSheet(composeTestRule)

            ThreeDotMenuMainRobot(composeTestRule).interact()
            return ThreeDotMenuMainRobot.Transition(composeTestRule)
        }

        fun openSearch(interact: SearchRobot.() -> Unit): SearchRobot.Transition {
            Log.i(TAG, "openSearch: Waiting for $waitingTime until the URL bar exists")
            composeTestRule.waitUntilAtLeastOneExists(hasTestTag(ADDRESSBAR_URL_BOX), waitingTime)
            Log.i(TAG, "openSearch: Waited for $waitingTime until the URL bar exists")
            Log.i(TAG, "openSearch: Trying to click navigation toolbar")
            composeTestRule.onAllNodesWithTag(ADDRESSBAR_URL_BOX).onLast().performClick()
            Log.i(TAG, "openSearch: Clicked navigation toolbar")

            SearchRobot(composeTestRule).interact()
            return SearchRobot.Transition(composeTestRule)
        }

        fun openNavigationToolbar(interact: NavigationToolbarRobot.() -> Unit): NavigationToolbarRobot.Transition {
            composeTestRule.waitUntilAtLeastOneExists(hasTestTag(ADDRESSBAR_URL_BOX), waitingTime)
            Log.i(TAG, "openNavigationToolbar: Trying to click navigation toolbar")
            composeTestRule.onAllNodesWithTag(ADDRESSBAR_URL_BOX).onLast().performClick()
            Log.i(TAG, "openNavigationToolbar: Clicked navigation toolbar")
            waitForAppWindowToBeUpdated()

            NavigationToolbarRobot(composeTestRule).interact()
            return NavigationToolbarRobot.Transition(composeTestRule)
        }

        fun openTabDrawer(
            composeTestRule: ComposeTestRule,
            interact: TabDrawerRobot.() -> Unit,
        ): TabDrawerRobot.Transition {
            Log.i(TAG, "openTabDrawer: Trying to click the tab counter button")
            composeTestRule.onNodeWithTag(TABS_COUNTER).performClick()
            Log.i(TAG, "openTabDrawer: Clicked the tab counter button")
            composeTestRule.waitForIdle()
            mDevice.waitForIdle()
            Log.i(TAG, "openTabDrawer: Trying to verify the tabs tray exists")
            composeTestRule.onNodeWithTag(TabsTrayTestTag.TABS_TRAY).assertExists()
            Log.i(TAG, "openTabDrawer: Verified the tabs tray exists")
            Log.i(TAG, "openTabDrawer: Trying to verify the tabs tray new tab FAB button exists")
            composeTestRule.onNodeWithTag(TabsTrayTestTag.FAB).assertExists()
            Log.i(TAG, "openTabDrawer: Verified the tabs tray new tab FAB button exists")

            TabDrawerRobot(composeTestRule).interact()
            return TabDrawerRobot.Transition(composeTestRule)
        }

        fun openNotificationShade(interact: NotificationRobot.() -> Unit): NotificationRobot.Transition {
            Log.i(TAG, "openNotificationShade: Trying to open the notification tray")
            mDevice.openNotification()
            Log.i(TAG, "openNotificationShade: Opened the notification tray")

            NotificationRobot().interact()
            return NotificationRobot.Transition()
        }

        fun goToHomescreen(
            isPrivateModeEnabled: Boolean = false,
            interact: HomeScreenRobot.() -> Unit,
        ): HomeScreenRobot.Transition {
            if (isPrivateModeEnabled) {
                Log.i(TAG, "goToHomescreen: Trying to click the \"New private tab\" button.")
                composeTestRule.onNodeWithContentDescription("New private tab").performClick()
                Log.i(TAG, "goToHomescreen: Clicked the \"New private tab\" button.")
            } else {
                Log.i(TAG, "goToHomescreen: Trying to click the \"New tab\" button.")
                composeTestRule.onNodeWithContentDescription("New tab").performClick()
                Log.i(TAG, "goToHomescreen: Clicked the \"New tab\" button.")
            }
            Log.i(TAG, "goToHomescreen: Trying to close the keyboard.")
            closeSoftKeyboard()
            Log.i(TAG, "goToHomescreen: Closed the keyboard.")
            Log.i(TAG, "goToHomescreen: Waiting for home screen to exist")
            composeTestRule.waitUntilAtLeastOneExists(hasTestTag(HOMEPAGE))
            Log.i(TAG, "goToHomescreen: Waited for home screen to exist")
            Log.i(TAG, "goToHomescreen: Trying to click the device back button")
            mDevice.pressBack()
            Log.i(TAG, "goToHomescreen: Clicked the device back button")
            mDevice.waitForIdle()
            composeTestRule.waitForIdle()

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }

        fun goBack(interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            Log.i(TAG, "goBack: Trying to click device back button")
            mDevice.pressBack()
            Log.i(TAG, "goBack: Clicked device back button")

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }

        fun clickTabCrashedCloseButton(interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            clickPageObject(composeTestRule, itemWithText("Close tab"))
            Log.i(TAG, "clickTabCrashedCloseButton: Waiting for device to be idle")
            mDevice.waitForIdle()
            Log.i(TAG, "clickTabCrashedCloseButton: Waited for device to be idle")

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }

        fun clickTabCrashedRestoreButton(interact: BrowserRobot.() -> Unit): Transition {
            clickPageObject(composeTestRule, itemWithResId("$packageName:id/restoreTabButton"))
            Log.i(TAG, "clickTabCrashedRestoreButton: Waiting for device to be idle")
            mDevice.waitForIdle()
            Log.i(TAG, "clickTabCrashedRestoreButton: Waited for device to be idle")

            BrowserRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun clickShareSelectedText(interact: ShareOverlayRobot.() -> Unit): ShareOverlayRobot.Transition {
            clickContextMenuItem("Share")

            ShareOverlayRobot().interact()
            return ShareOverlayRobot.Transition()
        }

        fun clickDownloadLink(title: String, interact: DownloadRobot.() -> Unit): DownloadRobot.Transition {
            for (i in 1..RETRY_COUNT) {
                Log.i(TAG, "clickDownloadLink: Started try #$i")
                try {
                    Log.i(TAG, "clickDownloadLink: Trying to click the: $title download link")
                    mDevice.findObject(By.textContains(title)).click()
                    Log.i(TAG, "clickDownloadLink: Clicked the: $title download link")
                    assertUIObjectExists(itemWithResId("$packageName:id/parentPanel"))

                    break
                } catch (e: AssertionError) {
                    Log.i(TAG, "clickDownloadLink: AssertionError caught, executing fallback methods")
                    if (i == RETRY_COUNT) {
                        throw e
                    } else {
                        browserScreen(composeTestRule) {}
                            .openThreeDotMenu {}
                            .clickRefreshButton {
                                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
                            }
                    }
                }
            }

            DownloadRobot(composeTestRule).interact()
            return DownloadRobot.Transition(composeTestRule)
        }

        fun clickStartCameraButton(interact: SitePermissionsRobot.() -> Unit): SitePermissionsRobot.Transition {
            // Test page used for testing permissions located at https://mozilla-mobile.github.io/testapp/permissions
            clickPageObject(composeTestRule, itemWithText("Open camera"))
            mDevice.waitForObjects(mDevice.findObject(UiSelector().textContains("to use your camera?")))

            SitePermissionsRobot(composeTestRule).interact()
            return SitePermissionsRobot.Transition(composeTestRule)
        }

        fun clickStartMicrophoneButton(interact: SitePermissionsRobot.() -> Unit): SitePermissionsRobot.Transition {
            // Test page used for testing permissions located at https://mozilla-mobile.github.io/testapp/permissions
            clickPageObject(composeTestRule, itemWithText("Open microphone"))
            mDevice.waitForObjects(mDevice.findObject(UiSelector().textContains("to use your microphone?")))

            SitePermissionsRobot(composeTestRule).interact()
            return SitePermissionsRobot.Transition(composeTestRule)
        }

        fun clickStartAudioVideoButton(interact: SitePermissionsRobot.() -> Unit): SitePermissionsRobot.Transition {
            // Test page used for testing permissions located at https://mozilla-mobile.github.io/testapp/permissions
            clickPageObject(composeTestRule, itemWithText("Camera & Microphone"))
            mDevice.waitForObjects(mDevice.findObject(UiSelector().textContains("to use your camera and microphone?")))

            SitePermissionsRobot(composeTestRule).interact()
            return SitePermissionsRobot.Transition(composeTestRule)
        }

        fun clickOpenNotificationButton(interact: SitePermissionsRobot.() -> Unit): SitePermissionsRobot.Transition {
            // Test page used for testing permissions located at https://mozilla-mobile.github.io/testapp/permissions
            clickPageObject(composeTestRule, itemWithText("Open notifications dialogue"))
            mDevice.waitForObjects(mDevice.findObject(UiSelector().textContains("Allow to send notifications?")))

            SitePermissionsRobot(composeTestRule).interact()
            return SitePermissionsRobot.Transition(composeTestRule)
        }

        fun clickGetLocationButton(interact: SitePermissionsRobot.() -> Unit): SitePermissionsRobot.Transition {
            // Test page used for testing permissions located at https://mozilla-mobile.github.io/testapp/permissions
            clickPageObject(composeTestRule, itemWithText("Get Location"))
            mDevice.waitForObjects(mDevice.findObject(UiSelector().textContains("to use your location?")))

            SitePermissionsRobot(composeTestRule).interact()
            return SitePermissionsRobot.Transition(composeTestRule)
        }

        fun clickRequestStorageAccessButton(
            interact: SitePermissionsRobot.() -> Unit
        ): SitePermissionsRobot.Transition {
            for (i in 1..RETRY_COUNT) {
                Log.i(TAG, "clickRequestStorageAccessButton: Started try #$i")
                try {
                    // Clicks the "request storage access" button from the "cross-site-cookies.html" local asset
                    clickPageObject(composeTestRule, itemContainingText("requestStorageAccess()"))
                    assertUIObjectExists(
                        itemWithResId("$packageName:id/deny_button"),
                        itemWithResId("$packageName:id/allow_button"),
                    )

                    break
                } catch (e: AssertionError) {
                    Log.i(TAG, "clickRequestStorageAccessButton: AssertionError caught, executing fallback methods")
                    if (i == RETRY_COUNT) {
                        throw e
                    }
                }
            }

            SitePermissionsRobot(composeTestRule).interact()
            return SitePermissionsRobot.Transition(composeTestRule)
        }

        fun clickRequestPersistentStorageAccessButton(
            interact: SitePermissionsRobot.() -> Unit
        ): SitePermissionsRobot.Transition {
            // Clicks the "Persistent storage" button from "https://mozilla-mobile.github.io/testapp/permissions"
            clickPageObject(composeTestRule, itemWithResId("persistentStorageButton"))

            SitePermissionsRobot(composeTestRule).interact()
            return SitePermissionsRobot.Transition(composeTestRule)
        }

        fun clickRequestDRMControlledContentAccessButton(
            interact: SitePermissionsRobot.() -> Unit
        ): SitePermissionsRobot.Transition {
            // Clicks the "DRM-controlled content" button from "https://mozilla-mobile.github.io/testapp/permissions"
            clickPageObject(composeTestRule, itemWithResId("drmPermissionButton"))

            SitePermissionsRobot(composeTestRule).interact()
            return SitePermissionsRobot.Transition(composeTestRule)
        }

        fun clickManageAddressButton(
            interact: SettingsSubMenuAutofillRobot.() -> Unit
        ): SettingsSubMenuAutofillRobot.Transition {
            Log.i(
                TAG,
                "clickManageAddressButton: Trying to click the manage address button and wait for $waitingTime ms for a new window",
            )
            itemWithResId("$packageName:id/manage_addresses").clickAndWaitForNewWindow(waitingTime)
            Log.i(
                TAG,
                "clickManageAddressButton: Clicked the manage address button and waited for $waitingTime ms for a new window",
            )

            SettingsSubMenuAutofillRobot(composeTestRule).interact()
            return SettingsSubMenuAutofillRobot.Transition(composeTestRule)
        }

        fun clickManageCreditCardsButton(
            interact: SettingsSubMenuAutofillRobot.() -> Unit
        ): SettingsSubMenuAutofillRobot.Transition {
            Log.i(
                TAG,
                "clickManageCreditCardsButton: Trying to click the manage credit cards button and wait for $waitingTime ms for a new window",
            )
            itemWithResId("$packageName:id/manage_credit_cards").clickAndWaitForNewWindow(waitingTime)
            Log.i(
                TAG,
                "clickManageCreditCardsButton: Clicked the manage credit cards button and waited for $waitingTime ms for a new window",
            )

            SettingsSubMenuAutofillRobot(composeTestRule).interact()
            return SettingsSubMenuAutofillRobot.Transition(composeTestRule)
        }

        fun clickOpenLinksInAppsGoToSettingsCFRButton(interact: SettingsRobot.() -> Unit): SettingsRobot.Transition {
            Log.i(
                TAG,
                "clickOpenLinksInAppsGoToSettingsCFRButton: Trying to click the \"Go to settings\" open links in apps CFR button and wait for $waitingTime ms for a new window",
            )
            itemWithResIdContainingText(
                    "$packageName:id/action",
                    getStringResource(R.string.open_in_app_cfr_positive_button_text),
                )
                .clickAndWaitForNewWindow(waitingTime)
            Log.i(
                TAG,
                "clickOpenLinksInAppsGoToSettingsCFRButton: Clicked the \"Go to settings\" open links in apps CFR button and waited for $waitingTime ms for a new window",
            )

            SettingsRobot().interact()
            return SettingsRobot.Transition()
        }

        fun clickDownloadPDFButton(
            confirmDownloadPrompt: Boolean = true,
            interact: DownloadRobot.() -> Unit,
        ): DownloadRobot.Transition {
            // Bug 2081247 to switch to new toolbar for testing
            if (Config.channel.isNightlyOrDebug) {
                Log.i(TAG, "clickDownloadPDFButton: Waiting for $waitingTime until the download PDF button exists")
                composeTestRule.waitUntilAtLeastOneExists(hasTestTag(PdfToolsTestTag.DOWNLOAD_BUTTON), waitingTime)
                Log.i(TAG, "clickDownloadPDFButton: Waited for $waitingTime until the download PDF button exists")
                Log.i(TAG, "clickDownloadPDFButton: Trying to click the download PDF button")
                composeTestRule.onNodeWithTag(PdfToolsTestTag.DOWNLOAD_BUTTON).performClick()
                Log.i(TAG, "clickDownloadPDFButton: Clicked the download PDF button")
                if (confirmDownloadPrompt) {
                    DownloadRobot(composeTestRule).verifyDownloadPrompt(composeTestRule)
                    DownloadRobot.Transition(composeTestRule).clickDownload(composeTestRule) {}
                }
            } else {
                Log.i(TAG, "clickDownloadPDFButton: Trying to click the download PDF button")
                itemWithResIdContainingText(
                        "download",
                        "Download",
                    )
                    .click()
                Log.i(TAG, "clickDownloadPDFButton: Clicked the download PDF button")
            }

            DownloadRobot(composeTestRule).interact()
            return DownloadRobot.Transition(composeTestRule)
        }

        fun closeWebCompatReporter(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            val backDescription = "Navigate back"
            Log.i(TAG, "closeWebCompatReporter: Waiting for $waitingTime until the \"Navigate back\" button exists")
            composeTestRule.waitUntilAtLeastOneExists(
                hasContentDescription(backDescription, substring = true),
                waitingTime,
            )
            Log.i(TAG, "closeWebCompatReporter: Waited for $waitingTime until the \"Navigate back\" button exists")
            Log.i(TAG, "closeWebCompatReporter: Trying to click the \"Navigate back\" button")
            composeTestRule.onNodeWithContentDescription(backDescription, substring = true).performClick()
            Log.i(TAG, "closeWebCompatReporter: Clicked the \"Navigate back\" button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }
    }
}

fun browserScreen(composeTestRule: ComposeTestRule, interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
    BrowserRobot(composeTestRule).interact()
    return BrowserRobot.Transition(composeTestRule)
}

private fun navURLBar() = itemWithResId("$packageName:id/toolbar")

private fun searchBar() = itemWithResId("$packageName:id/mozac_browser_toolbar_url_view")

private fun suggestedLogins() = itemWithResId("$packageName:id/loginSelectBar")

private fun selectAddressButton() = itemWithResId("$packageName:id/select_address_header")

private fun selectCreditCardButton() = itemWithResId("$packageName:id/select_credit_card_header")

fun clickPageObject(composeTestRule: ComposeTestRule, item: UiObject) {
    for (i in 1..RETRY_COUNT) {
        try {
            Log.i(TAG, "clickPageObject: Started try #$i")
            Log.i(TAG, "clickPageObject: Waiting for $waitingTimeLong ms for ${item.selector} to exist")
            item.waitForExists(waitingTimeLong)
            Log.i(TAG, "clickPageObject: Waited for $waitingTimeLong ms for ${item.selector} to exist")
            mDevice.waitForIdle()
            Log.i(TAG, "clickPageObject: Trying to click ${item.selector}")
            item.click()
            Log.i(TAG, "clickPageObject: Clicked ${item.selector}")
            waitForAppWindowToBeUpdated()

            break
        } catch (e: UiObjectNotFoundException) {
            Log.i(TAG, "clickPageObject: UiObjectNotFoundException caught, executing fallback methods")
            if (i == RETRY_COUNT) {
                throw e
            } else {
                browserScreen(composeTestRule) {}
                    .openThreeDotMenu {}
                    .clickRefreshButton {
                        runCatching { waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong) }
                            .onFailure { Log.w(TAG, "clickPageObject: waitForPageToLoad timed out on retry $i: $it") }
                    }
            }
        }
    }
}

fun longClickPageObject(composeTestRule: ComposeTestRule, item: UiObject) {
    for (i in 1..RETRY_COUNT) {
        try {
            Log.i(TAG, "longClickPageObject: Started try #$i")
            Log.i(TAG, "longClickPageObject: Waiting for $waitingTime ms for ${item.selector} to exist")
            item.waitForExists(waitingTime)
            Log.i(TAG, "longClickPageObject: Waited for $waitingTime ms for ${item.selector} to exist")
            mDevice.waitForIdle()
            Log.i(TAG, "longClickPageObject: Trying to long click ${item.selector}")
            item.longClick()
            Log.i(TAG, "longClickPageObject: Long clicked ${item.selector}")

            break
        } catch (e: UiObjectNotFoundException) {
            Log.i(TAG, "longClickPageObject: UiObjectNotFoundException caught, executing fallback methods")
            if (i == RETRY_COUNT) {
                throw e
            } else {
                browserScreen(composeTestRule) {}
                    .openThreeDotMenu {}
                    .clickRefreshButton {
                        waitForPageToLoad()
                    }
            }
        }
    }
}

fun clickContextMenuItem(item: String) {
    Log.i(TAG, "clickContextMenuItem: Waiting for $waitingTime ms for context menu item \"$item\" to exist")
    val menuItem =
        mDevice.wait(Until.findObject(text(item)), waitingTime)
            ?: throw AssertionError("Context menu item \"$item\" not found after $waitingTime ms")
    Log.i(TAG, "clickContextMenuItem: Trying to click context menu item: $item")
    menuItem.click()
    Log.i(TAG, "clickContextMenuItem: Clicked context menu item: $item")
    Log.i(TAG, "clickContextMenuItem: Waiting for $waitingTimeShort ms for $packageName window to be updated")
    mDevice.waitForWindowUpdate(packageName, waitingTimeShort)
    Log.i(TAG, "clickContextMenuItem: Waited for $waitingTimeShort ms for $packageName window to be updated")
}

fun setPageObjectText(composeTestRule: ComposeTestRule, webPageItem: UiObject, text: String) {
    for (i in 1..RETRY_COUNT) {
        Log.i(TAG, "setPageObjectText: Started try #$i")
        try {
            webPageItem.also {
                Log.i(TAG, "setPageObjectText: Waiting for $waitingTime ms for ${webPageItem.selector} to exist")
                it.waitForExists(waitingTime)
                Log.i(TAG, "setPageObjectText: Waited for $waitingTime ms for ${webPageItem.selector} to exist")
                Log.i(TAG, "setPageObjectText: Trying to clear ${webPageItem.selector} text field")
                it.clearTextField()
                Log.i(TAG, "setPageObjectText: Cleared ${webPageItem.selector} text field")
                Log.i(TAG, "setPageObjectText: Trying to set ${webPageItem.selector} text to $text")
                it.setText(text)
                Log.i(TAG, "setPageObjectText: ${webPageItem.selector} text was set to $text")
            }

            break
        } catch (e: UiObjectNotFoundException) {
            Log.i(TAG, "setPageObjectText: UiObjectNotFoundException caught, executing fallback methods")
            if (i == RETRY_COUNT) {
                throw e
            } else {
                browserScreen(composeTestRule) {}
                    .openThreeDotMenu {}
                    .clickRefreshButton {
                        waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
                    }
            }
        }
    }
}

fun clearTextFieldItem(item: UiObject) {
    Log.i(TAG, "clearTextFieldItem: Waiting for $waitingTime ms for ${item.selector} to exist")
    item.waitForExists(waitingTime)
    Log.i(TAG, "clearTextFieldItem: Waited for $waitingTime ms for ${item.selector} to exist")
    Log.i(TAG, "clearTextFieldItem: Trying to clear ${item.selector} text field")
    item.clearTextField()
    Log.i(TAG, "clearTextFieldItem: Cleared ${item.selector} text field")
}

// Context menu items
// Link URL
private fun contextMenuLinkUrl(linkUrl: String) = itemContainingText(linkUrl)

// Open link in new tab option
private fun contextMenuOpenLinkInNewTab() =
    itemContainingText(getStringResource(contextmenuR.string.mozac_feature_contextmenu_open_link_in_new_tab))

// Open link in private tab option
private fun contextMenuOpenLinkInPrivateTab() =
    itemContainingText(getStringResource(contextmenuR.string.mozac_feature_contextmenu_open_link_in_private_tab))

// Copy link option
private fun contextMenuCopyLink() =
    itemContainingText(getStringResource(contextmenuR.string.mozac_feature_contextmenu_copy_link))

// Download link option
private fun contextMenuDownloadLink() =
    itemContainingText(getStringResource(contextmenuR.string.mozac_feature_contextmenu_download_link))

// Share link option
private fun contextMenuShareLink() =
    itemContainingText(getStringResource(contextmenuR.string.mozac_feature_contextmenu_share_link))

// Open in external app option
private fun contextMenuOpenInExternalApp() =
    itemContainingText(getStringResource(contextmenuR.string.mozac_feature_contextmenu_open_link_in_external_app))

private fun ComposeTestRule.summarizeCFRMessage() =
    onNodeWithText(getStringResource(R.string.browser_toolbar_summarize_cfr_description))

private fun ComposeTestRule.summarizeCFDismissButton() = onNodeWithTag("cfr.dismiss")

private fun ComposeTestRule.summarizedBottomSheet() = onNodeWithText("Summary by Firefox")

private fun ComposeTestRule.summarizeErrorMessage() = onNodeWithText("Try again later.")
