/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test

import androidx.annotation.OptIn
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.MediumTest
import org.hamcrest.Matchers.equalTo
import org.hamcrest.Matchers.nullValue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.gecko.EventDispatcher
import org.mozilla.geckoview.ExperimentalGeckoViewApi
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSessionSettings
import org.mozilla.geckoview.PictureInPictureController
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.AssertCalled
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.WithDisplay
import org.mozilla.geckoview.test.util.UiThreadUtils

@RunWith(AndroidJUnit4::class)
@MediumTest
@OptIn(ExperimentalGeckoViewApi::class)
class PictureInPictureTest : BaseSessionTest() {

    @Before
    fun setup() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                "media.autoplay.default" to 0,
                "media.geckoview.autoplay.request" to false,
            )
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun exitWithAWindowDoesNotAskTheEmbedderToClose() {
        val delegate = registerDelegate()
        val source = startPictureInPicture(delegate).source

        sessionRule.waitForResult(source.sessionPictureInPicture.exit())
        source.waitForRoundTrip()

        assertThat(
            "The embedder closed the window itself, so it must not be asked to close it",
            delegate.exitCount,
            equalTo(0),
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun exitDropsTheStateHeldForTheWindow() {
        val delegate = registerDelegate()
        val source = startPictureInPicture(delegate).source

        sessionRule.waitForResult(source.sessionPictureInPicture.exit())

        source.loadTestPath(HELLO_HTML_PATH)
        source.waitForPageStop()
        source.waitForRoundTrip()

        assertThat(
            "A window the embedder already closed must not be closed again",
            delegate.exitCount,
            equalTo(0),
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun exitPictureInPictureWithoutWindowIsNoOp() {
        val delegate = registerDelegate()

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        mainSession.sessionPictureInPicture.exit()
        mainSession.waitForRoundTrip()

        val source = startPictureInPicture(delegate).source

        assertThat(
            "A stray exit must not have left state that breaks a later request",
            delegate.enterCount,
            equalTo(1),
        )

        sessionRule.waitForResult(source.sessionPictureInPicture.exit())
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestPictureInPictureNotifiesDelegate() {
        mainSession.loadTestPath(VIDEO_WEBM_PATH)
        mainSession.waitForPageStop()
        mainSession.playVideo()

        sessionRule.delegateDuringNextWait(
            object : PictureInPictureController.SessionPictureInPicture.Delegate {
                @AssertCalled(count = 1)
                override fun onEnterPictureInPicture(
                    session: GeckoSession,
                    placementToken: String,
                ) {
                    assertThat(
                        "Should be notified for the requesting session",
                        session,
                        equalTo(mainSession),
                    )
                    assertThat(
                        "A placement token should be provided",
                        placementToken.isNotEmpty(),
                        equalTo(true),
                    )
                }
            }
        )

        mainSession.setFocused(true)
        mainSession.sessionPictureInPicture.request()
        mainSession.waitUntilCalled(
            PictureInPictureController.SessionPictureInPicture.Delegate::class,
            "onEnterPictureInPicture",
        )
        sessionRule.waitForResult(mainSession.sessionPictureInPicture.exit())
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestPictureInPictureWithoutPlayingVideoIsNoOp() {
        mainSession.loadTestPath(VIDEO_WEBM_PATH)
        mainSession.waitForPageStop()

        sessionRule.delegateUntilTestEnd(
            object : PictureInPictureController.SessionPictureInPicture.Delegate {
                @AssertCalled(false)
                override fun onEnterPictureInPicture(
                    session: GeckoSession,
                    placementToken: String,
                ) {
                    fail("A paused at start video must not enter picture-in-picture")
                }
            }
        )

        mainSession.setFocused(true)
        mainSession.sessionPictureInPicture.request()
        mainSession.waitForRoundTrip()
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestPictureInPictureAfterPauseClonesThePausedVideo() {
        val delegate = registerDelegate()

        mainSession.loadTestPath(VIDEO_WEBM_PATH)
        mainSession.waitForPageStop()
        mainSession.playVideo()
        mainSession.pauseVideo()

        mainSession.setFocused(true)
        val cloned = mainSession.sessionPictureInPicture.request()
        UiThreadUtils.waitForCondition(
            { delegate.enterCount > 0 },
            sessionRule.env.defaultTimeoutMillis,
        )
        openPlayerSession(delegate)
        sessionRule.waitForResult(cloned)

        assertThat(
            "A video paused after playing must be cloned into the player",
            sessionRule.isVideoCloning(mainSession, "video"),
            equalTo(true),
        )

        sessionRule.waitForResult(mainSession.sessionPictureInPicture.exit())
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestPictureInPicturePrefersAPlayingVideoInAnotherFrameOverAPausedFocusedOne() {
        val delegate = registerDelegate()

        mainSession.loadTestPath(VIDEO_IN_IFRAME_PATH)
        mainSession.waitForPageStop()
        mainSession.setFocused(true)
        mainSession.evaluateJS(
            """const topVideo = document.body.appendChild(document.createElement('video'));
               topVideo.id = 'top';
               topVideo.src = 'videos/gizmo.webm'"""
        )
        mainSession.playVideo()
        mainSession.pauseVideo()
        mainSession.playVideoInSubframe()
        mainSession.evaluateJS("window.focus()")

        val cloned = mainSession.sessionPictureInPicture.request()
        UiThreadUtils.waitForCondition(
            { delegate.enterCount > 0 },
            sessionRule.env.defaultTimeoutMillis,
        )
        openPlayerSession(delegate)
        sessionRule.waitForResult(cloned)

        assertThat(
            "The paused video in the focused frame must lose to the one playing in the subframe",
            sessionRule.isVideoCloning(mainSession, "#top"),
            equalTo(false),
        )

        sessionRule.waitForResult(mainSession.sessionPictureInPicture.exit())
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestPictureInPictureSkipsAPausedVideoForAPlayingOne() {
        mainSession.loadTestPath(VIDEO_WEBM_PATH)
        mainSession.waitForPageStop()

        mainSession
            .evaluatePromiseJS(
                """new Promise(r => {
                 const v = document.body.appendChild(document.createElement('video'));
                 v.id = 'second';
                 v.src = 'videos/gizmo.webm';
                 v.loop = true;
                 v.addEventListener('playing', r, { once: true });
                 v.play();
               })"""
            )
            .value

        sessionRule.delegateDuringNextWait(
            object : PictureInPictureController.SessionPictureInPicture.Delegate {
                @AssertCalled(count = 1)
                override fun onEnterPictureInPicture(
                    session: GeckoSession,
                    placementToken: String,
                ) {
                    assertThat(
                        "Should be notified for the requesting session",
                        session,
                        equalTo(mainSession),
                    )
                }
            }
        )

        mainSession.setFocused(true)
        mainSession.sessionPictureInPicture.request()
        mainSession.waitUntilCalled(
            PictureInPictureController.SessionPictureInPicture.Delegate::class,
            "onEnterPictureInPicture",
        )
        sessionRule.waitForResult(mainSession.sessionPictureInPicture.exit())
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun sourceVideoWithControlsShowsThePictureInPictureMessage() {
        val delegate = registerDelegate()
        val source = startPictureInPicture(delegate).source

        assertThat(
            "The source video must tell the user where its video went",
            source.awaitVideoStatusMessage(),
            equalTo(PICTURE_IN_PICTURE_MESSAGE),
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun sourceVideoWithoutControlsShowsThePictureInPictureMessage() {
        val delegate = registerDelegate()
        val source = startPictureInPicture(delegate, controls = false).source

        assertThat(
            "A video with no controls must show the message rather than the mobile controls",
            source.awaitVideoStatusMessage(),
            equalTo(PICTURE_IN_PICTURE_MESSAGE),
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun videoNotInPictureInPictureShowsNoMessage() {
        mainSession.loadTestPath(VIDEO_WEBM_PATH)
        mainSession.waitForPageStop()
        mainSession.playVideo()

        assertThat(
            "A video playing in the page must not claim to be in picture-in-picture",
            sessionRule.getVideoStatusMessage(mainSession, "video"),
            nullValue(),
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun videoIsClonedIntoThePlayerSession() {
        val delegate = registerDelegate()

        val source = startPictureInPicture(delegate).source

        assertThat(
            "Source video must be cloned into the player",
            sessionRule.isVideoCloning(source, "video"),
            equalTo(true),
        )
        assertThat(
            "Cloning must not have reported a failure",
            delegate.exitCount,
            equalTo(0),
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun playerSessionSharesTheSourceProcessAndGroup() {
        val delegate = registerDelegate()

        mainSession.loadTestPath(VIDEO_WEBM_PATH)
        mainSession.waitForPageStop()
        mainSession.playVideo()

        mainSession.setFocused(true)
        val cloned = mainSession.sessionPictureInPicture.request()
        UiThreadUtils.waitForCondition(
            { delegate.enterCount > 0 },
            sessionRule.env.defaultTimeoutMillis,
        )

        val pidsBefore = sessionRule.getAllSessionPids().toSortedSet()
        val player = openPlayerSession(delegate)
        sessionRule.waitForResult(cloned)

        player.loadTestPath(HELLO_HTML_PATH)
        player.waitForPageStop()

        assertThat(
            "Player must not have brought up a content process of its own",
            sessionRule.getAllSessionPids().toSortedSet(),
            equalTo(pidsBefore),
        )
        assertThat(
            "Player must join the browsing context group of the source",
            sessionRule.getSessionBrowsingContextGroupId(player),
            equalTo(sessionRule.getSessionBrowsingContextGroupId(mainSession)),
        )
        assertThat(
            "Player must share the content process of the source",
            sessionRule.getSessionPid(player),
            equalTo(sessionRule.getSessionPid(mainSession)),
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun aNewerRequestSupersedesAnUnansweredOne() {
        val delegate = registerDelegate()

        mainSession.loadTestPath(VIDEO_WEBM_PATH)
        mainSession.waitForPageStop()
        mainSession.playVideo()

        mainSession.setFocused(true)
        val superseded = mainSession.sessionPictureInPicture.request()
        UiThreadUtils.waitForCondition(
            { delegate.enterCount > 0 },
            sessionRule.env.defaultTimeoutMillis,
        )

        val current = mainSession.sessionPictureInPicture.request()
        UiThreadUtils.waitForCondition(
            { delegate.enterCount > 1 },
            sessionRule.env.defaultTimeoutMillis,
        )

        try {
            sessionRule.waitForResult(superseded)
            fail("The superseded request must not complete successfully")
        } catch (e: RuntimeException) {
            val cause = e.cause as EventDispatcher.QueryException
            assertThat(
                "The superseded request should report why it was dropped",
                cause.data as String,
                equalTo("Superseded by a newer picture-in-picture request"),
            )
        }

        openPlayerSession(delegate)
        sessionRule.waitForResult(current)
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun aRequestFromTheSameTabReplacesTheOpenWindow() {
        val delegate = registerDelegate()
        val first = startPictureInPicture(delegate)

        first.source.pauseVideo()
        first.source
            .evaluatePromiseJS(
                """new Promise(r => {
                 const v = document.body.appendChild(document.createElement('video'));
                 v.id = 'second';
                 v.src = 'videos/gizmo.webm';
                 v.loop = true;
                 v.addEventListener('playing', r, { once: true });
                 v.play();
               })"""
            )
            .value

        first.source.setFocused(true)
        val cloned = first.source.sessionPictureInPicture.request()
        UiThreadUtils.waitForCondition(
            { delegate.enterCount > 1 },
            sessionRule.env.defaultTimeoutMillis,
        )
        assertThat(
            "The window is reused for the new video, so the embedder must not be asked to close it",
            delegate.exitCount,
            equalTo(0),
        )

        first.player.close()
        openPlayerSession(delegate)
        sessionRule.waitForResult(cloned)
        first.source.waitForRoundTrip()

        assertThat(
            "The second video must be cloned into the window",
            sessionRule.isVideoCloning(first.source, "#second"),
            equalTo(true),
        )

        first.source.loadTestPath(HELLO_HTML_PATH)
        UiThreadUtils.waitForCondition(
            { delegate.exitCount > 0 },
            sessionRule.env.defaultTimeoutMillis,
        )
        assertThat(
            "Tearing the replaced player down must not drop the one replacing it",
            delegate.exitCount,
            equalTo(1),
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun aRequestForTheVideoAlreadyInTheWindowKeepsIt() {
        val delegate = registerDelegate()
        val source = startPictureInPicture(delegate).source

        source.evaluateJS("document.querySelector('video').src = 'videos/gizmo.webm?next'")
        source.playVideo()

        source.setFocused(true)
        sessionRule.waitForResult(source.sessionPictureInPicture.request())
        source.waitForRoundTrip()

        assertThat(
            "The window already shows this video, so no new one must be asked for",
            delegate.enterCount,
            equalTo(1),
        )
        assertThat(
            "The window must not be closed",
            delegate.exitCount,
            equalTo(0),
        )
        assertThat(
            "The video must still be cloned into the window",
            sessionRule.isVideoCloning(source, "video"),
            equalTo(true),
        )

        sessionRule.waitForResult(source.sessionPictureInPicture.exit())
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun aRequestFromAnotherTabReplacesTheOpenWindow() {
        val delegate = registerDelegate()

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        val first = startPictureInPicture(delegate, sessionRule.createOpenSession())

        val second = startPictureInPicture(delegate).source
        sessionRule.waitForResult(first.source.sessionPictureInPicture.exit())
        first.player.close()

        assertThat(
            "The window is reused for the new video, so the embedder must not be asked to close it",
            delegate.exitCount,
            equalTo(0),
        )

        first.source.close()
        second.waitForRoundTrip()
        assertThat(
            "The replaced tab no longer has the window, so closing it must not close the window",
            delegate.exitCount,
            equalTo(0),
        )
        assertThat(
            "The new tab's video must still be cloned into the window",
            sessionRule.isVideoCloning(second, "video"),
            equalTo(true),
        )

        sessionRule.waitForResult(second.sessionPictureInPicture.exit())
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestPictureInPictureFindsAVideoInAnUnfocusedFrame() {
        val delegate = registerDelegate()

        mainSession.loadTestPath(VIDEO_IN_IFRAME_PATH)
        mainSession.waitForPageStop()
        mainSession.setFocused(true)
        mainSession.playVideoInSubframe()
        mainSession.evaluateJS("window.focus()")

        val cloned = mainSession.sessionPictureInPicture.request()
        UiThreadUtils.waitForCondition(
            { delegate.enterCount > 0 },
            sessionRule.env.defaultTimeoutMillis,
        )
        openPlayerSession(delegate)
        sessionRule.waitForResult(cloned)
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun failureToCloneClosesTheWindow() {
        val delegate = registerDelegate()

        mainSession.loadTestPath(VIDEO_IN_IFRAME_PATH)
        mainSession.waitForPageStop()
        mainSession.setFocused(true)
        mainSession.playVideoInSubframe()

        mainSession.sessionPictureInPicture.request()
        UiThreadUtils.waitForCondition(
            { delegate.enterCount > 0 },
            sessionRule.env.defaultTimeoutMillis,
        )

        mainSession.evaluateJS("document.getElementById('pipFrame').remove()")

        openPlayerSession(delegate)

        UiThreadUtils.waitForCondition(
            { delegate.exitCount > 0 },
            sessionRule.env.defaultTimeoutMillis,
        )
        assertThat(
            "Embedder must be told to close the window it opened",
            delegate.exitCount,
            equalTo(1),
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun navigatingTheSourceAwayClosesTheWindow() {
        val delegate = registerDelegate()
        val source = startPictureInPicture(delegate).source

        source.loadTestPath(HELLO_HTML_PATH)

        UiThreadUtils.waitForCondition(
            { delegate.exitCount > 0 },
            sessionRule.env.defaultTimeoutMillis,
        )
        assertThat(
            "Embedder must be told to close the window once for the video that went away",
            delegate.exitCount,
            equalTo(1),
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun closingASessionWithoutAWindowIsNoOp() {
        val delegate = CapturingDelegate()
        val other = sessionRule.createOpenSession()
        sessionRule.delegateUntilTestEnd(other, delegate)

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        other.loadTestPath(VIDEO_WEBM_PATH)
        other.waitForPageStop()
        other.close()
        mainSession.waitForRoundTrip()

        assertThat(
            "Closing a session with no window must not report one closing",
            delegate.exitCount,
            equalTo(0),
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun closingTheSourceSessionDropsTheStateHeldForTheWindow() {
        val delegate = registerDelegate()

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        val first = startPictureInPicture(delegate, sessionRule.createOpenSession())
        first.source.close()
        first.player.close()

        startPictureInPicture(delegate)
        assertThat(
            "A request made after the source closed must get its video cloned",
            sessionRule.isVideoCloning(mainSession, "video"),
            equalTo(true),
        )
        assertThat(
            "The application closed the source itself, so it must not be asked to close the window",
            delegate.exitCount,
            equalTo(0),
        )
    }

    private class CapturingDelegate : PictureInPictureController.SessionPictureInPicture.Delegate {
        var enterCount = 0
        var exitCount = 0
        var placementToken = ""

        override fun onEnterPictureInPicture(
            session: GeckoSession,
            placementToken: String,
        ) {
            this.placementToken = placementToken
            enterCount++
        }

        override fun onExitPictureInPicture(session: GeckoSession) {
            exitCount++
        }
    }

    private class PipSessions(val source: GeckoSession, val player: GeckoSession)

    private fun registerDelegate(): CapturingDelegate {
        val delegate = CapturingDelegate()
        sessionRule.delegateUntilTestEnd(delegate)
        return delegate
    }

    private fun startPictureInPicture(
        delegate: CapturingDelegate,
        source: GeckoSession = mainSession,
        controls: Boolean = true,
    ): PipSessions {
        source.loadTestPath(VIDEO_WEBM_PATH)
        source.waitForPageStop()
        if (!controls) {
            source.evaluateJS("document.querySelector('video').removeAttribute('controls')")
        }
        source.playVideo()

        val entered = delegate.enterCount
        source.setFocused(true)
        val cloned = source.sessionPictureInPicture.request()
        UiThreadUtils.waitForCondition(
            { delegate.enterCount > entered },
            sessionRule.env.defaultTimeoutMillis,
        )

        val player = openPlayerSession(delegate, source)
        sessionRule.waitForResult(cloned)

        return PipSessions(source, player)
    }

    private fun openPlayerSession(
        delegate: CapturingDelegate,
        source: GeckoSession = mainSession,
    ): GeckoSession {
        return sessionRule.createOpenSession(
            GeckoSessionSettings.Builder(source.settings).pictureInPictureToken(delegate.placementToken).build()
        )
    }

    /** The overlay message is localized asynchronously, so it is not there the instant it is set. */
    private fun GeckoSession.awaitVideoStatusMessage(): String? {
        var message: String? = null
        UiThreadUtils.waitForCondition(
            {
                message = sessionRule.getVideoStatusMessage(this, "video")
                !message.isNullOrEmpty()
            },
            sessionRule.env.defaultTimeoutMillis,
        )
        return message
    }

    private fun GeckoSession.playVideoInSubframe() {
        val playing =
            this.evaluatePromiseJS(
                """new Promise(r => document.getElementById('pipFrame').contentDocument
                   .querySelector('video').addEventListener('playing', r, { once: true }))"""
            )
        this.evaluateJS(
            """const v = document.getElementById('pipFrame').contentDocument.querySelector('video');
               v.loop = true;
               v.play()"""
        )
        playing.value
        this.evaluateJS("document.getElementById('pipFrame').contentWindow.focus()")
    }

    private fun GeckoSession.playVideo() {
        val playing =
            this.evaluatePromiseJS(
                """new Promise(r => document.querySelector('video')
                   .addEventListener('playing', r, { once: true }))"""
            )
        this.evaluateJS("document.querySelector('video').loop = true; document.querySelector('video').play()")
        playing.value
    }

    private fun GeckoSession.pauseVideo() {
        val paused =
            this.evaluatePromiseJS(
                """new Promise(r => document.querySelector('video')
                   .addEventListener('pause', r, { once: true }))"""
            )
        // A zero-length range is not recorded in `played`, so let playback advance first.
        this.evaluateJS(
            """const v = document.querySelector('video');
               const pauseOnceAdvanced = () => {
                 if (v.currentTime > 0) {
                   v.pause();
                 } else {
                   v.addEventListener('timeupdate', pauseOnceAdvanced, { once: true });
                 }
               };
               pauseOnceAdvanced()"""
        )
        paused.value
    }

    companion object {
        private const val PICTURE_IN_PICTURE_MESSAGE = "Video is playing in Picture-in-Picture"
    }
}
