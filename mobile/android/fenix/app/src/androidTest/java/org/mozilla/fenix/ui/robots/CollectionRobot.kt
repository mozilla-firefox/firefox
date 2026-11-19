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
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.pressImeActionButton
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.uiautomator.By
import androidx.test.uiautomator.UiSelector
import androidx.test.uiautomator.Until
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.MatcherHelper.assertItemTextEquals
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectIsGone
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.click
import org.mozilla.fenix.helpers.ext.waitNotNull

class CollectionRobot(private val composeTestRule: ComposeTestRule) {

    fun verifyTabsSelectedCounterText(numOfTabs: Int) {
        Log.i(
            TAG,
            "verifyTabsSelectedCounterText: Waiting for $waitingTime ms for \"Select tabs to save\" prompt to be gone",
        )
        itemWithText("Select tabs to save").waitUntilGone(waitingTime)
        Log.i(
            TAG,
            "verifyTabsSelectedCounterText: Waited for $waitingTime ms for \"Select tabs to save\" prompt to be gone",
        )

        val tabsCounter = mDevice.findObject(UiSelector().resourceId("$packageName:id/bottom_bar_text"))
        Log.i(TAG, "verifyTabsSelectedCounterText: Trying to assert that number of tabs selected is: $numOfTabs")
        when (numOfTabs) {
            1 -> assertItemTextEquals(tabsCounter, expectedText = "$numOfTabs tab selected")
            2 -> assertItemTextEquals(tabsCounter, expectedText = "$numOfTabs tabs selected")
        }
        Log.i(TAG, "verifyTabsSelectedCounterText: Asserted number of tabs selected is: $numOfTabs")
    }

    fun saveTabsSelectedForCollection() {
        Log.i(TAG, "saveTabsSelectedForCollection: Trying to click \"Save\" button")
        itemWithResId("$packageName:id/save_button").click()
        Log.i(TAG, "saveTabsSelectedForCollection: Clicked \"Save\" button")
    }

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

    fun selectRenameCollection() {
        Log.i(TAG, "selectRenameCollection: Trying to verify \"Rename collection\" menu option is displayed")
        composeTestRule.onNode(hasText("Rename collection")).assertIsDisplayed()
        Log.i(TAG, "selectRenameCollection: Verified \"Rename collection\" menu option is displayed")
        Log.i(TAG, "selectRenameCollection: Trying to click \"Rename collection\" menu option")
        composeTestRule.onNode(hasText("Rename collection")).performClick()
        Log.i(TAG, "selectRenameCollection: Clicked \"Rename collection\" menu option")
        Log.i(TAG, "selectRenameCollection: Waiting for $waitingTime ms for collection name text field to exist")
        mainMenuEditCollectionNameField().waitForExists(waitingTime)
        Log.i(TAG, "selectRenameCollection: Waited for $waitingTime ms for collection name text field to exist")
    }

    @OptIn(ExperimentalTestApi::class)
    fun selectAddTabToCollection() {
        Log.i(TAG, "selectAddTabToCollection: Trying to verify \"Add tab\" menu option is displayed")
        composeTestRule.onNode(hasText("Add tab")).assertIsDisplayed()
        Log.i(TAG, "selectAddTabToCollection: Verified \"Add tab\" menu option is displayed")
        Log.i(TAG, "selectAddTabToCollection: Trying to click \"Add tab\" menu option")
        composeTestRule.onNode(hasText("Add tab")).performClick()
        Log.i(TAG, "selectAddTabToCollection: Clicked \"Add tab\" menu option")
        Log.i(TAG, "selectAddTabToCollection: Waiting for the \"Add tab\" menu option to not exist")
        composeTestRule.waitUntilDoesNotExist(hasText("Add tab"))
        Log.i(TAG, "selectAddTabToCollection: Waited for the \"Add tab\" menu option to not exist")
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

        // names a collection saved from the 3dot menu
        fun typeCollectionNameAndSave(
            name: String,
            interact: BrowserRobot.() -> Unit,
        ): BrowserRobot.Transition {
            Log.i(TAG, "typeCollectionNameAndSave: Waiting for $waitingTime ms for collection name text field to exist")
            mainMenuEditCollectionNameField().waitForExists(waitingTime)
            Log.i(TAG, "typeCollectionNameAndSave: Waited for $waitingTime ms for collection name text field to exist")
            Log.i(TAG, "typeCollectionNameAndSave: Trying to set collection name text field to: $name")
            mainMenuEditCollectionNameField().setText(name)
            Log.i(TAG, "typeCollectionNameAndSave: Collection name text field set to: $name")
            Log.i(TAG, "typeCollectionNameAndSave: Trying to press done action button")
            onView(withId(R.id.name_collection_edittext)).perform(pressImeActionButton())
            Log.i(TAG, "typeCollectionNameAndSave: Pressed done action button")

            // wait for the collection creation wrapper to be dismissed
            mDevice.waitNotNull(Until.gone(By.res("$packageName:id/createCollectionWrapper")))

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
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

// collection name text field, when saving from the main menu option
private fun mainMenuEditCollectionNameField() = itemWithResId("$packageName:id/name_collection_edittext")
