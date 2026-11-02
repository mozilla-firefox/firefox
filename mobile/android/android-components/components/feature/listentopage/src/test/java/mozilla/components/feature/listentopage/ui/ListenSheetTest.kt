/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.requiredWidth
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertWidthIsEqualTo
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasStateDescription
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performSemanticsAction
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import java.util.Locale
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.listentopage.ArticleProgress
import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.ListenError
import mozilla.components.feature.listentopage.PlaybackPhase
import mozilla.components.feature.listentopage.PlaybackSpeed
import mozilla.components.feature.listentopage.R
import mozilla.components.feature.listentopage.VoiceState
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

// The expanded player is taller than Robolectric's default window, so `assertIsDisplayed` on anything
// below the progress bar needs a window the whole card fits in.
@RunWith(RobolectricTestRunner::class)
@Config(qualifiers = "w840dp-h1200dp")
class ListenSheetTest {

    @get:Rule val composeTestRule = createComposeRule()

    private val forward = testContext.getString(R.string.mozac_feature_listentopage_forward_30_sec)
    private val rewind = testContext.getString(R.string.mozac_feature_listentopage_back_10_sec)
    private val pause = testContext.getString(R.string.mozac_feature_listentopage_pause)
    private val play = testContext.getString(R.string.mozac_feature_listentopage_play)
    private val close = testContext.getString(R.string.mozac_feature_listentopage_close)
    private val voiceOptions = testContext.getString(R.string.mozac_feature_listentopage_voice_options)
    private val playbackSpeed = testContext.getString(R.string.mozac_feature_listentopage_playback_speed)
    private val expandedState = testContext.getString(R.string.mozac_feature_listentopage_dropdown_expanded)

    // The track hangs off the left of the window, so the drag starts inside the bar. It ends past the bar so that the
    // thumb lands on the end. The first move only takes the finger past the touch slop.
    private fun dragThumbPastEnd(release: Boolean) {
        composeTestRule.onNodeWithTag(AUDIO_PROGRESS_BAR_TEST_TAG_EXPANDED).performTouchInput {
            down(center)
            moveTo(percentOffset(0.75f, 0.5f))
            moveTo(percentOffset(1.2f, 0.5f))
            if (release) up()
        }
    }

