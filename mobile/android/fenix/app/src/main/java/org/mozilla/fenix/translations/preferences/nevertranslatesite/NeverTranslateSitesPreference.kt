/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.translations.preferences.nevertranslatesite

import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.InfoCard
import mozilla.components.compose.base.InfoType
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.list.TextListItem
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import mozilla.components.ui.icons.R as iconsR

/**
 * Never Translate Site preference screen.
 *
 * @param neverTranslateSitesListPreferences List of site urls to display.
 * @param hasNeverTranslateSitesError If a never translate sites error occurs.
 * @param onItemClick Invoked when the user clicks on the a item from the list.
 */
@Composable
fun NeverTranslateSitesPreference(
    neverTranslateSitesListPreferences: List<String>? = null,
    hasNeverTranslateSitesError: Boolean,
    onItemClick: (String) -> Unit,
) {
    Surface {
        LazyColumn {
            item {
                TextListItem(
                    label = stringResource(R.string.never_translate_site_header_preference),
                    modifier = Modifier
                        .padding(
                            start = 56.dp,
                        )
                        .semantics { heading() }
                        .defaultMinSize(minHeight = 76.dp)
                        .wrapContentHeight(),
                    maxLabelLines = Int.MAX_VALUE,
                )
            }

            if (hasNeverTranslateSitesError) {
                item {
                    NeverTranslateSitesErrorWarning()
                }
            }

            neverTranslateSitesListPreferences?.let {
                items(neverTranslateSitesListPreferences) { item: String ->
                    val itemContentDescription = stringResource(
                        id = R.string.never_translate_site_item_list_content_description_preference,
                        item,
                    )
                    TextListItem(
                        label = item,
                        modifier = Modifier
                            .padding(
                                start = 56.dp,
                            )
                            .clearAndSetSemantics {
                                role = Role.Button
                                contentDescription = itemContentDescription
                            }
                            .defaultMinSize(minHeight = 56.dp)
                            .wrapContentHeight(),
                        onClick = { onItemClick(item) },
                        iconPainter = painterResource(iconsR.drawable.mozac_ic_delete_24),
                        onIconClick = { onItemClick(item) },
                    )
                }
            }
        }
    }
}

@Composable
private fun NeverTranslateSitesErrorWarning() {
    val modifier = Modifier
        .fillMaxWidth()
        .padding(start = 72.dp, end = 16.dp, bottom = 16.dp, top = 16.dp)
        .defaultMinSize(minHeight = 56.dp)
        .wrapContentHeight()

    InfoCard(
        description = stringResource(id = R.string.never_translate_site_error_warning_text),
        type = InfoType.Warning,
        verticalRowAlignment = Alignment.CenterVertically,
        modifier = modifier,
    )
}

@Composable
internal fun getNeverTranslateSitesList(): List<String> {
    return mutableListOf<String>().apply {
        add(
            "mozilla.org",
        )
    }
}

@Preview
@Composable
private fun NeverTranslateSitePreferencePreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        NeverTranslateSitesPreference(
            neverTranslateSitesListPreferences = getNeverTranslateSitesList(),
            hasNeverTranslateSitesError = false,
        ) {}
    }
}

@Preview
@Composable
private fun NeverTranslateSitePreferenceErrorPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        NeverTranslateSitesPreference(
            neverTranslateSitesListPreferences = getNeverTranslateSitesList(),
            hasNeverTranslateSitesError = true,
        ) {}
    }
}
