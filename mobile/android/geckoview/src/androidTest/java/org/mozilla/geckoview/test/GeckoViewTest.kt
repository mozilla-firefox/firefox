package org.mozilla.geckoview.test

import android.content.Context
import android.graphics.Matrix
import android.os.Bundle
import android.os.LocaleList
import android.os.ParcelFileDescriptor
import android.util.Pair
import android.util.SparseArray
import android.view.View
import android.view.ViewStructure
import android.view.autofill.AutofillId
import android.view.autofill.AutofillValue
import androidx.annotation.OptIn
import androidx.test.ext.junit.rules.ActivityScenarioRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import androidx.test.platform.app.InstrumentationRegistry
import org.hamcrest.Matchers.equalTo
import org.junit.*
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.rules.RuleChain
import org.junit.runner.RunWith
import org.mozilla.geckoview.Autofill
import org.mozilla.geckoview.ExperimentalGeckoViewApi
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoView
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.NullDelegate
import org.mozilla.geckoview.test.util.UiThreadUtils

@RunWith(AndroidJUnit4::class)
@LargeTest
class GeckoViewTest : BaseSessionTest() {
    val activityRule = ActivityScenarioRule(GeckoViewTestActivity::class.java)
    private val uiAutomation = InstrumentationRegistry.getInstrumentation().uiAutomation

    @get:Rule override val rules = RuleChain.outerRule(activityRule).around(sessionRule)

    @Before
    fun setup() {
        activityRule.scenario.onActivity {
            // Attach the default session from the session rule to the GeckoView
            it.view.setSession(sessionRule.session)
        }
    }

    @After
    fun cleanup() {
        activityRule.scenario.onActivity {
            it.view.releaseSession()
        }
    }

    @Test
    fun setSessionOnClosed() {
        activityRule.scenario.onActivity {
            it.view.session!!.close()
            it.view.setSession(GeckoSession())
        }
    }

    @Test
    fun setSessionOnOpenDoesNotThrow() {
        activityRule.scenario.onActivity {
            assertThat("Session is open", it.view.session!!.isOpen, equalTo(true))
            val newSession = GeckoSession()
            it.view.setSession(newSession)
            assertThat(
                "The new session should be correctly set.",
                it.view.session,
                equalTo(newSession),
            )
        }
    }

    @Test(expected = java.lang.IllegalStateException::class)
    fun displayAlreadyAcquired() {
        activityRule.scenario.onActivity {
            assertThat(
                "View should be attached",
                it.view.isAttachedToWindow(),
                equalTo(true),
            )
            it.view.session!!.acquireDisplay()
        }
    }

    @Test
    fun relaseOnDetach() {
        activityRule.scenario.onActivity {
            // The GeckoDisplay should be released when the View is detached from the window...
            it.view.onDetachedFromWindow()
            it.view.session!!.releaseDisplay(it.view.session!!.acquireDisplay())
        }
    }

    private fun waitUntilContentProcessPriority(
        high: List<GeckoSession>,
        low: List<GeckoSession>,
    ) {
        val highPids = high.map { sessionRule.getSessionPid(it) }.toSet()
        val lowPids = low.map { sessionRule.getSessionPid(it) }.toSet()
        waitUntilContentProcessPriorityByPid(highPids = highPids, lowPids = lowPids)
    }

    private fun waitUntilContentProcessPriorityByPid(
        highPids: Collection<Int>,
        lowPids: Collection<Int>,
    ) {
        UiThreadUtils.waitForCondition(
            {
                val shouldBeHighPri = getContentProcessesOomScoreAdj(highPids)
                val shouldBeLowPri = getContentProcessesOomScoreAdj(lowPids)

                // Smaller oom_score_adj indicates higher priority, with 0 indicating foreground visibility.
                // Larger oom_score_adj indicates lower priority, with 900 indicating background visibility
                // and the process may be killed.
                shouldBeHighPri.count { it == 0 } == shouldBeHighPri.size &&
                    shouldBeLowPri.count { it >= 900 } == shouldBeLowPri.size
            },
            env.defaultTimeoutMillis,
        )
    }

