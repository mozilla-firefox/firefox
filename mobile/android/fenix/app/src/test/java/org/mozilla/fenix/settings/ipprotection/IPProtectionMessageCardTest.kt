/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ipprotection

import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertHasClickAction
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasClickAction
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.BeforeTest
import mozilla.components.compose.base.theme.Theme
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.nimbus.messaging.MessageData
import mozilla.components.service.nimbus.messaging.SettingsVpnMessageConfig
import mozilla.components.service.nimbus.messaging.StyleData
import mozilla.components.service.nimbus.messaging.types.VpnSettingsMessage
import mozilla.components.support.test.robolectric.testContext
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.experiments.nimbus.NullVariables
import org.mozilla.experiments.nimbus.StringHolder
import org.mozilla.fenix.theme.FirefoxTheme

private const val MESSAGE_CARD_TAG = "vpn-message-card"

@RunWith(AndroidJUnit4::class)
class IPProtectionMessageCardTest {
    @get:Rule val composeTestRule = createComposeRule()

    @BeforeTest
    fun setUp() {
        NullVariables.instance.setContext(testContext)
    }

    @Test
    fun `GIVEN a message with title WHEN the component is rendered THEN the title is rendered`() {
        val message =
            createVpnMessage(
                title = "My title",
                text = "My text",
            )

        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                IPProtectionMessageCard(
                    modifier = Modifier.testTag(MESSAGE_CARD_TAG),
                    message = message,
                )
            }
        }

        composeTestRule.onNodeWithText("My title").assertExists()
    }

    @Test
    fun `GIVEN a message with body WHEN rendering the card THEN the body is displayed`() {
        val body = "Your VPN data resets every month."
        val message =
            createVpnMessage(
                title = "Boost your VPN data limit to 1 GB",
                text = body,
            )

        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                IPProtectionMessageCard(message = message)
            }
        }

        composeTestRule.onNodeWithText(body).assertIsDisplayed()
    }

    @Test
    fun `GIVEN no message body WHEN rendering the card THEN the empty body marker is hidden`() {
        val message = createVpnMessage(title = "More data for you", text = "VPN_MESSAGE_EMPTY_BODY")

        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                IPProtectionMessageCard(message, modifier = Modifier.testTag(MESSAGE_CARD_TAG))
            }
        }

        composeTestRule.onNodeWithTag(MESSAGE_CARD_TAG).assertIsDisplayed()
        composeTestRule.onNodeWithText("VPN_MESSAGE_EMPTY_BODY").assertDoesNotExist()
    }

    @Test
    fun `GIVEN a message action WHEN rendering the card THEN the action is displayed and clickable`() {
        val message =
            createVpnMessage(
                title = "More data -> 50 GB",
                text = "Your message body",
                actionLabel = "Hey click me!",
            )
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                IPProtectionMessageCard(message = message)
            }
        }

        composeTestRule
            .onNodeWithContentDescription("Hey click me!", substring = true)
            .assertIsDisplayed()
            .assertHasClickAction()
    }

    @Test
    fun `GIVEN no action label WHEN rendering the card THEN there is no clickable action`() {
        val message =
            createVpnMessage(
                title = "More data for you",
                text = "Your message body",
                actionLabel = null,
            )

        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                IPProtectionMessageCard(
                    message = message,
                    modifier = Modifier.testTag(MESSAGE_CARD_TAG),
                )
            }
        }

        composeTestRule.onNodeWithTag(MESSAGE_CARD_TAG).assertIsDisplayed()
        composeTestRule.onAllNodes(hasClickAction(), useUnmergedTree = true).assertCountEquals(0)
    }

    private fun createVpnMessage(
        title: String,
        text: String?,
        actionLabel: String? = null,
    ): VpnSettingsMessage {
        val data =
            MessageData(
                title = StringHolder(null, title),
                text = StringHolder(null, text ?: "VPN_MESSAGE_EMPTY_BODY"),
                buttonLabel = actionLabel?.let { StringHolder(null, actionLabel) },
                settingsVpnConfig = SettingsVpnMessageConfig(titleArgs = emptyList()),
            )
        return VpnSettingsMessage.createFrom(
            Message(
                id = "id",
                data = data,
                action = "",
                style = StyleData(),
                triggerIfAll = emptyList(),
                metadata = Message.Metadata(id = "id"),
            )
        )!!
    }
}