    private fun setContent(
        expanded: Boolean,
        onExpandClicked: () -> Unit = {},
        onAction: (ListenAction) -> Unit = {},
        articleProgress: ArticleProgress = ArticleProgress(LESS_THAN_ONE_SEC_ELAPSED_TIME_MS, TOTAL_DURATION_MS),
        playbackPhase: PlaybackPhase = PlaybackPhase.Playing,
        error: ListenError? = null,
        availableWidth: Dp = NARROW_WINDOW_WIDTH,
    ) {
        composeTestRule.setContent {
            AcornTheme {
                Box(modifier = Modifier.requiredWidth(availableWidth)) {
                    ListenSheet(
                        article = ArticleDetails(title = TITLE, site = "bbc.co.uk"),
                        articleProgressState = remember { mutableStateOf(articleProgress) },
                        playbackPhase = playbackPhase,
                        speed = PlaybackSpeed.Default,
                        voiceState = VoiceState(),
                        onAction = onAction,
                        onExpandClicked = onExpandClicked,
                        expanded = expanded,
                        error = error,
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            }
        }
    }

    @Test
    fun `WHEN the sheet is expanded THEN the full controls are shown`() {
        setContent(expanded = true)

        composeTestRule.onNodeWithTag(PLAY_PAUSE_BUTTON_TEST_TAG).assertExists()
        composeTestRule.onNodeWithContentDescription(playbackSpeed).assertExists()
        composeTestRule.onNodeWithContentDescription(voiceOptions).assertExists()
        composeTestRule.onNodeWithContentDescription(close).assertExists()
        composeTestRule.onNodeWithContentDescription(rewind).assertExists()
        composeTestRule.onNodeWithContentDescription(forward).assertExists()
        composeTestRule.onNodeWithTag(AUDIO_PROGRESS_BAR_TEST_TAG_EXPANDED).assertExists()
        composeTestRule.onNodeWithTag(AUDIO_PROGRESS_BAR_TEST_TAG_COMPACT).assertDoesNotExist()
        composeTestRule.onNodeWithTag(ELAPSED_TIME_TEST_TAG, useUnmergedTree = true).assertDoesNotExist()
    }

    @Test
    fun `WHEN the sheet is compact THEN only the compact controls are shown`() {
        setContent(expanded = false)

        composeTestRule.onNodeWithTag(PLAY_PAUSE_BUTTON_TEST_TAG).assertExists()
        composeTestRule.onNodeWithContentDescription(playbackSpeed).assertDoesNotExist()
        composeTestRule.onNodeWithContentDescription(voiceOptions).assertDoesNotExist()
        composeTestRule.onNodeWithContentDescription(close).assertExists()
        composeTestRule.onNodeWithContentDescription(rewind).assertExists()
        composeTestRule.onNodeWithContentDescription(forward).assertDoesNotExist()
        composeTestRule.onNodeWithTag(AUDIO_PROGRESS_BAR_TEST_TAG_EXPANDED).assertDoesNotExist()
        composeTestRule.onNodeWithTag(AUDIO_PROGRESS_BAR_TEST_TAG_COMPACT).assertExists()
        composeTestRule.onNodeWithTag(ELAPSED_TIME_TEST_TAG, useUnmergedTree = true).assertDoesNotExist()
    }

    @Test
    fun `GIVEN the sheet is compact WHEN the article heading is clicked THEN expanding is requested`() {
        var expandClicked = false
        setContent(expanded = false, onExpandClicked = { expandClicked = true })

        composeTestRule.onNodeWithText(TITLE).performClick()

        assertTrue(expandClicked)
    }

    // Play button tests
    @Test
    fun `GIVEN playing WHEN shown THEN the playback button has a pause label`() {
        setContent(expanded = false, playbackPhase = PlaybackPhase.Playing)

        composeTestRule.onNodeWithTag(PLAY_PAUSE_BUTTON_TEST_TAG).assert(hasContentDescription(pause))
    }

    @Test
    fun `GIVEN buffering WHEN shown THEN the playback button has a pause label`() {
        setContent(expanded = false, playbackPhase = PlaybackPhase.Buffering)

        composeTestRule.onNodeWithTag(PLAY_PAUSE_BUTTON_TEST_TAG).assert(hasContentDescription(pause))
    }

    @Test
    fun `GIVEN an article with no length yet WHEN shown THEN the bar cannot be dragged`() {
        setContent(expanded = true, articleProgress = ArticleProgress())

        composeTestRule.onNodeWithTag(AUDIO_PROGRESS_BAR_TEST_TAG_EXPANDED).assertIsNotEnabled()
    }

    @Test
    fun `GIVEN an article with a length WHEN shown THEN the bar can be dragged`() {
        setContent(expanded = true, articleProgress = ArticleProgress(positionMs = 60_000, durationMs = 600_000))

        composeTestRule.onNodeWithTag(AUDIO_PROGRESS_BAR_TEST_TAG_EXPANDED).assertIsEnabled()
    }

    @Test
    fun `GIVEN buffering WHEN it lasts past the delay THEN the playback button shows a loading indicator`() {
        composeTestRule.mainClock.autoAdvance = false
        setContent(expanded = false, playbackPhase = PlaybackPhase.Buffering)

        composeTestRule.mainClock.advanceTimeBy(LOADING_INDICATOR_DELAY_MS / 2)
        composeTestRule.onNodeWithTag(PLAY_PAUSE_BUTTON_LOADING_TEST_TAG).assertDoesNotExist()

        composeTestRule.mainClock.advanceTimeBy(LOADING_INDICATOR_DELAY_MS)
        composeTestRule.onNodeWithTag(PLAY_PAUSE_BUTTON_LOADING_TEST_TAG).assertExists()
    }

    @Test
    fun `GIVEN playing WHEN shown THEN the playback button shows no loading indicator`() {
        composeTestRule.mainClock.autoAdvance = false
        setContent(expanded = false, playbackPhase = PlaybackPhase.Playing)

        composeTestRule.mainClock.advanceTimeBy(LOADING_INDICATOR_DELAY_MS * 2)

        composeTestRule.onNodeWithTag(PLAY_PAUSE_BUTTON_LOADING_TEST_TAG).assertDoesNotExist()
    }

    @Test
    fun `GIVEN paused WHEN shown THEN the playback button has a play label`() {
        setContent(expanded = false, playbackPhase = PlaybackPhase.Paused)

        composeTestRule.onNodeWithTag(PLAY_PAUSE_BUTTON_TEST_TAG).assert(hasContentDescription(play))
    }

    // Rewind button tests
    @Test
    fun `WHEN audio has played less than 1 sec THEN the rewind button is disabled`() {
        setContent(
            expanded = false,
            playbackPhase = PlaybackPhase.Playing,
            articleProgress = ArticleProgress(LESS_THAN_ONE_SEC_ELAPSED_TIME_MS, TOTAL_DURATION_MS),
        )

        composeTestRule.onNodeWithContentDescription(rewind).assertIsNotEnabled()
    }

    @Test
    fun `WHEN audio has played more than 1 sec THEN the rewind button is enabled`() {
        setContent(
            expanded = false,
            playbackPhase = PlaybackPhase.Playing,
            articleProgress = ArticleProgress(MORE_THAN_ONE_SEC_ELAPSED_TIME_MS, TOTAL_DURATION_MS),
        )

        composeTestRule.onNodeWithContentDescription(rewind).assertIsEnabled()
    }

    // Error state tests
    @Test
    fun `GIVEN an error WHEN shown THEN the error message is shown`() {
        setContent(expanded = false, playbackPhase = PlaybackPhase.Playing, error = ListenError.PlaybackFailed)

        val listenErrorMessage = testContext.getString(ListenError.PlaybackFailed.userFacingMessage)
        composeTestRule.onNodeWithText(listenErrorMessage).assertExists()
        composeTestRule.onNodeWithTag(PLAY_PAUSE_BUTTON_TEST_TAG).assert(hasContentDescription(listenErrorMessage))
    }

    @Test
    fun `GIVEN an error WHEN shown expanded THEN all buttons are disabled except for the close button`() {
        setContent(
            expanded = true,
            articleProgress = ArticleProgress(MORE_THAN_ONE_SEC_ELAPSED_TIME_MS, TOTAL_DURATION_MS),
            playbackPhase = PlaybackPhase.Playing,
            error = ListenError.PlaybackFailed,
        )
        composeTestRule.onNodeWithTag(PLAY_PAUSE_BUTTON_TEST_TAG).assertIsNotEnabled()
        composeTestRule.onNodeWithTag(AUDIO_PROGRESS_BAR_TEST_TAG_EXPANDED).assertIsNotEnabled()
        composeTestRule.onNodeWithContentDescription(playbackSpeed).assertIsNotEnabled()
        composeTestRule.onNodeWithContentDescription(voiceOptions).assertIsNotEnabled()
        composeTestRule.onNodeWithContentDescription(rewind).assertIsNotEnabled()
        composeTestRule.onNodeWithContentDescription(forward).assertIsNotEnabled()
        composeTestRule.onNodeWithContentDescription(close).assertIsEnabled()
    }

    @Test
    fun `GIVEN an error WHEN shown compact THEN all buttons are disabled except for the close button`() {
        setContent(
            expanded = false,
            articleProgress = ArticleProgress(MORE_THAN_ONE_SEC_ELAPSED_TIME_MS, TOTAL_DURATION_MS),
            playbackPhase = PlaybackPhase.Playing,
            error = ListenError.PlaybackFailed,
        )
        composeTestRule.onNodeWithTag(PLAY_PAUSE_BUTTON_TEST_TAG).assertIsNotEnabled()
        // The compact bar is a Canvas with no interaction, so it carries no enabled semantics to assert on.
        composeTestRule.onNodeWithTag(AUDIO_PROGRESS_BAR_TEST_TAG_COMPACT).assertExists()
        composeTestRule.onNodeWithContentDescription(rewind).assertIsNotEnabled()
        composeTestRule.onNodeWithContentDescription(close).assertIsEnabled()
    }

    // Screen size adaptability tests
    @Test
    fun `GIVEN a window wider than the player WHEN the sheet is compact THEN the player stops growing at its max width`() {
        setContent(expanded = false, availableWidth = WIDE_WINDOW_WIDTH)

        composeTestRule.onNodeWithTag(LISTEN_SHEET_PLAYER_CARD_TEST_TAG).assertWidthIsEqualTo(PlayerMaxWidth)
    }

    @Test
    fun `GIVEN a window wider than the player WHEN the sheet is expanded THEN the player stops growing at its max width`() {
        setContent(expanded = true, availableWidth = WIDE_WINDOW_WIDTH)

        composeTestRule.onNodeWithTag(LISTEN_SHEET_PLAYER_CARD_TEST_TAG).assertWidthIsEqualTo(PlayerMaxWidth)
    }

    @Test
    fun `GIVEN a window narrower than the player WHEN the sheet is expanded THEN the player fills the window`() {
        setContent(expanded = true, availableWidth = NARROW_WINDOW_WIDTH)

        composeTestRule.onNodeWithTag(LISTEN_SHEET_PLAYER_CARD_TEST_TAG).assertWidthIsEqualTo(NARROW_WINDOW_WIDTH)
    }

    @Test
    fun `GIVEN a window narrower than the player WHEN the sheet is compact THEN the player fills the window`() {
        setContent(expanded = false, availableWidth = NARROW_WINDOW_WIDTH)

        composeTestRule.onNodeWithTag(LISTEN_SHEET_PLAYER_CARD_TEST_TAG).assertWidthIsEqualTo(NARROW_WINDOW_WIDTH)
    }

    @Test
    fun `WHEN the thumb is dragged THEN no time is shown and no seek is asked for before it is let go`() {
        val actions = mutableListOf<ListenAction>()
        setContent(expanded = true, onAction = { actions.add(it) })

        dragThumbPastEnd(release = false)

        composeTestRule.onNodeWithTag(ELAPSED_TIME_TEST_TAG, useUnmergedTree = true).assertDoesNotExist()
        assertTrue("a seek was asked for before the thumb was let go", actions.isEmpty())
    }

    @Test
    fun `WHEN the thumb is let go THEN a seek is asked for`() {
        val actions = mutableListOf<ListenAction>()
        setContent(expanded = true, onAction = { actions.add(it) })

        dragThumbPastEnd(release = true)

        val seek = actions.single() as ListenAction.Playback.SeekRequested
        assertEquals(1f, seek.fraction, 0.01f)
    }

    @Test
    fun `GIVEN the voice menu is collapsed WHEN the voice button is clicked THEN its dropdown state changes from collapsed to expanded`() {
        setContent(expanded = true)
        val voiceButton = composeTestRule.onNodeWithContentDescription(voiceOptions)

        voiceButton
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Button))
            .assert(
                hasStateDescription(testContext.getString(R.string.mozac_feature_listentopage_dropdown_voice_collapsed))
            )
            .performClick()

        voiceButton.assert(hasStateDescription(expandedState))
    }

