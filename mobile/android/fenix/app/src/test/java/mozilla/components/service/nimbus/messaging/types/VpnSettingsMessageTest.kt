/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.nimbus.messaging.types

import java.util.Locale
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.nimbus.messaging.MessageData
import mozilla.components.service.nimbus.messaging.SettingsVpnMessageConfig
import mozilla.components.service.nimbus.messaging.StyleData
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.experiments.nimbus.NullVariables
import org.mozilla.experiments.nimbus.StringHolder
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class VpnSettingsMessageTest {
    @Before
    fun setup() {
        NullVariables.instance.setContext(testContext)
    }

    @Test
    fun `WHEN the message is valid THEN createFrom formats the title and preserves the other fields`() {
        val message =
            createMessage(
                MessageData(
                    title = StringHolder(null, "%s GB of VPN data."),
                    text = StringHolder(null, "Protect your connection"),
                    buttonLabel = StringHolder(null, "Try Firefox VPN"),
                    settingsVpnConfig = SettingsVpnMessageConfig(titleArgs = listOf("fxa_data_limit_gb")),
                )
            )

        val vpnMessage =
            requireNotNull(
                VpnSettingsMessage.createFrom(
                    message,
                    locale = Locale.US,
                    titleArgsReplacements = mapOf("fxa_data_limit_gb" to "50"),
                )
            )

        assertEquals("50 GB of VPN data.", vpnMessage.title)
        assertEquals("Protect your connection", vpnMessage.text)
        assertEquals("Try Firefox VPN", vpnMessage.actionText)
        assertSame(message, vpnMessage.original)
    }

    @Test
    fun `WHEN the message has no title THEN createFrom returns null`() {
        val message = createMessage(MessageData(text = StringHolder(null, "VPN message body")))

        assertNull(VpnSettingsMessage.createFrom(message))
    }

    @Test
    fun `WHEN the title is empty or whitespace only THEN createFrom returns null`() {
        for (title in listOf("", "  ")) {
            val message = createMessage(MessageData(title = StringHolder(null, title)))

            assertNull(VpnSettingsMessage.createFrom(message))
        }
    }

    @Test
    fun `WHEN title formatting produces a blank title THEN createFrom returns null`() {
        val message =
            createMessage(
                MessageData(
                    title = StringHolder(null, "%s"),
                    settingsVpnConfig = SettingsVpnMessageConfig(titleArgs = listOf("account")),
                )
            )

        assertNull(VpnSettingsMessage.createFrom(message, titleArgsReplacements = mapOf("account" to "")))
    }

    @Test
    fun `WHEN a replacement key does not match the title argument THEN createFrom returns null`() {
        val message =
            createMessage(
                MessageData(
                    title = StringHolder(null, "%d GB of VPN data"),
                    settingsVpnConfig = SettingsVpnMessageConfig(titleArgs = listOf("fxa_data_limit_gb")),
                )
            )

        assertNull(VpnSettingsMessage.createFrom(message, titleArgsReplacements = mapOf("data_limit_gb" to 50)))
    }

    @Test
    fun `WHEN a replacement value has the wrong type THEN createFrom returns null`() {
        val message =
            createMessage(
                MessageData(
                    title = StringHolder(null, "%d GB of VPN data"),
                    settingsVpnConfig = SettingsVpnMessageConfig(titleArgs = listOf("fxa_data_limit_gb")),
                )
            )

        assertNull(VpnSettingsMessage.createFrom(message, titleArgsReplacements = mapOf("fxa_data_limit_gb" to "50")))
    }

    @Test
    fun `WHEN the button label is blank THEN action text is null`() {
        val message =
            createMessage(
                MessageData(
                    title = StringHolder(null, "Try Firefox VPN"),
                    buttonLabel = StringHolder(null, "  "),
                )
            )

        val vpnMessage = requireNotNull(VpnSettingsMessage.createFrom(message))

        assertNull(vpnMessage.actionText)
    }

    @Test
    fun `WHEN the message body is VPN_MESSAGE_EMPTY_BODY THEN text is null`() {
        val message =
            createMessage(
                MessageData(
                    title = StringHolder(null, "Try Firefox VPN"),
                    text = StringHolder(null, "VPN_MESSAGE_EMPTY_BODY"),
                )
            )

        val vpnMessage = requireNotNull(VpnSettingsMessage.createFrom(message))

        assertNull(vpnMessage.text)
    }

    @Test
    fun `WHEN the message body is whitespace only THEN text is null`() {
        val message =
            createMessage(
                MessageData(
                    title = StringHolder(null, "Try Firefox VPN"),
                    text = StringHolder(null, "  "),
                )
            )

        val vpnMessage = requireNotNull(VpnSettingsMessage.createFrom(message))

        assertNull(vpnMessage.text)
    }

    private fun createMessage(data: MessageData) =
        Message(
            id = "vpn-message",
            data = data,
            action = "",
            style = StyleData(),
            triggerIfAll = emptyList(),
            metadata = Message.Metadata(id = "vpn-message"),
        )
}
