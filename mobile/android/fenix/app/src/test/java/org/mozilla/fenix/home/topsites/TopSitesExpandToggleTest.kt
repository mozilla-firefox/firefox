/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.requiredWidth
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.math.abs
import mozilla.components.compose.base.theme.Theme
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.home.fake.FakeHomepagePreview
import org.mozilla.fenix.theme.FirefoxTheme

/** A phone-width screen, which leaves room for [TOP_SITES_MIN_COLUMNS] shortcuts a row. */
private val PHONE_WIDTH = 360.dp

/** A tablet-width screen, which leaves room for [TOP_SITES_MAX_COLUMNS] shortcuts a row. */
private val WIDE_WIDTH = 800.dp

private val PHONE_COLLAPSED = collapsedTopSitesCount(TOP_SITES_MIN_COLUMNS)
private val WIDE_COLLAPSED = collapsedTopSitesCount(TOP_SITES_MAX_COLUMNS)

/** More shortcuts than two rows hold on a phone, but fewer than two rows hold on a wide screen. */
private val MORE_THAN_PHONE_FITS = PHONE_COLLAPSED + 3

/** Fewer shortcuts than two rows hold on a phone, leaving room for the "Add shortcut" tile. */
private val FEWER_THAN_PHONE_FITS = PHONE_COLLAPSED - 3

@RunWith(AndroidJUnit4::class)
class TopSitesExpandToggleTest {

    @get:Rule val composeTestRule = createComposeRule()