    @Test
    fun `WHEN the speed button is clicked THEN its dropdown expands and is read from the top`() {
        setContent(expanded = true)
        val speedButton = composeTestRule.onNodeWithContentDescription(playbackSpeed)

        speedButton.assert(SemanticsMatcher.expectValue(SemanticsProperties.Role, Role.Button)).performClick()

        speedButton.assert(hasStateDescription(expandedState))
        composeTestRule
            .onAllNodes(SemanticsMatcher.keyIsDefined(SemanticsProperties.TraversalIndex))
            .assertCountEquals(0)
    }

    @Test
    fun `WHEN the sheet is expanded THEN the seek bar is labelled and accessibility services can seek with it`() {
        val actions = mutableListOf<ListenAction>()
        setContent(
            expanded = true,
            onAction = { actions.add(it) },
            articleProgress = ArticleProgress(positionMs = 84_000, durationMs = 360_000),
        )
        val spokenState =
            testContext.getString(
                R.string.mozac_feature_listentopage_seek_state,
                formatSpokenDuration(84, Locale.getDefault()),
                formatSpokenDuration(360, Locale.getDefault()),
            )

        composeTestRule
            .onNodeWithTag(AUDIO_PROGRESS_BAR_TEST_TAG_EXPANDED)
            .assert(hasStateDescription(spokenState))
            .assert(SemanticsMatcher.keyIsDefined(SemanticsActions.SetProgress))
            .performSemanticsAction(SemanticsActions.SetProgress) { it(0.5f) }

        assertEquals(listOf(ListenAction.Playback.SeekRequested(0.5f)), actions)
    }

