/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.pageObjects

import android.os.SystemClock
import android.util.Log
import android.view.WindowManager
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.ComposeTimeoutException
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onLast
import androidx.compose.ui.test.performTouchInput
import androidx.test.uiautomator.By
import androidx.test.uiautomator.Until
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_URL
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_URL_BOX
import mozilla.components.concept.engine.mediasession.MediaSession
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeShort
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.ext.waitNotNull
import org.mozilla.fenix.ui.efficiency.helpers.BasePage
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.navigation.NavigationRegistry
import org.mozilla.fenix.ui.efficiency.navigation.NavigationStep
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.DownloadsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.HomeSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SearchBarSelectors
import org.mozilla.fenix.ui.efficiency.selectors.ToolbarSelectors

class BrowserPage(composeRule: AndroidComposeTestRule<HomeActivityIntentTestRule, *>) : BasePage(composeRule) {
    override val pageName = "BrowserPage"

    init {
        NavigationRegistry.register(
            from = "HomePage",
            to = pageName,
            steps =
                listOf(
                    NavigationStep.Click(ToolbarSelectors.TOOLBAR_URL_BOX),
                    NavigationStep.EnterText(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE),
                    NavigationStep.PressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE),
                ),
        )

        // Use UIAutomator selector to avoid Compose sync hanging when GeckoView is active.
        NavigationRegistry.register(
            from = pageName,
            to = pageName,
            steps =
                listOf(
                    NavigationStep.Click(ToolbarSelectors.TOOLBAR_URL_BOX_UIAUTOMATOR),
                    NavigationStep.EnterText(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE),
                    NavigationStep.PressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE),
                ),
        )

        NavigationRegistry.register(
            from = "SearchBarComponent",
            to = pageName,
            steps =
                listOf(
                    NavigationStep.EnterText(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE),
                    NavigationStep.PressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE),
                ),
        )

        // Use UIAutomator selector to avoid Compose sync hanging when GeckoView is active.
        NavigationRegistry.register(
            from = pageName,
            to = "MainMenuPage",
            steps = listOf(NavigationStep.Click(HomeSelectors.MAIN_MENU_BUTTON_UIAUTOMATOR)),
        )

        // Use UIAutomator selector to avoid Compose sync hanging when GeckoView is active.
        NavigationRegistry.register(
            from = pageName,
            to = "HomePage",
            steps =
                listOf(
                    NavigationStep.Click(ToolbarSelectors.NEW_TAB_BUTTON),
                    NavigationStep.PressBack,
                ),
        )

        NavigationRegistry.register(
            from = pageName,
            to = "AddToHomeScreenComponent",
            steps =
                listOf(
                    NavigationStep.Click(BrowserPageSelectors.MAIN_MENU_BUTTON),
                    NavigationStep.Click(MainMenuSelectors.MORE_BUTTON),
                    NavigationStep.Click(MainMenuSelectors.ADD_TO_HOMESCREEN_BUTTON),
                ),
        )

        // UIAutomator (Compose sync can hang while GeckoView is active) AND the content-description
        // variant rather than the testTag: with shouldUseExpandedToolbar the counter moves to the bottom
        // navigation bar, where it carries no tag. NOTE: TabDrawerPage registers this same edge — keep the
        // two in step until the duplicate is removed.
        NavigationRegistry.register(
            from = pageName,
            to = "TabDrawerPage",
            steps = listOf(NavigationStep.Click(ToolbarSelectors.TAB_COUNTER_ANY_LAYOUT)),
        )
    }

    override fun navigateToPage(url: String, forceNavigation: Boolean): BrowserPage {
        super.navigateToPage(url = url.ifBlank { "example.com" }, forceNavigation = forceNavigation)
        return this
    }

    override fun mozGetSelectorsByGroup(group: String): List<Selector> {
        return BrowserPageSelectors.all.filter { it.groups.contains(group) }
    }

    /**
     * Taps the Play control on a web media page (video or audio). The tap is confirmed by [verifyMediaPlaybackState],
     * not by the click call itself -- see [BrowserPageSelectors.MEDIA_PLAY_BUTTON] for why this is an exact-text,
     * fire-and-forget click.
     */
    fun clickMediaPlayButton(): BrowserPage {
        mozClick(BrowserPageSelectors.MEDIA_PLAY_BUTTON)
        return this
    }

    /**
     * Polls the selected tab's media-session playback state until it reaches [expectedState] or the wait budget runs
     * out. Reads the browser store directly rather than the UI, mirroring the legacy BrowserRobot.assertPlaybackState.
     */
    fun verifyMediaPlaybackState(expectedState: MediaSession.PlaybackState): BrowserPage {
        val store = appContext.components.core.store
        val startMillis = SystemClock.uptimeMillis()
        while (SystemClock.uptimeMillis() - startMillis <= waitingTime) {
            if (store.state.selectedTab?.mediaSessionState?.playbackState == expectedState) {
                return this
            }
        }
        fail("Playback did not move to state: $expectedState")
        return this
    }

    fun verifyPageContent(text: String): BrowserPage {
        mDevice.waitNotNull(
            Until.findObject(By.res("$packageName:id/engineView")),
            waitingTime,
        )
        assertTrue(mDevice.wait(Until.findObject(By.textContains(text)), waitingTimeLong) != null)
        return this
    }

    /**
     * Verify page content that the page only produces after some asynchronous work has settled, reloading between
     * attempts. Needed for the tracking-protection test page, whose "<category> blocked" report is written once
     * trackers have been processed — a plain [verifyPageContent] can run before that lands and no amount of waiting on
     * the current document will make it appear.
     *
     * Mirrors the legacy BrowserRobot.verifyTrackingProtectionWebContent, which retried each assertion with a page
     * refresh for the same reason.
     */
    fun verifyPageContentWithReload(url: String, text: String, attempts: Int = 3): BrowserPage {
        for (attempt in 1..attempts) {
            try {
                return verifyPageContent(text)
            } catch (e: AssertionError) {
                if (attempt == attempts) throw e
                Log.i("BrowserPage", "verifyPageContentWithReload: '$text' absent on attempt $attempt, reloading")
                navigateToPage(url, forceNavigation = true)
            }
        }
        return this
    }

    fun verifyTranslationSheetIsDisplayed(): BrowserPage {
        // The "translate from/to" dropdowns render a beat after the sheet frame, title and
        // buttons because they depend on the async page-settings fetch. mozVerifyElementsByGroup
        // is a single-shot check, so firing it the instant the sheet animates in can miss the
        // dropdowns and fail spuriously. Gate on the last-rendered dropdown before the group check.
        mozVerify(BrowserPageSelectors.TRANSLATION_SHEET_TRANSLATE_TO, timeout = waitingTimeLong)
        mozVerifyElementsByGroup("notTranslatedPageTranslationSheet")
        return this
    }

    // Reload-based recovery for the page-load auto-prompt path, where reloading the page re-triggers
    // the sheet. The menu-opened path (isPageLoadTranslationsPromptEnabled = false) must NOT use this:
    // a reload there dismisses the sheet with no way to bring it back -- call
    // verifyTranslationSheetIsDisplayed() directly instead.
    fun verifyTranslationSheetWithReload(url: String, attempts: Int = 3): BrowserPage {
        for (attempt in 1..attempts) {
            try {
                return verifyTranslationSheetIsDisplayed()
            } catch (e: AssertionError) {
                if (attempt == attempts) throw e
                Log.i(
                    "BrowserPage",
                    "verifyTranslationSheetWithReload: translation sheet absent on attempt $attempt, reloading",
                )
                navigateToPage(url, forceNavigation = true)
            }
        }
        return this
    }

    fun translatePageFromSheet(attempts: Int = 3): BrowserPage {
        mozClick(BrowserPageSelectors.TRANSLATION_SHEET_TRANSLATE_BUTTON)
        // A first-time translation downloads a language model (tens of MB) before it can finish
        // and dismiss the sheet, so on a slow or briefly-dropped network a single wait window
        // isn't enough. Retry the wait -- the download keeps progressing in the background -- rather
        // than failing the first time it overruns. This mirrors the legacy TranslationsRobot's
        // RETRY_COUNT x waitingTimeLong budget.
        for (attempt in 1..attempts) {
            try {
                mozWaitUntilAbsent(BrowserPageSelectors.TRANSLATION_SHEET_TRANSLATE_BUTTON, timeout = waitingTimeLong)
                return this
            } catch (e: AssertionError) {
                if (attempt == attempts) throw e
                Log.i("BrowserPage", "translatePageFromSheet: sheet still up after attempt $attempt, waiting again")
            }
        }
        return this
    }

    fun verifyHttpsOnlyErrorPage(): BrowserPage {
        return verifyPageContent("Secure site not available")
            .verifyPageContent("Most likely, the website simply does not support HTTPS.")
            .verifyPageContent("HTTPS-Only mode will be turned off temporarily")
            .verifyPageContent(HTTPS_ERROR_GO_BACK)
    }

    fun goBackFromHttpsError(): BrowserPage {
        return clickPageContent(HTTPS_ERROR_GO_BACK).clickPageContentIfPresent(HTTPS_ERROR_GO_BACK)
    }

    fun clickPageContent(text: String): BrowserPage {
        mozClick(BrowserPageSelectors.PAGE_CONTENT(text))
        return this
    }

    fun clickPageContentIfPresent(text: String): BrowserPage {
        mozClickIfPresent(BrowserPageSelectors.PAGE_CONTENT(text))
        return this
    }

    /**
     * Click a web-content element and wait for [expectedContent] to render, reloading [url] and re-clicking between
     * attempts. Mirrors the legacy clickPageObject retry-with-refresh: a tap can land before GeckoView has wired up the
     * page's DOM handlers, in which case the click is a silent no-op and waiting on the same document never recovers
     * it.
     */
    fun clickPageObjectUntilContent(
        selector: Selector,
        url: String,
        expectedContent: String,
        attempts: Int = 3,
    ): BrowserPage {
        for (attempt in 1..attempts) {
            mozClick(selector)
            try {
                return verifyPageContent(expectedContent)
            } catch (e: AssertionError) {
                if (attempt == attempts) throw e
                Log.i(
                    "BrowserPage",
                    "clickPageObjectUntilContent: '$expectedContent' absent on attempt $attempt, reloading",
                )
                navigateToPage(url, forceNavigation = true)
            }
        }
        return this
    }

    // --- Downloads from a web page ---

    /**
     * Click the download link named [fileName] and wait for the download prompt, reloading [url] between attempts.
     *
     * Mirrors the legacy BrowserRobot.clickDownloadLink, which retried the click three times with a page refresh in
     * between: the link can be tapped before the page is fully interactive, in which case the tap lands but no prompt
     * opens — waiting longer on the same document never helps.
     */
    fun clickDownloadLink(fileName: String, url: String, attempts: Int = 3): BrowserPage {
        for (attempt in 1..attempts) {
            try {
                mozClick(DownloadsSelectors.DOWNLOAD_LINK(fileName))
                mozVerify(DownloadsSelectors.DOWNLOAD_DIALOG_TITLE, timeout = waitingTimeLong)
                return this
            } catch (e: AssertionError) {
                if (attempt == attempts) throw e
                Log.i(
                    "BrowserPage",
                    "clickDownloadLink: no download prompt for '$fileName' on attempt $attempt, reloading",
                )
                navigateToPage(url, forceNavigation = true)
            }
        }
        return this
    }

    /**
     * Assert the full download prompt, not just its title: legacy verifyDownloadPrompt checked the dialog, its Cancel
     * button and its Download button were all displayed.
     */
    fun verifyDownloadPrompt(): BrowserPage {
        mozVerify(DownloadsSelectors.DOWNLOAD_DIALOG_TITLE, timeout = waitingTimeLong)
        mozVerify(DownloadsSelectors.DOWNLOAD_DIALOG_CANCEL_BUTTON)
        mozVerify(DownloadsSelectors.DOWNLOAD_DIALOG_CONFIRM_BUTTON)
        return this
    }

    /** Confirm the download prompt, starting the download. */
    fun clickDownloadPromptConfirmButton(): BrowserPage {
        mozClick(DownloadsSelectors.DOWNLOAD_DIALOG_CONFIRM_BUTTON)
        return this
    }

    /**
     * Assert the "Download completed" snackbar for [fileName] — the completion text, the "Open" action (tag and label)
     * and the file name itself, as legacy verifyDownloadCompleteSnackbar did.
     */
    fun verifyDownloadCompleteSnackbar(fileName: String): BrowserPage {
        // The snackbar only appears once the file has actually transferred, so this waits on the
        // network, not on rendering.
        mozVerify(DownloadsSelectors.DOWNLOAD_COMPLETE_SNACKBAR, timeout = waitingTimeLong)
        mozVerify(DownloadsSelectors.DOWNLOAD_SNACK_BAR_OPEN_BUTTON)
        mozVerify(DownloadsSelectors.DOWNLOAD_SNACK_BAR_OPEN_ACTION_LABEL)
        mozVerify(DownloadsSelectors.FILE_NAME_TEXT(fileName))
        return this
    }

    /**
     * Assert the "Download in progress" snackbar shown after starting a large download, as legacy
     * verifySnackBarText("Download in progress") did.
     */
    fun verifyDownloadInProgressSnackbar(): BrowserPage {
        mozVerify(DownloadsSelectors.DOWNLOAD_IN_PROGRESS_SNACKBAR, timeout = waitingTimeLong)
        return this
    }

    /**
     * Wait for the download snackbar to auto-dismiss so it stops covering the page before the notification shade is
     * opened. Mirrors legacy DownloadRobot.waitUntilDownloadSnackbarGone.
     */
    fun waitUntilDownloadSnackbarGone(): BrowserPage {
        mozWaitUntilAbsent(DownloadsSelectors.DOWNLOAD_IN_PROGRESS_SNACKBAR, timeout = waitingTimeLong)
        return this
    }

    fun clickSubmitLoginButton(): BrowserPage {
        mozClick(BrowserPageSelectors.SUBMIT_LOGIN_BUTTON)
        return this
    }

    fun verifySaveLoginPromptIsDisplayed(): BrowserPage {
        mozVerify(BrowserPageSelectors.SAVE_LOGIN_PROMPT)
        return this
    }

    fun verifySaveLoginPromptIsNotDisplayed(): BrowserPage {
        mozVerifyElementAbsent(BrowserPageSelectors.SAVE_LOGIN_PROMPT)
        return this
    }

    fun continueToHttpSite(): BrowserPage {
        // The HTTPS-Only error page renders its buttons only once the HTTP->HTTPS upgrade has failed,
        // which lands after navigateToPage() has already returned on engineView. mozClick does a single
        // non-polling lookup, so wait for the button to render first, otherwise the click races it.
        verifyPageContent(HTTPS_ERROR_CONTINUE)
        return clickPageContent(HTTPS_ERROR_CONTINUE)
    }

    fun verifyOpenLinkInAppPrompt(appName: String): BrowserPage {
        mozVerify(BrowserPageSelectors.OPEN_IN_APP_PROMPT(appName), timeout = waitingTimeLong)
        return this
    }

    fun clickOpenLinkInAppPromptOpenButton(): BrowserPage {
        mozClick(BrowserPageSelectors.OPEN_IN_APP_PROMPT_BUTTON)
        return this
    }

    fun clickStayInBrowserPromptButton(): BrowserPage {
        mozClick(BrowserPageSelectors.STAY_IN_FIREFOX_PROMPT_BUTTON)
        return this
    }

    /**
     * Dismiss the "Stay in / Open in app" prompt if it is showing, otherwise do nothing. The prompt is not guaranteed
     * to appear when opening a PDF link, so this mirrors the legacy clickStayInAppPromptButton, which was guarded by an
     * `if (exists())` no-op.
     */
    fun clickStayInAppPromptButtonIfPresent(): BrowserPage {
        mozClickIfPresent(BrowserPageSelectors.STAY_IN_FIREFOX_PROMPT_BUTTON)
        return this
    }

    // --- Text selection on web content ---

    /**
     * Long-press web-content text containing [text] to raise the text-selection floating action bar. Waits for the text
     * to render first (a PDF or a freshly loaded page may not be interactive the instant navigation reports done),
     * mirroring the legacy longClickPageObject's waitForExists.
     */
    fun longClickWebContentText(text: String): BrowserPage {
        val selector = BrowserPageSelectors.PAGE_CONTENT(text)
        mozVerify(selector, timeout = waitingTimeLong)
        mozLongClick(selector)
        return this
    }

    /**
     * Click an item on the text-selection floating action bar (e.g. "Select all", "Copy", "Search", "Private Search",
     * "Share"). Ports the legacy top-level clickContextMenuItem, which waited for the item then clicked it by exact
     * text.
     */
    fun clickContextMenuItem(item: String): BrowserPage {
        val selector = BrowserPageSelectors.TEXT_SELECTION_CONTEXT_MENU_ITEM(item)
        mozVerify(selector, timeout = waitingTime)
        mozClick(selector)
        return this
    }

    /**
     * Long-press the display-mode (browser) URL box to raise its context menu. Mirrors the legacy
     * BrowserRobot.longClickToolbar: the URL box renders as several ADDRESSBAR_URL_BOX nodes and only the last
     * (unmerged) is the on-screen one, so this cannot go through mozLongClick's single-node resolution.
     */
    fun longClickToolbar(): BrowserPage {
        composeRule.waitForIdle()
        composeRule
            .onAllNodesWithTag(ADDRESSBAR_URL_BOX, useUnmergedTree = true)
            .onLast()
            .assertExists()
            .performTouchInput { longClick() }
        return this
    }

    /** Click a context-menu item (by content description) from the long-pressed display-mode toolbar. */
    fun clickDisplayModeToolbarContextMenuItem(contentDescription: String): BrowserPage {
        mozClick(ToolbarSelectors.DISPLAY_MODE_TOOLBAR_MENU_ITEM(contentDescription))
        return this
    }

    /**
     * Assert the tab counter shows [numberOfTabs] open tabs, distinguishing the private ("Private Tabs Open: N") from
     * the normal ("Non-private Tabs Open: N") content description. Ports the legacy BrowserRobot.verifyTabCounter.
     */
    fun verifyTabCounter(numberOfTabs: String, isPrivateBrowsingEnabled: Boolean = false): BrowserPage {
        // Compose (not UIAutomator) content-description: the UIAutomator accessibility tree lags the
        // Compose tree for this counter by seconds, so a UIAutomator check can still read the old count
        // after a new tab has already opened. The Compose tree reflects the new total promptly.
        val selector =
            if (isPrivateBrowsingEnabled) {
                ToolbarSelectors.PRIVATE_TAB_COUNTER_COMPOSE_WITH_COUNT(numberOfTabs)
            } else {
                ToolbarSelectors.TAB_COUNTER_COMPOSE_WITH_COUNT(numberOfTabs)
            }
        mozVerify(selector, timeout = waitingTimeLong)
        return this
    }

    /**
     * Select [webText] on the page and trigger its "Search"/"Private Search" action from the text-selection bar,
     * confirming a new tab actually opened (the counter reaches [expectedTabCount]).
     *
     * The GeckoView text-selection Search action silently no-ops when it fires cold — the item is clickable and the
     * click succeeds, but no tab opens and the page never navigates. A single-shot sequence is therefore flaky (the
     * legacy test leaned on RetryTestRule(3) to paper over it). This re-selects the text and re-triggers the action in
     * place until the new tab appears. It deliberately does NOT reload the page between attempts: the page and its
     * selectable text are still present after a no-op, and a reload's own URL-box click is itself flaky (UiObject.click
     * can return false), which would turn one dead attempt into a corrupted state.
     */
    fun searchSelectedWebText(
        webText: String,
        menuItem: String,
        expectedTabCount: String,
        isPrivateBrowsingEnabled: Boolean,
        attempts: Int = 4,
    ): BrowserPage {
        val counter =
            if (isPrivateBrowsingEnabled) {
                ToolbarSelectors.PRIVATE_TAB_COUNTER_COMPOSE_WITH_COUNT(expectedTabCount)
            } else {
                ToolbarSelectors.TAB_COUNTER_COMPOSE_WITH_COUNT(expectedTabCount)
            }
        for (attempt in 1..attempts) {
            try {
                longClickWebContentText(webText)
                clickContextMenuItem(menuItem)
                // A fired Search opens the tab within a few seconds; a no-op never resolves (the counter
                // stays put even past 25s), so a short per-attempt wait fails fast and lets us re-select
                // rather than burning the whole budget on one dead attempt.
                mozVerify(counter, timeout = waitingTime)
                return this
            } catch (e: AssertionError) {
                if (attempt == attempts) throw e
                Log.i(
                    "BrowserPage",
                    "searchSelectedWebText: '$menuItem' opened no new tab on attempt $attempt, re-selecting",
                )
            }
        }
        return this
    }

    /**
     * Type a username/password into the web login form. Submitting the form GETs to itself, so after the first save the
     * page reloads; typing the next set of credentials while that reload is still in flight silently loses them (and
     * the following submit then re-saves the previous login instead of the new one). Each attempt therefore waits for
     * the reloaded form to be present, enters the credentials, and asserts the username actually stuck — re-entering if
     * the reload wiped it. Mirrors the legacy setPageObjectText (waitForExists + retry-with-refresh) sequence.
     */
    fun setLoginFormCredentials(username: String, password: String, attempts: Int = 4): BrowserPage {
        for (attempt in 1..attempts) {
            mozVerify(BrowserPageSelectors.USERNAME_WEB_FIELD, timeout = waitingTimeLong)
            mozClearAndEnterText(username, BrowserPageSelectors.USERNAME_WEB_FIELD)
            mozClearAndEnterText(password, BrowserPageSelectors.PASSWORD_WEB_FIELD)
            try {
                mozVerify(BrowserPageSelectors.PREFILLED_USERNAME(username), timeout = 3_000)
                return this
            } catch (e: AssertionError) {
                if (attempt == attempts) throw e
                Log.i(
                    "BrowserPage",
                    "setLoginFormCredentials: username '$username' didn't stick on attempt $attempt, re-entering",
                )
            }
        }
        return this
    }

    /**
     * Open the saved-login suggestions bar over the web form's username field. Focusing the field is what summons the
     * bar, but the first focus sometimes autofills the field directly without ever showing it, so we re-focus and
     * retry. Mirrors the legacy BrowserRobot.clickSuggestedLoginsButton, which re-clicked the username field for the
     * same reason.
     */
    fun clickSuggestedLoginsBar(attempts: Int = 3): BrowserPage {
        for (attempt in 1..attempts) {
            mozClick(BrowserPageSelectors.USERNAME_WEB_FIELD)
            try {
                mozVerify(BrowserPageSelectors.SUGGESTED_LOGINS_BAR, timeout = 3_000)
                mozClick(BrowserPageSelectors.SUGGESTED_LOGINS_BAR)
                return this
            } catch (e: AssertionError) {
                if (attempt == attempts) throw e
                Log.i("BrowserPage", "clickSuggestedLoginsBar: bar absent on attempt $attempt, re-focusing username")
            }
        }
        return this
    }

    fun verifyUrl(url: String, timeout: Long = waitingTimeShort): BrowserPage {
        val expectedText = url.replace("http://", "")
        val textMatcher = hasText(expectedText, substring = true, ignoreCase = true)
        try {
            composeRule.waitUntil(timeout) {
                composeRule.onAllNodesWithTag(ADDRESSBAR_URL, useUnmergedTree = true).fetchSemanticsNodes().any {
                    textMatcher.matches(it)
                }
            }
        } catch (_: ComposeTimeoutException) {
            val actual =
                composeRule.onAllNodesWithTag(ADDRESSBAR_URL, useUnmergedTree = true).fetchSemanticsNodes().mapNotNull {
                    it.config.getOrNull(SemanticsProperties.Text)?.joinToString("")
                }
            throw AssertionError("Expected URL to contain '$expectedText' but found: $actual")
        }
        return this
    }

    /**
     * Assert the Gecko engine's font-size factor matches [textSizePercentage], mirroring the legacy
     * checkTextSizeOnWebsite: the accessibility slider ultimately drives engine.settings.fontSizeFactor, so this reads
     * the applied engine setting rather than measuring rendered text. The step math
     * (MIN_VALUE/STEP_SIZE/DECIMAL_CONVERSION) is copied from the legacy accessibility robot.
     */
    fun verifyTextSizeOnWebsite(textSizePercentage: Int): BrowserPage {
        val steps = (textSizePercentage - FONT_SIZE_MIN_VALUE) / FONT_SIZE_STEP_SIZE
        val expectedFactor =
            ((steps * FONT_SIZE_STEP_SIZE) + FONT_SIZE_MIN_VALUE).toFloat() / FONT_SIZE_DECIMAL_CONVERSION
        assertTrue(
            "Text size on website was not set to: $textSizePercentage",
            appContext.components.core.engine.settings.fontSizeFactor == expectedFactor,
        )
        return this
    }

    fun openMainMenu(): BrowserPage {
        mozClick(BrowserPageSelectors.MAIN_MENU_BUTTON)

        return this
    }

    // --- Address autofill on a web form ---

    /** Focus the web address form's street-address field, which triggers the autofill prompt. */
    fun clickAddressFormStreetField(): BrowserPage {
        // Android stylus handwriting pops a "Try out your stylus" dialog when a web text field is
        // focused, covering the page and suppressing the autofill prompt. Disable it for the run so
        // the tap behaves like the legacy environment; the dismiss below is a belt-and-suspenders
        // fallback in case the setting doesn't take effect before the first focus.
        runCatching { mDevice.executeShellCommand("settings put secure stylus_handwriting_enabled 0") }
        mozClick(BrowserPageSelectors.ADDRESS_STREET_WEB_FIELD)
        dismissKnownOverlaysIfPresent()
        return this
    }

    /**
     * Click the "Select address" header of the autofill prompt.
     *
     * Mirrors the legacy BrowserRobot.clickSelectAddressButton retry that works around
     * https://bugzilla.mozilla.org/show_bug.cgi?id=1816869 — the prompt sometimes fails to surface, so we re-focus
     * other form fields to re-trigger Gecko autofill and retry.
     */
    fun clickSelectAddressButton(): BrowserPage {
        for (i in 1..AUTOFILL_RETRY_COUNT) {
            // Clear any lingering blocking overlay (e.g. stylus prompt) before checking for the prompt.
            dismissKnownOverlaysIfPresent()
            try {
                mozVerify(BrowserPageSelectors.SELECT_ADDRESS_HEADER, timeout = 3_000)
                mozClick(BrowserPageSelectors.SELECT_ADDRESS_HEADER)
                return this
            } catch (e: AssertionError) {
                if (i == AUTOFILL_RETRY_COUNT) throw e
                // bug 1816869: the prompt sometimes fails to surface. Re-focus the street field to
                // re-trigger Gecko autofill (mozClick auto-dismisses a blocking overlay if one appears).
                mozClick(BrowserPageSelectors.ADDRESS_STREET_WEB_FIELD)
            }
        }
        return this
    }

    /** Pick the saved-address suggestion whose row contains [streetAddress]. */
    fun clickAddressSuggestion(streetAddress: String): BrowserPage {
        val suggestion = BrowserPageSelectors.ADDRESS_SUGGESTION(streetAddress)
        mozVerify(suggestion)
        mozClick(suggestion)
        return this
    }

    /** Assert the web street-address field is autofilled with [streetAddress]. */
    fun verifyAutofilledAddress(streetAddress: String): BrowserPage {
        mozVerify(BrowserPageSelectors.AUTOFILLED_STREET_ADDRESS(streetAddress), timeout = waitingTime)
        return this
    }

    // --- Credit card autofill on a web form ---

    /** Focus the web credit card form's card-number field, which triggers the autofill prompt. */
    fun clickCreditCardNumberField(): BrowserPage {
        // Same stylus-handwriting overlay as the address form: focusing a web text field pops a
        // "Try out your stylus" dialog that covers the page and suppresses the autofill prompt.
        runCatching { mDevice.executeShellCommand("settings put secure stylus_handwriting_enabled 0") }
        mozClick(BrowserPageSelectors.CREDIT_CARD_NUMBER_WEB_FIELD)
        dismissKnownOverlaysIfPresent()
        return this
    }

    /**
     * Click the "Select credit card" header of the autofill prompt.
     *
     * Mirrors [clickSelectAddressButton]'s retry around bug 1816869 — the prompt sometimes fails to surface, so we
     * re-focus the card-number field to re-trigger Gecko autofill and retry.
     */
    fun clickSelectCreditCardButton(): BrowserPage {
        for (i in 1..AUTOFILL_RETRY_COUNT) {
            dismissKnownOverlaysIfPresent()
            try {
                mozVerify(BrowserPageSelectors.SELECT_CREDIT_CARD_HEADER, timeout = 3_000)
                mozClick(BrowserPageSelectors.SELECT_CREDIT_CARD_HEADER)
                return this
            } catch (e: AssertionError) {
                if (i == AUTOFILL_RETRY_COUNT) throw e
                mozClick(BrowserPageSelectors.CREDIT_CARD_NUMBER_WEB_FIELD)
            }
        }
        return this
    }

    /**
     * Long-press the web-content link labelled [linkText] and wait for the context menu to offer [contextMenuItem],
     * refreshing the page and retrying if it does not appear.
     *
     * Mirrors the legacy BrowserRobot.longClickPageObject, whose retry is load-bearing: mozLongClick is single-shot,
     * and a long press that lands before GeckoView has settled opens no menu at all. The legacy fallback between
     * attempts is a three-dot-menu Refresh, reproduced here rather than a swipe or a scroll. The caller still clicks
     * the item; this only guarantees the menu is up.
     */
    fun longClickPageObjectUntilContextMenu(linkText: String, contextMenuItem: String): BrowserPage {
        val link = BrowserPageSelectors.PAGE_LINK(linkText)
        val menuItem = BrowserPageSelectors.CONTEXT_MENU_ITEM(contextMenuItem)
        for (i in 1..CONTEXT_MENU_RETRY_COUNT) {
            try {
                mozVerify(link, timeout = waitingTime)
                mozLongClick(link)
                mozVerify(menuItem, timeout = waitingTimeShort)
                return this
            } catch (e: AssertionError) {
                if (i == CONTEXT_MENU_RETRY_COUNT) throw e
                mozClick(BrowserPageSelectors.MAIN_MENU_BUTTON)
                mozClick(MainMenuSelectors.REFRESH_BUTTON)
                mozVerify(BrowserPageSelectors.ENGINE_VIEW, timeout = waitingTime)
            }
        }
        return this
    }

    /** Pick the saved-card suggestion whose masked number ends in [lastDigits]. */
    fun clickCreditCardSuggestion(lastDigits: String): BrowserPage {
        val suggestion = BrowserPageSelectors.CREDIT_CARD_SUGGESTION(lastDigits)
        mozVerify(suggestion)
        mozClick(suggestion)
        return this
    }

    /** Assert the web card-number field is autofilled with [number]. */
    fun verifyAutofilledCreditCard(number: String): BrowserPage {
        mozVerify(BrowserPageSelectors.AUTOFILLED_CREDIT_CARD(number), timeout = waitingTime)
        return this
    }

    /**
     * Assert whether screenshots are currently permitted on the app window.
     *
     * Android blocks screen capture whenever the window carries [WindowManager.LayoutParams.FLAG_SECURE]. Fenix sets
     * that flag on the activity window while a private tab is in the foreground and "Allow screenshots in private
     * browsing" is off, and clears it otherwise. Asserting the flag is the deterministic equivalent of trying to take a
     * screenshot, without depending on the flaky OS capture path.
     *
     * @param allowed true expects screenshots permitted (flag absent); false expects them blocked (flag present).
     */
    fun verifyScreenshotsAllowed(allowed: Boolean): BrowserPage {
        val isSecure = composeRule.runOnUiThread {
            (composeRule.activity.window.attributes.flags and WindowManager.LayoutParams.FLAG_SECURE) != 0
        }
        assertTrue(
            "Expected screenshots to be " +
                (if (allowed) "allowed (FLAG_SECURE absent)" else "blocked (FLAG_SECURE present)") +
                ", but FLAG_SECURE was ${if (isSecure) "present" else "absent"}",
            isSecure != allowed,
        )
        return this
    }

    private companion object {
        const val HTTPS_ERROR_GO_BACK = "Go Back (Recommended)"
        const val HTTPS_ERROR_CONTINUE = "Continue to HTTP Site"
        const val AUTOFILL_RETRY_COUNT = 3
        const val FONT_SIZE_STEP_SIZE = 5
        const val FONT_SIZE_MIN_VALUE = 50
        const val FONT_SIZE_DECIMAL_CONVERSION = 100f
        const val CONTEXT_MENU_RETRY_COUNT = 3
    }
}
