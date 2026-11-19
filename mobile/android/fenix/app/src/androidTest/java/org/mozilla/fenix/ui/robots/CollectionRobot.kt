/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.assertAny
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeLeft
import androidx.compose.ui.test.swipeRight
import androidx.test.uiautomator.UiSelector
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectIsGone
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.click

class CollectionRobot(private val composeTestRule: ComposeTestRule) {

    @OptIn(ExperimentalTestApi::class)
    fun verifyTabSavedInCollection(title: String, visible: Boolean = true) {
        if (visible) {
            Log.i(TAG, "verifyTabSavedInCollection: Waiting for tab with title '$title' to appear")
            composeTestRule.waitUntil(waitingTime) {
                composeTestRule
                    .onAllNodesWithText(title)
                    .fetchSemanticsNodes(atLeastOneRootRequired = false)
                    .isNotEmpty()
            }
            Log.i(TAG, "verifyTabSavedInCollection: Tab with title '$title' is now present")

            Log.i(TAG, "verifyTabSavedInCollection: Waiting for compose rule to be idle")
            composeTestRule.waitForIdle()
            Log.i(TAG, "verifyTabSavedInCollection: Waited for compose rule to be idle")

            Log.i(TAG, "verifyTabSavedInCollection: Trying to verify that tab with title: $title is displayed")
            composeTestRule.onNodeWithText(title).assertIsDisplayed()
            Log.i(TAG, "verifyTabSavedInCollection: Verified that tab with title: $title is displayed")
        } else {
            Log.i(TAG, "verifyTabSavedInCollection: Waiting for tab with title '$title' to be removed")
            composeTestRule.waitUntil(waitingTimeLong) {
                composeTestRule.onAllNodesWithText(title).fetchSemanticsNodes(atLeastOneRootRequired = false).isEmpty()
            }
            Log.i(TAG, "verifyTabSavedInCollection: Tab with title '$title' is no longer present")

            Log.i(TAG, "verifyTabSavedInCollection: Waiting for compose rule to be idle")
            composeTestRule.waitForIdle()
            Log.i(TAG, "verifyTabSavedInCollection: Waited for compose rule to be idle")

            Log.i(TAG, "verifyTabSavedInCollection: Trying to verify that tab with title: $title is not displayed")
            composeTestRule.onNodeWithText(title).assertIsNotDisplayed()
            Log.i(TAG, "verifyTabSavedInCollection: Verified that tab with title: $title is not displayed")
        }
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyCollectionTabUrl(isDisplayed: Boolean, url: String) {
        if (isDisplayed) {
            Log.i(TAG, "verifyCollectionTabUrl: Waiting for $waitingTime until tab with url: $url exists")
            composeTestRule.waitUntilAtLeastOneExists(hasText(url))
            Log.i(TAG, "verifyCollectionTabUrl: Waited for $waitingTime until tab with url: $url exists")
            Log.i(TAG, "verifyCollectionTabUrl: Trying to verify that the node with text: $url has text: $url")
            composeTestRule.onAllNodesWithText(url).assertAny(hasText(url))
            Log.i(TAG, "verifyCollectionTabUrl: Verified that the node with text: $url has text: $url")
        } else {
            Log.i(TAG, "verifyCollectionTabUrl: Waiting for $waitingTime until tab with url: $url does not exist")
            composeTestRule.waitUntilDoesNotExist(hasText(url))
            Log.i(TAG, "verifyCollectionTabUrl: Waited for $waitingTime until tab with url: $url does not exist")
            Log.i(TAG, "verifyCollectionTabUrl: Trying to verify that the tab with url: $url is not displayed")
            composeTestRule.onNodeWithText(url).assertIsNotDisplayed()
            Log.i(TAG, "verifyCollectionTabUrl: Verified that the tab with url: $url is not displayed")
        }
    }

    fun verifyShareCollectionButtonIsVisible(visible: Boolean) {
        if (visible) {
            Log.i(TAG, "verifyShareCollectionButtonIsVisible: Trying to verify that the share button is displayed")
            composeTestRule.onNodeWithContentDescription("Share").assertIsDisplayed()
            Log.i(TAG, "verifyShareCollectionButtonIsVisible: Verified that the share button is displayed")
        } else {
            Log.i(TAG, "verifyShareCollectionButtonIsVisible: Trying to verify that the share button is not displayed")
            composeTestRule.onNodeWithContentDescription("Share").assertIsNotDisplayed()
            Log.i(TAG, "verifyShareCollectionButtonIsVisible: Verified that the share button is not displayed")
        }
    }

    fun verifyCollectionMenuIsVisible(visible: Boolean) {
        if (visible) {
            Log.i(TAG, "verifyCollectionMenuIsVisible: Trying to verify collection three dot button exists")
            collectionThreeDotButton(composeTestRule).assertExists()
            Log.i(TAG, "verifyCollectionMenuIsVisible: Verified collection three dot button exists")
            Log.i(TAG, "verifyCollectionMenuIsVisible: Trying to verify collection three dot button is displayed")
            collectionThreeDotButton(composeTestRule).assertIsDisplayed()
            Log.i(TAG, "verifyCollectionMenuIsVisible: Verified collection three dot button is displayed")
        } else {
            Log.i(TAG, "verifyCollectionMenuIsVisible: Trying to verify collection three dot button does not exist")
            collectionThreeDotButton(composeTestRule).assertDoesNotExist()
            Log.i(TAG, "verifyCollectionMenuIsVisible: Verified collection three dot button does not exist")
        }
    }

    fun clickCollectionThreeDotButton() {
        Log.i(TAG, "clickCollectionThreeDotButton: Trying to verify three dot button is displayed")
        collectionThreeDotButton(composeTestRule).assertIsDisplayed()
        Log.i(TAG, "clickCollectionThreeDotButton: Verified three dot button is displayed")
        Log.i(TAG, "clickCollectionThreeDotButton: Trying to click three dot button")
        collectionThreeDotButton(composeTestRule).performClick()
        Log.i(TAG, "clickCollectionThreeDotButton: Clicked three dot button")
    }

    fun selectOpenTabs() {
        Log.i(TAG, "selectOpenTabs: Trying to verify \"Open tabs\" menu option is displayed")
        composeTestRule.onNode(hasText("Open tabs")).assertIsDisplayed()
        Log.i(TAG, "selectOpenTabs: Verified \"Open tabs\" menu option is displayed")
        Log.i(TAG, "selectOpenTabs: Trying to click \"Open tabs\" menu option")
        composeTestRule.onNode(hasText("Open tabs")).performClick()
        Log.i(TAG, "selectOpenTabs: Clicked \"Open tabs\" menu option")
    }

    @OptIn(ExperimentalTestApi::class)
    fun selectDeleteCollection() {
        Log.i(TAG, "selectDeleteCollection: Trying to verify \"Delete collection\" menu option is displayed")
        composeTestRule.onNode(hasText("Delete collection")).assertIsDisplayed()
        Log.i(TAG, "selectDeleteCollection: Verified \"Delete collection\" menu option is displayed")
        Log.i(TAG, "selectDeleteCollection: Trying to click \"Delete collection\" menu option")
        composeTestRule.onNode(hasText("Delete collection")).performClick()
        Log.i(TAG, "selectDeleteCollection: Clicked \"Delete collection\" menu option")
        Log.i(TAG, "selectDeleteCollection: Waiting for the \"Delete collection\" menu option to not exist")
        composeTestRule.waitUntilDoesNotExist(hasText("Delete collection"))
        Log.i(TAG, "selectDeleteCollection: Waited for the \"Delete collection\" menu option to not exist")
    }

    fun verifyCollectionItemRemoveButtonIsVisible(title: String, visible: Boolean) {
        if (visible) {
            assertUIObjectExists(removeTabFromCollectionButton(title), exists = true)
        } else {
            assertUIObjectIsGone(removeTabFromCollectionButton(title))
        }
    }

    fun removeTabFromCollection(title: String) {
        Log.i(TAG, "removeTabFromCollection: Trying to click remove button for tab: $title")
        removeTabFromCollectionButton(title).click()
        Log.i(TAG, "removeTabFromCollection: Clicked remove button for tab: $title")
    }

    fun swipeTabLeft(title: String) {
        Log.i(TAG, "swipeTabLeft: Trying to swipe left the collections tab with title: $title")
        composeTestRule.onAllNodesWithText(title)[0].performTouchInput { swipeLeft() }
        Log.i(TAG, "swipeTabLeft: Swiped left the collections tab with title: $title")
        composeTestRule.waitForIdle()
        mDevice.waitForIdle()
    }

    fun swipeTabRight(title: String) {
        Log.i(TAG, "swipeTabRight: Trying to swipe right the collections tab with title: $title")
        composeTestRule.onAllNodesWithText(title)[0].performTouchInput { swipeRight() }
        Log.i(TAG, "swipeTabRight: Swiped right the collections tab with title: $title")
        composeTestRule.waitForIdle()
        mDevice.waitForIdle()
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        fun collapseCollection(
            title: String,
            interact: HomeScreenRobot.() -> Unit,
        ): HomeScreenRobot.Transition {
            Log.i(TAG, "collapseCollection: Trying to click the collection with title: $title")
            composeTestRule.onNodeWithText(title).performClick()
            Log.i(TAG, "collapseCollection: Clicked the collection with title: $title")
            Log.i(TAG, "collapseCollection: Waiting for compose test rule to be idle")
            composeTestRule.waitForIdle()
            Log.i(TAG, "collapseCollection: Waiting for compose test rule to be idle")

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }

        @OptIn(ExperimentalTestApi::class)
        fun clickShareCollectionButton(interact: ShareOverlayRobot.() -> Unit): ShareOverlayRobot.Transition {
            Log.i(TAG, "clickShareCollectionButton: Waiting for $waitingTime ms for share collection button to exist")
            composeTestRule.waitUntilExactlyOneExists(hasContentDescription("Share"), waitingTime)
            Log.i(TAG, "clickShareCollectionButton: Waited for $waitingTime ms for share collection button to exist")
            Log.i(TAG, "clickShareCollectionButton: Trying to click share collection button")
            composeTestRule.onNodeWithContentDescription("Share").performClick()
            Log.i(TAG, "clickShareCollectionButton: Clicked share collection button")

            ShareOverlayRobot().interact()
            return ShareOverlayRobot.Transition()
        }
    }
}

fun collectionRobot(
    composeTestRule: ComposeTestRule,
    interact: CollectionRobot.() -> Unit,
): CollectionRobot.Transition {
    CollectionRobot(composeTestRule).interact()
    return CollectionRobot.Transition(composeTestRule)
}

private fun collectionThreeDotButton(rule: ComposeTestRule) = rule.onNode(hasContentDescription("Collection menu"))

private fun removeTabFromCollectionButton(title: String) =
    mDevice.findObject(UiSelector().text(title)).getFromParent(UiSelector().description("Remove tab from collection"))
