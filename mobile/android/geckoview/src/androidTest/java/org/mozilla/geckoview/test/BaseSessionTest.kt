/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview.test

import android.os.Parcel
import android.os.SystemClock
import android.view.KeyEvent
import androidx.test.platform.app.InstrumentationRegistry
import org.hamcrest.Matcher
import org.hamcrest.Matchers
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assume.assumeThat
import org.junit.Rule
import org.junit.rules.ErrorCollector
import org.junit.rules.RuleChain
import org.mozilla.geckoview.GeckoRuntimeSettings
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule
import org.mozilla.geckoview.test.util.TestServer
import kotlin.reflect.KClass

/**
 * Common base class for tests using GeckoSessionTestRule,
 * providing the test rule and other utilities.
 */
open class BaseSessionTest(
    noErrorCollector: Boolean = false,
    serverCustomHeaders: Map<String, String>? = null,
    responseModifiers: Map<String, TestServer.ResponseModifier>? = null,
) {
    companion object {
        const val RESUBMIT_CONFIRM = "/assets/www/resubmit.html"
        const val BEFORE_UNLOAD = "/assets/www/beforeunload.html"
        const val CLICK_TO_RELOAD_HTML_PATH = "/assets/www/clickToReload.html"
        const val CLICK_TO_REPLACE_HTML_PATH = "/assets/www/clickToReplace.html"
        const val CLIPBOARD_READ_HTML_PATH = "/assets/www/clipboard_read.html"
        const val CONTENT_CRASH_URL = "about:crashcontent"
        const val CONTENT_CRASH_JAVA_URL = "about:crashcontentjava"
        const val DND_HTML_PATH = "/assets/www/dnd.html"
        const val DND_XORIGIN_HTML_PATH = "/assets/www/dnd_xorigin.html"
        const val DOWNLOAD_HTML_PATH = "/assets/www/download.html"
        const val FORM_BLANK_HTML_PATH = "/assets/www/form_blank.html"
        const val FORMS_HTML_PATH = "/assets/www/forms.html"
        const val FORMS_XORIGIN_HTML_PATH = "/assets/www/forms_xorigin.html"
        const val FORMS2_HTML_PATH = "/assets/www/forms2.html"
        const val FORMS3_HTML_PATH = "/assets/www/forms3.html"
        const val FORMS4_HTML_PATH = "/assets/www/forms4.html"
        const val FORMS5_HTML_PATH = "/assets/www/forms5.html"
        const val SELECT_HTML_PATH = "/assets/www/select.html"
        const val SELECT_MULTIPLE_HTML_PATH = "/assets/www/select-multiple.html"
        const val SELECT_LISTBOX_HTML_PATH = "/assets/www/select-listbox.html"
        const val ADDRESS_FORM_HTML_PATH = "/assets/www/address_form.html"
        const val FORMS_AUTOCOMPLETE_HTML_PATH = "/assets/www/forms_autocomplete.html"
        const val FORMS_ID_VALUE_HTML_PATH = "/assets/www/forms_id_value.html"
        const val CC_FORM_HTML_PATH = "/assets/www/cc_form.html"
        const val FEDCM_RP_HTML_PATH = "/assets/www/fedcm_rp.html"
        const val FEDCM_IDP_MANIFEST_PATH = "/assets/www/fedcm_idp_manifest.json"
        const val HELLO_HTML_PATH = "/assets/www/hello.html"
        const val HELLO2_HTML_PATH = "/assets/www/hello2.html"
        const val HELLO_IFRAME_HTML_PATH = "/assets/www/iframe_hello.html"
        const val INPUTS_PATH = "/assets/www/inputs.html"
        const val INVALID_URI = "not a valid uri"
        const val LINKS_HTML_PATH = "/assets/www/links.html"
        const val LOREM_IPSUM_HTML_PATH = "/assets/www/loremIpsum.html"
        const val METATAGS_PATH = "/assets/www/metatags.html"
        const val MOUSE_TO_RELOAD_HTML_PATH = "/assets/www/mouseToReload.html"
        const val NEW_SESSION_CHILD_HTML_PATH = "/assets/www/newSession_child.html"
        const val NEW_SESSION_HTML_PATH = "/assets/www/newSession.html"
        const val POPUP_HTML_PATH = "/assets/www/popup.html"
        const val PRINT_CONTENT_CHANGE = "/assets/www/print_content_change.html"
        const val PRINT_IFRAME = "/assets/www/print_iframe.html"
        const val PROMPT_HTML_PATH = "/assets/www/prompts.html"
        const val SAVE_STATE_PATH = "/assets/www/saveState.html"
        const val TEST_GIF_PATH = "/assets/www/images/test.gif"
        const val TEXT_CONTROL_PATH = "/assets/www/textcontrol.html"
        const val TITLE_CHANGE_HTML_PATH = "/assets/www/titleChange.html"
        const val TRACKERS_PATH = "/assets/www/trackers.html"
        const val VIDEO_MP4_PATH = "/assets/www/mp4.html"
        const val VIDEO_WEBM_PATH = "/assets/www/webm.html"
        const val VIDEO_BAD_PATH = "/assets/www/badVideoPath.html"
        const val UNKNOWN_HOST_URI = "https://www.test.invalid/"
        const val UNKNOWN_PROTOCOL_URI = "htt://invalid"
        const val FULLSCREEN_PATH = "/assets/www/fullscreen.html"
        const val VIEWPORT_PATH = "/assets/www/viewport.html"
        const val IFRAME_REDIRECT_LOCAL = "/assets/www/iframe_redirect_local.html"
        const val IFRAME_REDIRECT_AUTOMATION = "/assets/www/iframe_redirect_automation.html"
        const val IFRAME_SANDBOX_BLOCK = "/assets/www/iframe_sandbox_block.html"
        const val IFRAME_SANDBOX_ALLOW = "/assets/www/iframe_sandbox_allow.html"
        const val AUTOPLAY_PATH = "/assets/www/autoplay.html"
        const val SIMPLE_SCROLL_TEST_PATH = "/assets/www/simple-scroll.html"
        const val SCROLL_TEST_PATH = "/assets/www/scroll.html"
        const val COLORS_HTML_PATH = "/assets/www/colors.html"
        const val FIXED_BOTTOM = "/assets/www/fixedbottom.html"
        const val FIXED_VH = "/assets/www/fixedvh.html"
        const val FIXED_PERCENT = "/assets/www/fixedpercent.html"
        const val STORAGE_TITLE_HTML_PATH = "/assets/www/reflect_local_storage_into_title.html"
        const val HUNG_SCRIPT = "/assets/www/hungScript.html"
        const val PUSH_HTML_PATH = "/assets/www/push/push.html"
        const val OPEN_WINDOW_PATH = "https://example.org/tests/junit/open_window.html"
        const val OPEN_WINDOW_TARGET_PATH = "https://example.org/tests/junit/open_window_target.html"
        const val CLICK_ACTION_PATH = "/assets/www/worker/click-action.html"
        const val DATA_URI_PATH = "/assets/www/data_uri.html"
        const val IFRAME_UNKNOWN_PROTOCOL = "/assets/www/iframe_unknown_protocol.html"
        const val MEDIA_SESSION_DOM1_PATH = "/assets/www/media_session_dom1.html"
        const val MEDIA_SESSION_DEFAULT1_PATH = "/assets/www/media_session_default1.html"
        const val PULL_TO_REFRESH_SUBFRAME_PATH = "/assets/www/pull-to-refresh-subframe.html"
        const val TOUCH_HTML_PATH = "/assets/www/touch.html"
        const val TOUCH_XORIGIN_HTML_PATH = "/assets/www/touch_xorigin.html"
        const val GETUSERMEDIA_XORIGIN_CONTAINER_HTML_PATH = "/assets/www/getusermedia_xorigin_container.html"
        const val ROOT_100_PERCENT_HEIGHT_HTML_PATH = "/assets/www/root_100_percent_height.html"
        const val ROOT_98VH_HTML_PATH = "/assets/www/root_98vh.html"
        const val ROOT_100VH_HTML_PATH = "/assets/www/root_100vh.html"
        const val IFRAME_100_PERCENT_HEIGHT_NO_SCROLLABLE_HTML_PATH = "/assets/www/iframe_100_percent_height_no_scrollable.html"
        const val IFRAME_100_PERCENT_HEIGHT_SCROLLABLE_HTML_PATH = "/assets/www/iframe_100_percent_height_scrollable.html"
        const val IFRAME_98VH_SCROLLABLE_HTML_PATH = "/assets/www/iframe_98vh_scrollable.html"
        const val IFRAME_98VH_NO_SCROLLABLE_HTML_PATH = "/assets/www/iframe_98vh_no_scrollable.html"
        const val TOUCHSTART_HTML_PATH = "/assets/www/touchstart.html"
        const val TOUCH_ACTION_HTML_PATH = "/assets/www/touch-action.html"
        const val TOUCH_ACTION_WHEEL_LISTENER_HTML_PATH = "/assets/www/touch-action-wheel-listener.html"
        const val OVERFLOW_HTML_PATH = "/assets/www/overflow.html"
        const val OVERFLOW_CLIP_HTML_PATH = "/assets/www/overflow-clip.html"
        const val OVERSCROLL_BEHAVIOR_AUTO_HTML_PATH = "/assets/www/overscroll-behavior-auto.html"
        const val OVERSCROLL_BEHAVIOR_AUTO_NONE_HTML_PATH = "/assets/www/overscroll-behavior-auto-none.html"
        const val OVERSCROLL_BEHAVIOR_NONE_AUTO_HTML_PATH = "/assets/www/overscroll-behavior-none-auto.html"
        const val OVERSCROLL_BEHAVIOR_NONE_NON_ROOT_HTML_PATH = "/assets/www/overscroll-behavior-none-on-non-root.html"
        const val SCROLL_HANDOFF_HTML_PATH = "/assets/www/scroll-handoff.html"
        const val SHOW_DYNAMIC_TOOLBAR_HTML_PATH = "/assets/www/showDynamicToolbar.html"
        const val HIDE_DYNAMIC_TOOLBAR_HTML_PATH = "/assets/www/hideDynamicToolbar.html"
        const val HIDE_DYNAMIC_TOOLBAR_ON_RESIZES_VISUAL_HTML_PATH = "/assets/www/hideDynamicToolbarOnResizesVisual.html"
        const val CONTEXT_MENU_AUDIO_HTML_PATH = "/assets/www/context_menu_audio.html"
        const val CONTEXT_MENU_IMAGE_NESTED_HTML_PATH = "/assets/www/context_menu_image_nested.html"
        const val CONTEXT_MENU_IMAGE_HTML_PATH = "/assets/www/context_menu_image.html"
        const val CONTEXT_MENU_LINK_HTML_PATH = "/assets/www/context_menu_link.html"
        const val CONTEXT_MENU_LINK_TEXT_HTML_PATH = "/assets/www/context_menu_link_text.html"
        const val CONTEXT_MENU_LINK_TEXT_HTML_NORMAL_LENGTH_PATH = "/assets/www/context_menu_normal_link_text.html"
        const val CONTEXT_MENU_VIDEO_HTML_PATH = "/assets/www/context_menu_video.html"
        const val CONTEXT_MENU_BLOB_FULL_HTML_PATH = "/assets/www/context_menu_blob_full.html"
        const val CONTEXT_MENU_BLOB_BUFFERED_HTML_PATH = "/assets/www/context_menu_blob_buffered.html"
        const val REMOTE_IFRAME = "/assets/www/accessibility/test-remote-iframe.html"
        const val LOCAL_IFRAME = "/assets/www/accessibility/test-local-iframe.html"
        const val BODY_FULLY_COVERED_BY_GREEN_ELEMENT = "/assets/www/red-background-body-fully-covered-by-green-element.html"
        const val COLOR_GRID_HTML_PATH = "/assets/www/color_grid.html"
        const val COLOR_ORANGE_BACKGROUND_HTML_PATH = "/assets/www/color_orange_background.html"
        const val FT_FONT_HTML_PATH = "/assets/www/ft_font.html"
        const val TRACEMONKEY_PDF_PATH = "/assets/www/tracemonkey.pdf"
        const val HELLO_PDF_WORLD_PDF_PATH = "/assets/www/helloPDFWorld.pdf"
        const val ORANGE_PDF_PATH = "/assets/www/orange.pdf"
        const val NO_META_VIEWPORT_HTML_PATH = "/assets/www/no-meta-viewport.html"
        const val TRANSLATIONS_EN = "/assets/www/translations-tester-en.html"
        const val TRANSLATIONS_ES = "/assets/www/translations-tester-es.html"
        const val INTERSECTION_OBSERVER_HTML_PATH = "/assets/www/intersection-observer.html"
        const val INTERSECTION_OBSERVER_DESKTOP_HTML_PATH = "/assets/www/intersection-observer-desktop.html"
        const val BUG1909181_HTML_PATH = "/assets/www/bug1909181.html"
        const val BUG1912358_HTML_PATH = "/assets/www/bug1912358.html"
        const val BUG1985669_HTML_PATH = "/assets/www/bug1985669.html"
        const val BUG1993407_HTML_PATH = "/assets/www/bug1993407.html"
        const val BUG1994311_HTML_PATH = "/assets/www/bug1994311.html"
        const val PAGE_EXTRACTION_HTML_PATH = "/assets/www/page-extraction.html"
        const val PAGE_EXTRACTION_READER_MODE_HTML_PATH = "/assets/www/readerModeArticle.html"
        const val POSITION_STICKY_HTML_PATH = "/assets/www/position-sticky.html"
        const val POSITION_STICKY_ON_MAIN_THREAD_HTML_PATH = "/assets/www/position-sticky-on-main-thread.html"
        const val INTERACTIVE_WIDGET_HTML_PATH = "/assets/www/interactive-widget.html"
        const val INTERACTIVE_WIDGET_OVERLAYS_CONTENT_HTML_PATH = "/assets/www/interactive-widget-overlays-content.html"
        const val VIEW_TRANSITION_SNAPSHOT_SIZE = "/assets/www/view-transition-snapshot-size.html"
        const val FRAMEBUSTING_CHILD_URI = "https://example.org/tests/junit/framebusting_child.html"
        const val FRAMEBUSTING_PARENT_URI = "https://example.net/tests/junit/framebusting_parent.html"
        const val DIALOG_HTML_PATH = "/assets/www/dialog.html"
        const val POPOVER_HTML_PATH = "/assets/www/popover.html"

        const val TEST_ENDPOINT = GeckoSessionTestRule.TEST_ENDPOINT
        const val TEST_HOST = GeckoSessionTestRule.TEST_HOST
        const val TEST_PORT = GeckoSessionTestRule.TEST_PORT
    }

    val sessionRule = GeckoSessionTestRule(serverCustomHeaders, responseModifiers)

    // Override this to include more `evaluate` rules in the chain
    @get:Rule
    open val rules = RuleChain.outerRule(sessionRule)

    @get:Rule var temporaryProfile = TemporaryProfileRule()

    @get:Rule val errors = ErrorCollector()

    val mainSession get() = sessionRule.session

    fun <T> assertThat(reason: String, v: T, m: Matcher<in T>) = sessionRule.checkThat(reason, v, m)
    fun <T> assertInAutomationThat(reason: String, v: T, m: Matcher<in T>) =
        if (sessionRule.env.isAutomation) {
            assertThat(reason, v, m)
        } else {
            assumeThat(reason, v, m)
        }

    init {
        if (!noErrorCollector) {
            sessionRule.errorCollector = errors
        }
    }

    fun <T> forEachCall(vararg values: T): T = sessionRule.forEachCall(*values)

    fun getTestBytes(path: String) =
        InstrumentationRegistry.getInstrumentation().targetContext.resources.assets
            .open(path.removePrefix("/assets/")).readBytes()

    fun createTestUrl(path: String) = GeckoSessionTestRule.TEST_ENDPOINT + path

    fun GeckoSession.loadTestPath(path: String) =
        this.loadUri(createTestUrl(path))

    inline fun GeckoRuntimeSettings.toParcel(lambda: (Parcel) -> Unit) {
        val parcel = Parcel.obtain()
        try {
            this.writeToParcel(parcel, 0)

            val pos = parcel.dataPosition()
            parcel.setDataPosition(0)

            lambda(parcel)

            assertThat(
                "Read parcel matches written parcel",
                parcel.dataPosition(),
                Matchers.equalTo(pos),
            )
        } finally {
            parcel.recycle()
        }
    }

    fun GeckoSession.open() =
        sessionRule.openSession(this)

    fun GeckoSession.waitForPageStop() =
        sessionRule.waitForPageStop(this)

    fun GeckoSession.waitForPageStops(count: Int) =
        sessionRule.waitForPageStops(this, count)

    fun GeckoSession.waitUntilCalled(ifce: KClass<*>, vararg methods: String) =
        sessionRule.waitUntilCalled(this, ifce, *methods)

    fun GeckoSession.waitUntilCalled(callback: Any) =
        sessionRule.waitUntilCalled(this, callback)

    fun GeckoSession.addDisplay(x: Int, y: Int) =
        sessionRule.addDisplay(this, x, y)

    fun GeckoSession.releaseDisplay() =
        sessionRule.releaseDisplay(this)

    fun GeckoSession.forCallbacksDuringWait(callback: Any) =
        sessionRule.forCallbacksDuringWait(this, callback)

    fun GeckoSession.delegateUntilTestEnd(callback: Any) =
        sessionRule.delegateUntilTestEnd(this, callback)

    fun GeckoSession.delegateDuringNextWait(callback: Any) =
        sessionRule.delegateDuringNextWait(this, callback)

    fun GeckoSession.synthesizeTap(x: Int, y: Int) =
        sessionRule.synthesizeTap(this, x, y)

    fun GeckoSession.synthesizeMouse(downTime: Long, action: Int, x: Int, y: Int, buttonState: Int) =
        sessionRule.synthesizeMouse(this, downTime, action, x, y, buttonState)

    fun GeckoSession.synthesizeMouseMove(x: Int, y: Int) =
        sessionRule.synthesizeMouseMove(this, x, y)

    fun GeckoSession.evaluateJS(js: String): Any? =
        sessionRule.evaluateJS(this, js)

    fun GeckoSession.evaluatePromiseJS(js: String): GeckoSessionTestRule.ExtensionPromise =
        sessionRule.evaluatePromiseJS(this, js)

    fun GeckoSession.waitForJS(js: String): Any? =
        sessionRule.waitForJS(this, js)

    fun GeckoSession.waitForRoundTrip() = sessionRule.waitForRoundTrip(this)

    fun GeckoSession.pressKey(keyCode: Int) {
        // Create a Promise to listen to the key event, and wait on it below.
        val promise = this.evaluatePromiseJS(
            """new Promise(r => window.addEventListener(
                    'keyup', r, { once: true }))""",
        )
        val time = SystemClock.uptimeMillis()
        val keyEvent = KeyEvent(time, time, KeyEvent.ACTION_DOWN, keyCode, 0)
        this.textInput.onKeyDown(keyCode, keyEvent)
        this.textInput.onKeyUp(
            keyCode,
            KeyEvent.changeAction(keyEvent, KeyEvent.ACTION_UP),
        )
        promise.value
    }

    fun GeckoSession.flushApzRepaints() = sessionRule.flushApzRepaints(this)

    fun GeckoSession.zoomToFocusedInput() = sessionRule.zoomToFocusedInput(this)

    fun GeckoSession.promiseAllPaintsDone() = sessionRule.promiseAllPaintsDone(this)

    fun GeckoSession.getLinkColor(selector: String) = sessionRule.getLinkColor(this, selector)

    fun GeckoSession.getWebExtensionsSchemaPermissionNames(typeNames: Array<String>): List<String> =
        sessionRule.getWebExtensionsSchemaPermissionNames(this, typeNames).asJSList<String>()

    fun GeckoSession.setResolutionAndScaleTo(resolution: Float) =
        sessionRule.setResolutionAndScaleTo(this, resolution)

    fun GeckoSession.setHandlingUserInput(handlingUserInput: Boolean) =
        sessionRule.setHandlingUserInput(this, handlingUserInput)

    fun GeckoSession.triggerCookieBannerDetected() =
        sessionRule.triggerCookieBannerDetected(this)

    fun GeckoSession.triggerCookieBannerHandled() =
        sessionRule.triggerCookieBannerHandled(this)

    fun GeckoSession.triggerTranslationsOffer() =
        sessionRule.triggerTranslationsOffer(this)

    fun GeckoSession.triggerLanguageStateChange(languageState: JSONObject) =
        sessionRule.triggerLanguageStateChange(this, languageState)

    fun GeckoSession.teardownAlertsService() =
        sessionRule.teardownAlertsService(this)

    fun GeckoSession.notifyUserGestureActivation() = sessionRule.notifyUserGestureActivation(this)

    var GeckoSession.active: Boolean
        get() = sessionRule.getActive(this)
        set(value) = setActive(value)

    @Suppress("UNCHECKED_CAST")
    fun Any?.asJsonArray(): JSONArray = this as JSONArray

    @Suppress("UNCHECKED_CAST")
    fun <V> JSONObject.asMap(): Map<String?, V?> {
        val result = HashMap<String?, V?>()
        for (key in this.keys()) {
            result[key] = this[key] as V
        }
        return result
    }

    @Suppress("UNCHECKED_CAST")
    fun <T> Any?.asJSList(): List<T> {
        val array = this.asJsonArray()
        val result = ArrayList<T>()

        for (i in 0 until array.length()) {
            result.add(array[i] as T)
        }

        return result
    }
}