    /**
     * Helper function reads oom_score_adj. oom_score_adj is set by Android based on some criteria to manage process
     * priority.
     *
     * Background on oom_score_adj:
     * https://cs.android.com/android/platform/superproject/+/android-latest-release:frameworks/base/services/core/java/com/android/server/am/OomAdjuster.md
     *
     * oom_score_adj constants:
     * https://cs.android.com/android/platform/superproject/+/android-latest-release:frameworks/base/services/core/java/com/android/server/am/ProcessList.java
     */
    fun getContentProcessesOomScoreAdj(pids: Collection<Int>): List<Int> = pids.map { pid ->
        val shellCommand = uiAutomation.executeShellCommand("cat /proc/$pid/oom_score_adj")
        ParcelFileDescriptor.AutoCloseInputStream(shellCommand).use { inputStream ->
            inputStream.bufferedReader(Charsets.UTF_8).readText().trim().toInt()
        }
    }

    fun setupPriorityTest(): GeckoSession {
        // This makes the test a little bit faster
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                "dom.ipc.processPriorityManager.backgroundGracePeriodMS" to 0,
                "dom.ipc.processPriorityManager.backgroundPerceivableGracePeriodMS" to 0,
            )
        )

        val otherSession = sessionRule.createOpenSession()

        // The process manager sets newly created processes to FOREGROUND priority until they
        // are de-prioritized, so we need to activate and deactivate the session to trigger
        // a setPriority call.
        otherSession.setActive(true)
        otherSession.setActive(false)

        // Need a dummy page to be able to get the PID from the session
        otherSession.loadUri("https://example.com")
        otherSession.waitForPageStop()

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        waitUntilContentProcessPriority(
            high = listOf(mainSession),
            low = listOf(otherSession),
        )

        return otherSession
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    fun setTabActiveKeepsTabAtHighPriority() {
        activityRule.scenario.onActivity {
            val otherSession = setupPriorityTest()

            // A tab with priority hint does not get de-prioritized even when
            // the surface is destroyed
            mainSession.setPriorityHint(GeckoSession.PRIORITY_HIGH)

            // This will destroy mainSession's surface and create a surface for otherSession
            it.view.setSession(otherSession)

            waitUntilContentProcessPriority(high = listOf(mainSession, otherSession), low = listOf())

            // Destroying otherSession's surface should leave mainSession as the sole high priority
            // tab
            it.view.releaseSession()

            waitUntilContentProcessPriority(high = listOf(mainSession), low = listOf())

            // Cleanup
            mainSession.setPriorityHint(GeckoSession.PRIORITY_DEFAULT)
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    fun processPriorityTest() {
        activityRule.scenario.onActivity {
            val otherSession = setupPriorityTest()

            // After setting otherSession to the view, otherSession should be high priority
            // and mainSession should be de-prioritized
            it.view.setSession(otherSession)

            waitUntilContentProcessPriority(
                high = listOf(otherSession),
                low = listOf(mainSession),
            )

            // After releasing otherSession, both sessions should be low priority
            it.view.releaseSession()

            waitUntilContentProcessPriority(
                high = listOf(),
                low = listOf(mainSession, otherSession),
            )

            // Test that re-setting mainSession in the view raises the priority again
            it.view.setSession(mainSession)
            waitUntilContentProcessPriority(
                high = listOf(mainSession),
                low = listOf(otherSession),
            )

            // Setting the session to active should also raise priority
            otherSession.setActive(true)
            waitUntilContentProcessPriority(
                high = listOf(mainSession, otherSession),
                low = listOf(),
            )
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    @OptIn(ExperimentalGeckoViewApi::class)
    fun viewKeepsKeptActiveSessionPlayingWhenShowingAnotherSession() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                "media.autoplay.default" to 0,
                "media.geckoview.autoplay.request" to false,
            )
        )

        activityRule.scenario.onActivity {
            mainSession.loadTestPath(VIDEO_WEBM_PATH)
            mainSession.waitForPageStop()
            // Pause the video whenever the page is hidden, as many video sites do.
            mainSession.evaluateJS(
                """document.addEventListener('visibilitychange', () => {
                     if (document.hidden) document.querySelector('video').pause();
                   })"""
            )
            val playing =
                mainSession.evaluatePromiseJS(
                    """new Promise(r => document.querySelector('video')
                       .addEventListener('playing', r, { once: true }))"""
                )
            mainSession.evaluateJS(
                "document.querySelector('video').loop = true; document.querySelector('video').play()"
            )
            playing.value

            mainSession.sessionPictureInPicture.setKeepActive(true)

            it.view.setSession(sessionRule.createOpenSession())
            mainSession.waitForRoundTrip()

            assertThat(
                "Page must stay visible while kept active",
                mainSession.evaluateJS("document.visibilityState") as String,
                equalTo("visible"),
            )
            assertThat(
                "Video must keep playing while kept active",
                mainSession.evaluateJS("document.querySelector('video').paused") as Boolean,
                equalTo(false),
            )
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    fun viewSetsSessionActiveWhileShowingIt() {
        activityRule.scenario.onActivity {
            mainSession.loadTestPath(HELLO_HTML_PATH)
            mainSession.waitForPageStop()
            val otherSession = sessionRule.createOpenSession()
            otherSession.loadTestPath(HELLO_HTML_PATH)
            otherSession.waitForPageStop()

            // This will destroy mainSession's surface and create a surface for otherSession
            it.view.setSession(otherSession)
            assertThat(
                "docShell shouldn't be active once the view shows another session",
                mainSession.active,
                equalTo(false),
            )
            assertThat(
                "docShell should be active while the view shows it",
                otherSession.active,
                equalTo(true),
            )

            // This will destroy otherSession's surface
            it.view.releaseSession()
            assertThat(
                "docShell shouldn't be active once the view releases it",
                otherSession.active,
                equalTo(false),
            )

            // Re-setting mainSession in the view should activate it again
            it.view.setSession(mainSession)
            assertThat(
                "docShell should be active once the view shows it again",
                mainSession.active,
                equalTo(true),
            )
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    @OptIn(ExperimentalGeckoViewApi::class)
    fun viewKeepsKeptActiveSessionActiveWhenShowingAnotherSession() {
        activityRule.scenario.onActivity {
            mainSession.loadTestPath(HELLO_HTML_PATH)
            mainSession.waitForPageStop()

            mainSession.sessionPictureInPicture.setKeepActive(true)

            // This will destroy mainSession's surface and create a surface for the other session
            it.view.setSession(sessionRule.createOpenSession())

            assertThat(
                "docShell should stay active while kept active",
                mainSession.active,
                equalTo(true),
            )
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    @OptIn(ExperimentalGeckoViewApi::class)
    fun viewDeactivatesSessionOnceKeepActiveIsCleared() {
        activityRule.scenario.onActivity {
            mainSession.loadTestPath(HELLO_HTML_PATH)
            mainSession.waitForPageStop()

            mainSession.sessionPictureInPicture.setKeepActive(true)
            mainSession.sessionPictureInPicture.setKeepActive(false)

            it.view.setSession(sessionRule.createOpenSession())

            assertThat(
                "docShell shouldn't be active once no longer kept active",
                mainSession.active,
                equalTo(false),
            )
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    @OptIn(ExperimentalGeckoViewApi::class)
    fun viewActivatesKeptActiveSessionWhenShowingItAgain() {
        activityRule.scenario.onActivity {
            mainSession.loadTestPath(HELLO_HTML_PATH)
            mainSession.waitForPageStop()

            // This will destroy mainSession's surface and deactivate it, as it is not kept active yet
            it.view.setSession(sessionRule.createOpenSession())
            assertThat(
                "docShell shouldn't be active once the view shows another session",
                mainSession.active,
                equalTo(false),
            )

            mainSession.sessionPictureInPicture.setKeepActive(true)

            // Re-setting mainSession in the view should activate it again
            it.view.setSession(mainSession)
            assertThat(
                "docShell should be active once the view shows it again",
                mainSession.active,
                equalTo(true),
            )
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    @OptIn(ExperimentalGeckoViewApi::class)
    fun viewKeepsKeptActiveSessionActiveWhenReleasingIt() {
        activityRule.scenario.onActivity {
            mainSession.loadTestPath(HELLO_HTML_PATH)
            mainSession.waitForPageStop()

            mainSession.sessionPictureInPicture.setKeepActive(true)

            // This will destroy mainSession's surface
            it.view.releaseSession()

            assertThat(
                "docShell should stay active while kept active",
                mainSession.active,
                equalTo(true),
            )
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    @OptIn(ExperimentalGeckoViewApi::class)
    fun clearingKeepActiveReportsDeactivationSkippedWhileShowingAnotherSession() {
        activityRule.scenario.onActivity {
            mainSession.loadTestPath(HELLO_HTML_PATH)
            mainSession.waitForPageStop()

            mainSession.sessionPictureInPicture.setKeepActive(true)
            it.view.setSession(sessionRule.createOpenSession())

            assertThat(
                "Clearing must report the skipped deactivation",
                mainSession.sessionPictureInPicture.setKeepActive(false),
                equalTo(true),
            )
            assertThat(
                "Clearing again must not report it twice",
                mainSession.sessionPictureInPicture.setKeepActive(false),
                equalTo(false),
            )
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    @OptIn(ExperimentalGeckoViewApi::class)
    fun clearingKeepActiveReportsDeactivationSkippedWhileReleased() {
        activityRule.scenario.onActivity {
            mainSession.loadTestPath(HELLO_HTML_PATH)
            mainSession.waitForPageStop()

            mainSession.sessionPictureInPicture.setKeepActive(true)
            it.view.releaseSession()

            assertThat(
                "Clearing must report the skipped deactivation",
                mainSession.sessionPictureInPicture.setKeepActive(false),
                equalTo(true),
            )
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    @OptIn(ExperimentalGeckoViewApi::class)
    fun clearingKeepActiveReportsNothingWhileViewStillShowsSession() {
        activityRule.scenario.onActivity {
            mainSession.loadTestPath(HELLO_HTML_PATH)
            mainSession.waitForPageStop()

            mainSession.sessionPictureInPicture.setKeepActive(true)

            assertThat(
                "Clearing must not report a deactivation that never happened",
                mainSession.sessionPictureInPicture.setKeepActive(false),
                equalTo(false),
            )
            assertThat(
                "docShell should stay active while the view shows it",
                mainSession.active,
                equalTo(true),
            )
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    @OptIn(ExperimentalGeckoViewApi::class)
    fun clearingKeepActiveReportsNothingOnceViewShowsSessionAgain() {
        activityRule.scenario.onActivity {
            mainSession.loadTestPath(HELLO_HTML_PATH)
            mainSession.waitForPageStop()

            mainSession.sessionPictureInPicture.setKeepActive(true)
            it.view.setSession(sessionRule.createOpenSession())
            it.view.setSession(mainSession)

            assertThat(
                "Showing the session again must cancel the skipped deactivation",
                mainSession.sessionPictureInPicture.setKeepActive(false),
                equalTo(false),
            )
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    @OptIn(ExperimentalGeckoViewApi::class)
    fun clearingKeepActiveReportsNothingWhenSessionHiddenBeforeKeptActive() {
        activityRule.scenario.onActivity {
            mainSession.loadTestPath(HELLO_HTML_PATH)
            mainSession.waitForPageStop()

            it.view.setSession(sessionRule.createOpenSession())
            mainSession.sessionPictureInPicture.setKeepActive(true)

            assertThat(
                "A deactivation before keeping active must not be reported",
                mainSession.sessionPictureInPicture.setKeepActive(false),
                equalTo(false),
            )
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    fun setPriorityHint() {
        val otherSession = setupPriorityTest()

        // Setting priorityHint to PRIORITY_HIGH raises priority
        otherSession.setPriorityHint(GeckoSession.PRIORITY_HIGH)

        waitUntilContentProcessPriority(
            high = listOf(mainSession, otherSession),
            low = listOf(),
        )

        // Setting priorityHint to PRIORITY_DEFAULT should lower priority
        otherSession.setPriorityHint(GeckoSession.PRIORITY_DEFAULT)

        waitUntilContentProcessPriority(
            high = listOf(mainSession),
            low = listOf(otherSession),
        )
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    fun setActiveProcessPriorityTest() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                "dom.ipc.processPriorityManager.backgroundGracePeriodMS" to 0,
                "dom.ipc.processPriorityManager.backgroundPerceivableGracePeriodMS" to 0,
                "fission.webContentIsolationStrategy" to 1,
            )
        )

        // Can't use getSessionPid until the session is loaded,
        // but we can safely assume the opening session is the only PID
        val initialPids = sessionRule.getAllSessionPids()
        assertTrue("Only one session PID detected on startup", initialPids.size == 1)
        val initialPid = initialPids.first()
        val initialOomScoreAdj = getContentProcessesOomScoreAdj(listOf(initialPid)).first()

        mainSession.setActive(true)
        mainSession.setActive(false)

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()
        val loadedPid = sessionRule.getSessionPid(mainSession)
        val loadedOomScoreAdj = getContentProcessesOomScoreAdj(listOf(loadedPid)).first()

        val isLoadedActive = sessionRule.getActive(mainSession)
        assertFalse("The session was set to inactive.", isLoadedActive)

        assertTrue("A process switch did occur for fission, so the PIDs are different.", initialPid != loadedPid)

        // Note that higher oom score means less priority
        assertTrue(
            "The initial oom score adj has more priority than the loaded oom score because it was backgrounded.",
            loadedOomScoreAdj > initialOomScoreAdj,
        )
        assertTrue(
            "The initial oom score adj indicates higher priority because it started in the foreground.",
            initialOomScoreAdj == 0,
        )
        assertTrue(
            "The loaded oom score adj indicates lower priority because it is backgrounded.",
            loadedOomScoreAdj == 900,
        )
    }

    private fun visit(
        node: MockViewStructure,
        callback: (MockViewStructure) -> Unit,
    ) {
        callback(node)

        for (child in node.children) {
            if (child != null) {
                visit(child, callback)
            }
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    fun autofillWithNoSession() {
        mainSession.loadTestPath(FORMS_XORIGIN_HTML_PATH)
        mainSession.waitForPageStop()

        val autofills =
            mapOf(
                "#user1" to "username@example.com",
                "#pass1" to "test-password",
            )
        val notFilled = listOf("#user2", "#pass2")

        // Only the form containing the focused field is filled.
        val promises = autofills.map { entry ->
            mainSession.evaluatePromiseJS(
                """
                getData('${entry.key}', '${entry.value}')
                """
            )
        }

        mainSession.evaluateJS("document.querySelector('#user1').focus()")
        UiThreadUtils.waitForCondition(
            { mainSession.autofillSession.focused != null },
            env.defaultTimeoutMillis,
        )

        activityRule.scenario.onActivity {
            val root = MockViewStructure(View.NO_ID)
            it.view.onProvideAutofillVirtualStructure(root, 0)

            val data = SparseArray<AutofillValue>()
            visit(root) { node ->
                if (node.hints?.indexOf(View.AUTOFILL_HINT_USERNAME) != -1) {
                    data.set(node.id, AutofillValue.forText("username@example.com"))
                } else if (node.hints?.indexOf(View.AUTOFILL_HINT_PASSWORD) != -1) {
                    data.set(node.id, AutofillValue.forText("test-password"))
                }
            }

            // Releasing the session will set mSession in GeckoView to null
            // this test verifies that we can still autofill correctly even in released state
            val session = it.view.releaseSession()!!
            it.view.autofill(data)

            // Put back the session and verifies that the autofill went through anyway
            it.view.setSession(session)

            // Wait on the promises and check for correct values.
            for (values in promises.map { p -> p.value.asJSList<String>() }) {
                val (key, actual, expected, eventInterface) = values

                assertThat("Auto-filled value must match ($key)", actual, equalTo(expected))
                assertThat(
                    "input event should be dispatched with InputEvent interface",
                    eventInterface,
                    equalTo("InputEvent"),
                )
            }
        }

        for (key in autofills.keys + notFilled) {
            assertThat(
                "Field in the unfocused iframe must not be filled ($key)",
                mainSession.evaluatePromiseJS("window.getIframeValue('$key')").value as String,
                equalTo("foo"),
            )
        }

        for (key in notFilled) {
            assertThat(
                "Field outside the focused form must not be filled ($key)",
                mainSession.evaluateJS("document.querySelector('$key').value") as String,
                equalTo("foo"),
            )
        }
    }

    @Test
    @NullDelegate(Autofill.Delegate::class)
    fun activityContextDelegate() {
        var delegateCalled = false
        activityRule.scenario.onActivity {
            class TestActivityDelegate : GeckoView.ActivityContextDelegate {
                override fun getActivityContext(): Context {
                    delegateCalled = true
                    return it
                }
            }
            // Set view delegate
            it.view.activityContextDelegate = TestActivityDelegate()
            val context = it.view.activityContextDelegate?.activityContext
            assertTrue("The activity context delegate was called.", delegateCalled)
            assertTrue("The activity context delegate provided the expected context.", context == it)
        }
    }

    class MockViewStructure(
        var id: Int,
        var parent: MockViewStructure? = null,
    ) : ViewStructure() {
        private var enabled: Boolean = false
        private var inputType = 0
        var children = Array<MockViewStructure?>(0, { null })
        var childIndex = 0
        var hints: Array<out String>? = null

        override fun setId(
            p0: Int,
            p1: String?,
            p2: String?,
            p3: String?,
        ) {
            id = p0
        }

        override fun setEnabled(p0: Boolean) {
            enabled = p0
        }

        override fun setChildCount(p0: Int) {
            children = Array(p0, { null })
        }

        override fun getChildCount(): Int = children.size

        override fun newChild(p0: Int): ViewStructure {
            val child = MockViewStructure(p0, this)
            children[childIndex++] = child
            return child
        }

        override fun asyncNewChild(p0: Int): ViewStructure = newChild(p0)

        override fun setInputType(p0: Int) {
            inputType = p0
        }

        fun getInputType(): Int = inputType

        override fun setAutofillHints(p0: Array<out String>?) {
            hints = p0
        }

        override fun addChildCount(p0: Int): Int {
            TODO()
        }

        override fun setDimens(
            p0: Int,
            p1: Int,
            p2: Int,
            p3: Int,
            p4: Int,
            p5: Int,
        ) {}

        override fun setTransformation(p0: Matrix?) {}

        override fun setElevation(p0: Float) {}

        override fun setAlpha(p0: Float) {}

        override fun setVisibility(p0: Int) {}

        override fun setClickable(p0: Boolean) {}

        override fun setLongClickable(p0: Boolean) {}

        override fun setContextClickable(p0: Boolean) {}

        override fun setFocusable(p0: Boolean) {}

        override fun setFocused(p0: Boolean) {}

        override fun setAccessibilityFocused(p0: Boolean) {}

        override fun setCheckable(p0: Boolean) {}

        override fun setChecked(p0: Boolean) {}

        override fun setSelected(p0: Boolean) {}

        override fun setActivated(p0: Boolean) {}

        override fun setOpaque(p0: Boolean) {}

        override fun setClassName(p0: String?) {}

        override fun setContentDescription(p0: CharSequence?) {}

        override fun setText(p0: CharSequence?) {}

        override fun setText(
            p0: CharSequence?,
            p1: Int,
            p2: Int,
        ) {}

        override fun setTextStyle(
            p0: Float,
            p1: Int,
            p2: Int,
            p3: Int,
        ) {}

        override fun setTextLines(
            p0: IntArray?,
            p1: IntArray?,
        ) {}

        override fun setHint(p0: CharSequence?) {}

        override fun getText(): CharSequence = ""

        override fun getTextSelectionStart(): Int = 0

        override fun getTextSelectionEnd(): Int = 0

        override fun getHint(): CharSequence = ""

        override fun getExtras(): Bundle = Bundle()

        override fun hasExtras(): Boolean = false

        override fun getAutofillId(): AutofillId? = null

        override fun setAutofillId(p0: AutofillId) {}

        override fun setAutofillId(
            p0: AutofillId,
            p1: Int,
        ) {}

        override fun setAutofillType(p0: Int) {}

        override fun setAutofillValue(p0: AutofillValue?) {}

        override fun setAutofillOptions(p0: Array<out CharSequence>?) {}

        override fun setDataIsSensitive(p0: Boolean) {}

        override fun asyncCommit() {}

        override fun setWebDomain(p0: String?) {}

        override fun setLocaleList(p0: LocaleList?) {}

        override fun newHtmlInfoBuilder(p0: String): HtmlInfo.Builder = MockHtmlInfoBuilder()

        override fun setHtmlInfo(p0: HtmlInfo) {}
    }

    class MockHtmlInfoBuilder : ViewStructure.HtmlInfo.Builder() {
        override fun addAttribute(
            p0: String,
            p1: String,
        ): ViewStructure.HtmlInfo.Builder = this

        override fun build(): ViewStructure.HtmlInfo = MockHtmlInfo()
    }

    class MockHtmlInfo : ViewStructure.HtmlInfo() {
        override fun getTag(): String {
            TODO("Not yet implemented")
        }

        override fun getAttributes(): MutableList<Pair<String, String>>? {
            TODO("Not yet implemented")
        }
    }
}