    @Test
    fun `GIVEN a bar catching up with a changed estimate WHEN shown THEN the spoken length is the one the bar is drawn against`() {
        setContent(
            expanded = true,
            articleProgress = ArticleProgress(positionMs = 60_000, durationMs = 600_000, barPosition = 0.2f),
        )
        val spokenState =
            testContext.getString(
                R.string.mozac_feature_listentopage_seek_state,
                formatSpokenDuration(60, Locale.getDefault()),
                formatSpokenDuration(300, Locale.getDefault()),
            )

        composeTestRule.onNodeWithTag(AUDIO_PROGRESS_BAR_TEST_TAG_EXPANDED).assert(hasStateDescription(spokenState))
    }

    @Test
    fun `WHEN the sheet is compact THEN the article heading announces expanding and changes to it`() {
        setContent(expanded = false)

        composeTestRule
            .onNodeWithText(TITLE)
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.LiveRegion, LiveRegionMode.Polite))
            .assert(
                SemanticsMatcher("has the expand player click label") {
                    it.config.getOrNull(SemanticsActions.OnClick)?.label ==
                        testContext.getString(R.string.mozac_feature_listentopage_expand_player)
                }
            )
    }

    private companion object {
        const val TITLE = "Match Preview: Wrexham AFC vs Sunderland AFC"
        const val TOTAL_DURATION_MS = 45000L
        const val LESS_THAN_ONE_SEC_ELAPSED_TIME_MS = 300L
        const val MORE_THAN_ONE_SEC_ELAPSED_TIME_MS = 1200L

        // The tag the elapsed time had, kept so that the tests notice if a time is shown again.
        const val ELAPSED_TIME_TEST_TAG = "PlayerExpandedElapsedTime"
        val NARROW_WINDOW_WIDTH = 400.dp
        val WIDE_WINDOW_WIDTH = 840.dp
    }
}
