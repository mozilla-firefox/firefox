/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ipprotection

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.PromoCard
import mozilla.components.compose.base.PromoCardColors
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.compose.base.theme.informationContainer
import mozilla.components.service.nimbus.messaging.Message
import mozilla.components.service.nimbus.messaging.MessageData
import mozilla.components.service.nimbus.messaging.StyleData
import mozilla.components.service.nimbus.messaging.types.VpnSettingsMessage
import org.mozilla.experiments.nimbus.NullVariables
import org.mozilla.experiments.nimbus.StringHolder
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * UI component for showing a message on the VPN screen.
 *
 * @param message The [VpnSettingsMessage] object
 * @param modifier Modifier for the component
 * @param onAction Callback invoked when the message's action is clicked
 * @param onDismiss Callback invoked when the message is dismissed
 */
@Composable
internal fun IPProtectionMessageCard(
    message: VpnSettingsMessage,
    modifier: Modifier = Modifier,
    onAction: (message: Message) -> Unit = {},
    onDismiss: ((message: Message) -> Unit)? = null,
) {
    PromoCard(
        modifier = modifier,
        title = message.title,
        footer =
            message.actionText?.let { label ->
                label to
                    LinkTextState(
                        text = label,
                        url = "",
                        onClick = { onAction(message.original) },
                    )
            },
        description = message.text,
        colors = PromoCardColors.promoCardColors(backgroundColor = MaterialTheme.colorScheme.informationContainer),
        closeButtonContentDescription =
            if (onDismiss != null) stringResource(R.string.ip_protection_settings_message_dismiss) else null,
        onDismiss = onDismiss?.let { dismiss -> { dismiss(message.original) } },
    )
}

@Composable
@Preview
private fun PreviewIPProtectionMessageCard(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme = theme) {
        // the message object is from Nimbus, and requires the context to be set
        NullVariables.instance.setContext(LocalContext.current)
        IPProtectionMessageCard(
            modifier = Modifier.fillMaxWidth(),
            onAction = {},
            message =
                VpnSettingsMessage.createFrom(
                    Message(
                        id = "123",
                        data =
                            MessageData(
                                title = StringHolder(null, "Boost your VPN data limit to 50 GB"),
                                buttonLabel = StringHolder(null, "Sign in"),
                            ),
                        action = "action",
                        style = StyleData(),
                        triggerIfAll = emptyList(),
                        metadata = Message.Metadata(id = ""),
                    )
                )!!,
        ) {}
    }
}
