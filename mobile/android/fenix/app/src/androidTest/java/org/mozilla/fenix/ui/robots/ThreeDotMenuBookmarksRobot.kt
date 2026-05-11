package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource

class ThreeDotMenuBookmarksRobot {
    class Transition(private val composeTestRule: ComposeTestRule) {

        fun clickOpenInNewTab(interact: TabDrawerRobot.() -> Unit): TabDrawerRobot.Transition {
            Log.i(TAG, "clickOpenInNewTab: Trying to click the \"Open in new tab\" button")
            composeTestRule.openInNewTabButton().performClick()
            Log.i(TAG, "clickOpenInNewTab: Clicked the \"Open in new tab\" button")

            TabDrawerRobot(composeTestRule).interact()
            return TabDrawerRobot.Transition(composeTestRule)
        }

        fun clickOpenInPrivateTab(interact: TabDrawerRobot.() -> Unit): TabDrawerRobot.Transition {
            Log.i(TAG, "clickOpenInPrivateTab: Trying to click the \"Open in private tab\" button")
            composeTestRule.openInPrivateTabButton().performClick()
            Log.i(TAG, "clickOpenInPrivateTab: Clicked the \"Open in private tab\" button")

            TabDrawerRobot(composeTestRule).interact()
            return TabDrawerRobot.Transition(composeTestRule)
        }

        fun clickDelete(interact: BookmarksRobot.() -> Unit): BookmarksRobot.Transition {
            Log.i(TAG, "clickDelete: Trying to click the \"Delete\" button")
            composeTestRule.deleteButton().performClick()
            Log.i(TAG, "clickDelete: Clicked the \"Delete\" button")

            BookmarksRobot(composeTestRule).interact()
            return BookmarksRobot.Transition(composeTestRule)
        }

        fun clickShare(interact: ShareOverlayRobot.() -> Unit): ShareOverlayRobot.Transition {
            Log.i(TAG, "clickShare: Trying to click the \"Share\" button")
            composeTestRule.shareButton().performClick()
            Log.i(TAG, "clickShare: Clicked the \"Share\" button")

            ShareOverlayRobot().interact()
            return ShareOverlayRobot.Transition()
        }

        fun clickEdit(interact: BookmarksRobot.() -> Unit): BookmarksRobot.Transition {
            Log.i(TAG, "clickEdit: Trying to click the \"Edit\" button")
            composeTestRule.editButton().performClick()
            Log.i(TAG, "clickEdit: Clicked the \"Edit\" button")

            BookmarksRobot(composeTestRule).interact()
            return BookmarksRobot.Transition(composeTestRule)
        }

        fun clickMove(interact: BookmarksRobot.() -> Unit): BookmarksRobot.Transition {
            Log.i(TAG, "clickMove: Trying to click the \"Move\" button")
            composeTestRule.moveButton().performClick()
            Log.i(TAG, "clickMove: Clicked the \"Move\" button")
            composeTestRule.waitForIdle()

            BookmarksRobot(composeTestRule).interact()
            return BookmarksRobot.Transition(composeTestRule)
        }

        fun clickOpenAllInTabs(composeTestRule: ComposeTestRule, interact: TabDrawerRobot.() -> Unit): TabDrawerRobot.Transition {
            Log.i(TAG, "clickOpenAllInTabs: Trying to click the \"Open all in new tabs\" button")
            composeTestRule.openAllInTabsButton().performClick()
            Log.i(TAG, "clickOpenAllInTabs: Clicked the \"Open all in new tabs\" button")

            TabDrawerRobot(composeTestRule).interact()
            return TabDrawerRobot.Transition(composeTestRule)
        }

        fun clickOpenAllInPrivateTabs(composeTestRule: ComposeTestRule, interact: TabDrawerRobot.() -> Unit): TabDrawerRobot.Transition {
            Log.i(TAG, "clickOpenAllInPrivateTabs: Trying to click the \"Open all in private tabs\" button")
            composeTestRule.openAllInPrivateTabsButton().performClick()
            Log.i(TAG, "clickOpenAllInPrivateTabs: Clicked the \"Open all in private tabs\" button")

            TabDrawerRobot(composeTestRule).interact()
            return TabDrawerRobot.Transition(composeTestRule)
        }
    }
}
private fun ComposeTestRule.openInNewTabButton() = onNodeWithText(getStringResource(R.string.bookmark_menu_open_in_new_tab_button))

private fun ComposeTestRule.openInPrivateTabButton() = onNodeWithText(getStringResource(R.string.bookmark_menu_open_in_private_tab_button))

private fun ComposeTestRule.editButton() = onNodeWithText(getStringResource(R.string.bookmark_menu_edit_button))

private fun ComposeTestRule.shareButton() = onNodeWithText(getStringResource(R.string.bookmark_menu_share_button))

private fun ComposeTestRule.deleteButton() = onNodeWithText(getStringResource(R.string.bookmark_menu_delete_button))

private fun ComposeTestRule.moveButton() = onNodeWithText(getStringResource(R.string.bookmark_menu_move_button))

private fun ComposeTestRule.openAllInTabsButton() = onNodeWithText("Open all in new tabs")

private fun ComposeTestRule.openAllInPrivateTabsButton() = onNodeWithText("Open all in private tabs")