    private fun setTopSitesContent(
        count: Int,
        isExpanded: Boolean,
        width: Dp = PHONE_WIDTH,
        onExpandToggleClick: () -> Unit = {},
    ) {
        val topSites =
            FakeHomepagePreview.topSites(
                providedCount = 0,
                pinnedCount = 0,
                defaultCount = count,
            )
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                Surface {
                    Box(modifier = Modifier.requiredWidth(width)) {
                        TopSites(
                            topSites = topSites,
                            onTopSiteClick = {},
                            onTopSiteLongClick = {},
                            onTopSiteImpression = { _, _ -> },
                            onOpenInPrivateTabClicked = {},
                            onEditTopSiteClicked = {},
                            onRemoveTopSiteClicked = {},
                            onSettingsClicked = {},
                            onSponsorPrivacyClicked = {},
                            onTopSitesItemBound = {},
                            onAddShortcutClicked = {},
                            onExpandToggleClick = onExpandToggleClick,
                            isExpanded = isExpanded,
                        )
                    }
                }
            }
        }
    }

    @Test
    fun `GIVEN more shortcuts than fit WHEN collapsed THEN only two rows are rendered`() {
        setTopSitesContent(count = MORE_THAN_PHONE_FITS, isExpanded = false)

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.TOP_SITE_ITEM_ROOT).assertCountEquals(PHONE_COLLAPSED)
    }

    @Test
    fun `GIVEN more shortcuts than fit WHEN expanded THEN every shortcut is rendered`() {
        setTopSitesContent(count = MORE_THAN_PHONE_FITS, isExpanded = true)

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.TOP_SITE_ITEM_ROOT).assertCountEquals(MORE_THAN_PHONE_FITS)
    }

    @Test
    fun `GIVEN a wide screen WHEN collapsed THEN two rows hold more shortcuts than on a phone`() {
        setTopSitesContent(count = WIDE_COLLAPSED, isExpanded = false, width = WIDE_WIDTH)

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.TOP_SITE_ITEM_ROOT).assertCountEquals(WIDE_COLLAPSED)
        assertTrue(WIDE_COLLAPSED > PHONE_COLLAPSED)
    }

    @Test
    fun `GIVEN a wide screen with room for every shortcut WHEN collapsed THEN there is nothing to expand`() {
        setTopSitesContent(
            count = MORE_THAN_PHONE_FITS,
            isExpanded = false,
            width = WIDE_WIDTH,
        )

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.TOP_SITE_ITEM_ROOT).assertCountEquals(MORE_THAN_PHONE_FITS)
        composeTestRule.onAllNodesWithTag(TopSitesTestTag.EXPAND_TOGGLE).assertCountEquals(0)
    }

    @Test
    fun `GIVEN the section is collapsed WHEN rendered THEN the expand label is shown`() {
        setTopSitesContent(count = MORE_THAN_PHONE_FITS, isExpanded = false)

        composeTestRule.onNodeWithTag(TopSitesTestTag.EXPAND_TOGGLE).assertIsDisplayed()
        composeTestRule.onNodeWithText("Show all").assertIsDisplayed()
    }

    @Test
    fun `GIVEN the section is expanded WHEN rendered THEN the collapse label is shown`() {
        setTopSitesContent(count = MORE_THAN_PHONE_FITS, isExpanded = true)

        composeTestRule.onNodeWithText("Show less").assertIsDisplayed()
    }

    @Test
    fun `WHEN the toggle is clicked THEN the callback is invoked`() {
        var clicked = false
        setTopSitesContent(
            count = MORE_THAN_PHONE_FITS,
            isExpanded = false,
            onExpandToggleClick = { clicked = true },
        )

        composeTestRule.onNodeWithTag(TopSitesTestTag.EXPAND_TOGGLE).performClick()

        assertTrue(clicked)
    }

    @Test
    fun `GIVEN titles that fit on one line WHEN rendered THEN the toggle sits 8dp below the last row`() {
        setTopSitesContent(count = MORE_THAN_PHONE_FITS, isExpanded = false)

        val lastRowBottom =
            composeTestRule.onAllNodesWithTag(TopSitesTestTag.TOP_SITE_ITEM_ROOT).fetchSemanticsNodes().maxOf {
                it.boundsInRoot.bottom
            }
        val toggleTop =
            composeTestRule.onNodeWithTag(TopSitesTestTag.EXPAND_TOGGLE).fetchSemanticsNode().boundsInRoot.top
        val expectedGap = with(composeTestRule.density) { 8.dp.toPx() }
        val tolerance = with(composeTestRule.density) { 1.dp.toPx() }
        val actualGap = toggleTop - lastRowBottom

        assertTrue(
            "Expected the toggle to sit ${expectedGap}px below the last row, but it sat ${actualGap}px below it",
            abs(actualGap - expectedGap) <= tolerance,
        )
    }

    private fun setStatefulTopSitesContent(
        count: Int,
        width: Dp = PHONE_WIDTH,
    ) {
        val state =
            TopSiteState(
                topSites =
                    FakeHomepagePreview.topSites(
                        providedCount = 0,
                        pinnedCount = 0,
                        defaultCount = count,
                    ),
                colors =
                    TopSiteColors(
                        titleTextColor = Color.Black,
                        sponsoredTextColor = Color.Black,
                        faviconCardBackgroundColor = Color.White,
                    ),
            )
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                Surface {
                    Box(modifier = Modifier.requiredWidth(width)) {
                        TopSites(
                            state = state,
                            interactor = FakeHomepagePreview.topSitesInteractor,
                            onTopSitesItemBound = {},
                            onAddShortcutClicked = {},
                        )
                    }
                }
            }
        }
    }

    @Test
    fun `GIVEN the section starts collapsed WHEN the toggle is clicked THEN every shortcut is shown`() {
        setStatefulTopSitesContent(count = MORE_THAN_PHONE_FITS)

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.TOP_SITE_ITEM_ROOT).assertCountEquals(PHONE_COLLAPSED)

        composeTestRule.onNodeWithTag(TopSitesTestTag.EXPAND_TOGGLE).performClick()

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.TOP_SITE_ITEM_ROOT).assertCountEquals(MORE_THAN_PHONE_FITS)
    }

    @Test
    fun `GIVEN fewer shortcuts than fit THEN the add shortcut tile is shown`() {
        setStatefulTopSitesContent(count = FEWER_THAN_PHONE_FITS)

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.TOP_SITE_ITEM_ROOT).assertCountEquals(FEWER_THAN_PHONE_FITS)
        composeTestRule.onAllNodesWithTag(TopSitesTestTag.ADD_SHORTCUT_ROOT).assertCountEquals(1)
    }

    @Test
    fun `GIVEN exactly two rows of shortcuts WHEN the toggle is clicked THEN the add shortcut tile is revealed`() {
        setStatefulTopSitesContent(count = PHONE_COLLAPSED)

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.ADD_SHORTCUT_ROOT).assertCountEquals(0)

        composeTestRule.onNodeWithTag(TopSitesTestTag.EXPAND_TOGGLE).performClick()

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.TOP_SITE_ITEM_ROOT).assertCountEquals(PHONE_COLLAPSED)
        composeTestRule.onAllNodesWithTag(TopSitesTestTag.ADD_SHORTCUT_ROOT).assertCountEquals(1)
    }

    @Test
    fun `GIVEN exactly two rows and expanded WHEN the toggle is clicked THEN the tile is hidden again`() {
        setStatefulTopSitesContent(count = PHONE_COLLAPSED)

        composeTestRule.onNodeWithTag(TopSitesTestTag.EXPAND_TOGGLE).performClick()
        composeTestRule.onNodeWithTag(TopSitesTestTag.EXPAND_TOGGLE).performClick()

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.ADD_SHORTCUT_ROOT).assertCountEquals(0)
    }

    @Test
    fun `GIVEN a wide screen holding every shortcut THEN the toggle still reveals the tile`() {
        setStatefulTopSitesContent(count = WIDE_COLLAPSED, width = WIDE_WIDTH)

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.ADD_SHORTCUT_ROOT).assertCountEquals(0)

        composeTestRule.onNodeWithTag(TopSitesTestTag.EXPAND_TOGGLE).performClick()

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.ADD_SHORTCUT_ROOT).assertCountEquals(1)
    }

    @Test
    fun `GIVEN more shortcuts than fit WHEN collapsed THEN the add shortcut tile is hidden`() {
        setStatefulTopSitesContent(count = MORE_THAN_PHONE_FITS)

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.ADD_SHORTCUT_ROOT).assertCountEquals(0)
    }

    @Test
    fun `GIVEN more shortcuts than fit WHEN expanded THEN the add shortcut tile is shown`() {
        setStatefulTopSitesContent(count = MORE_THAN_PHONE_FITS)

        composeTestRule.onNodeWithTag(TopSitesTestTag.EXPAND_TOGGLE).performClick()

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.ADD_SHORTCUT_ROOT).assertCountEquals(1)
    }

    @Test
    fun `GIVEN the section is expanded WHEN the toggle is clicked again THEN it collapses`() {
        setStatefulTopSitesContent(count = MORE_THAN_PHONE_FITS)

        composeTestRule.onNodeWithTag(TopSitesTestTag.EXPAND_TOGGLE).performClick()
        composeTestRule.onNodeWithTag(TopSitesTestTag.EXPAND_TOGGLE).performClick()

        composeTestRule.onAllNodesWithTag(TopSitesTestTag.TOP_SITE_ITEM_ROOT).assertCountEquals(PHONE_COLLAPSED)
    }
}
