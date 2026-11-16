/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.PreviewLightDark
import java.util.Locale
import mozilla.components.compose.base.menu.DropdownMenu
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.menu.MenuItem.CheckableItem
import mozilla.components.compose.base.menu.MenuItem.TextItem
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.listentopage.R
import mozilla.components.feature.listentopage.Voice
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.ui.icons.R as iconsR

/** UI to allow the user to manage their selected voice for narration. */
@Composable
fun VoiceSelection(
    expanded: Boolean,
    availableVoices: List<Voice>,
    selectedVoice: Voice?,
    onVoiceClick: (Voice) -> Unit,
    onDismissRequest: () -> Unit,
) {
    val context = LocalContext.current
    val voiceItems =
        if (availableVoices.isEmpty()) {
            listOf(
                TextItem(
                    text = Text.Resource(R.string.mozac_feature_listentopage_error_voice_options_not_available),
                    enabled = false,
                    onClick = {},
                )
            )
        } else {
            availableVoices.toMenuItems(selectedVoice, onVoiceClick)
        }
    val title = stringResource(R.string.mozac_feature_listentopage_voice_options)
    val menuItems =
        voiceItems +
            MenuItem.Divider +
            MenuItem.IconItem(
                text = Text.Resource(R.string.mozac_feature_listentopage_voice_options_tts_settings),
                drawableRes = iconsR.drawable.mozac_ic_external_link_24,
                onClick = {
                    onDismissRequest()
                    context.openTextToSpeechSettings()
                },
            )
    DropdownMenu(
        expanded = expanded,
        menuItems = menuItems,
        modifier = Modifier.semantics { paneTitle = title },
        focusCheckedItemFirst = false,
        onDismissRequest = onDismissRequest,
    )
}

private fun Context.openTextToSpeechSettings() {
    val intent = Intent("com.android.settings.TTS_SETTINGS")
    try {
        startActivity(intent)
    } catch (e: ActivityNotFoundException) {
        Logger.error("Unable to open text-to-speech settings", e)
    }
}

private fun List<Voice>.toMenuItems(
    selectedVoice: Voice?,
    onClick: (Voice) -> Unit,
): List<MenuItem> = map { voice ->
    CheckableItem(
        text = Text.String(voice.displayName),
        isChecked = voice == selectedVoice,
        onClick = { onClick(voice) },
    )
}

private val previewVoices =
    listOf(Locale.UK, Locale.US, Locale.CANADA, Locale.forLanguageTag("en-AU")).map {
        Voice(id = "en-${it.country.lowercase()}-local", locale = it)
    }

// Dropdown menus are currently only previewable in interactive mode - give it a shot if you don't see anything
@PreviewLightDark
@Composable
private fun PreviewVoiceSelection() {
    var selectedVoice by remember { mutableStateOf(previewVoices.last()) }

    AcornTheme {
        Surface(modifier = Modifier.fillMaxSize()) {
            VoiceSelection(
                expanded = true,
                availableVoices = previewVoices,
                selectedVoice = selectedVoice,
                onVoiceClick = { selectedVoice = it },
                onDismissRequest = {},
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun PreviewNoOfflineVoicesAvailableError() {
    AcornTheme {
        Surface(modifier = Modifier.fillMaxSize()) {
            VoiceSelection(
                expanded = true,
                availableVoices = emptyList(),
                selectedVoice = null,
                onVoiceClick = { _ -> },
                onDismissRequest = {},
            )
        }
    }
}
