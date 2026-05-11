/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.content.Context
import android.util.AttributeSet
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import androidx.preference.Preference
import androidx.preference.PreferenceViewHolder
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme

/**
 * A [Preference] for the built-in VPN (IP Protection) settings entry.
 */
class IPProtectionPreference @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : Preference(context, attrs) {

    /**
     * Enables a `beta` badge next to the entry.
     */
    var showBetaBadge: Boolean = false

    init {
        layoutResource = R.layout.preference_ip_protection
    }

    override fun onBindViewHolder(holder: PreferenceViewHolder) {
        super.onBindViewHolder(holder)
        holder.itemView.findViewById<ComposeView>(R.id.compose_view)?.apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            setContent {
                FirefoxTheme {
                    IPProtectionPreferenceRow(
                        title = context.getString(R.string.preferences_ip_protection_title_2),
                        showBetaBadge = showBetaBadge,
                    )
                }
            }
        }
    }
}

@Composable
internal fun IPProtectionPreferenceRow(
    title: String,
    showBetaBadge: Boolean,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = title,
            style = FirefoxTheme.typography.subtitle1,
            color = MaterialTheme.colorScheme.onSurface,
        )

        Spacer(modifier = Modifier.width(8.dp))

        if (showBetaBadge) {
            BetaBadge()
        }
    }
}

@Composable
private fun BetaBadge() {
    Surface(
        shape = RoundedCornerShape(8.dp),
        color = FirefoxTheme.colors.actionInformation,
    ) {
        Text(
            text = stringResource(R.string.preferences_ip_protection_beta_badge_label),
            modifier = Modifier.padding(horizontal = 8.dp),
            style = FirefoxTheme.typography.subtitle2,
            color = FirefoxTheme.colors.textOnColorPrimary,
        )
    }
}

@PreviewLightDark
@Composable
private fun IPProtectionPreferenceRowPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        Surface(color = MaterialTheme.colorScheme.surface) {
            IPProtectionPreferenceRow(
                title = stringResource(id = R.string.preferences_ip_protection_title_2),
                showBetaBadge = true,
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun IPProtectionPreferenceRowNoBadgePreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        Surface(color = MaterialTheme.colorScheme.surface) {
            IPProtectionPreferenceRow(
                title = stringResource(id = R.string.preferences_ip_protection_title_2),
                showBetaBadge = false,
            )
        }
    }
}
