/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.address.ui.edit

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.concept.storage.Address
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.settings.address.store.AddressState
import org.mozilla.fenix.settings.address.store.AddressStore
import org.mozilla.fenix.settings.address.store.BackTapped
import org.mozilla.fenix.settings.address.store.DeleteTapped
import org.mozilla.fenix.settings.address.store.SaveTapped
import org.mozilla.fenix.settings.address.store.isEditing
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Topbar for editing an address.
 *
 * @param store the [AddressStore] used to power the topbar.
 */
@Composable
internal fun EditAddressTopBar(store: AddressStore) {
    TopAppBar(
        title = {
            Text(
                text = stringResource(store.state.titleId),
                style = FirefoxTheme.typography.headline5,
            )
        },
        navigationIcon = {
            IconButton(
                onClick = { store.dispatch(BackTapped) },
                contentDescription = stringResource(R.string.bookmark_navigate_back_button_content_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                    contentDescription = null,
                )
            }
        },
        actions = {
            if (store.state.isEditing) {
                IconButton(
                    onClick = { store.dispatch(DeleteTapped) },
                    contentDescription = stringResource(R.string.address_menu_delete_address),
                    modifier = Modifier.testTag(EditAddressTestTag.TOPBAR_DELETE_BUTTON),
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_delete_24),
                        contentDescription = null,
                    )
                }
            }

            IconButton(
                onClick = { store.dispatch(SaveTapped) },
                contentDescription = stringResource(R.string.address_menu_save_address),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_checkmark_24),
                    contentDescription = null,
                )
            }
        },
        windowInsets =
            WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
    )
}

@get:StringRes
private val AddressState.titleId: Int
    get() =
        if (isEditing) {
            R.string.addresses_edit_address
        } else {
            R.string.addresses_add_address
        }

@Preview
@Composable
private fun AddTopBarPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    val store = AddressStore(AddressState.initial(), listOf())

    FirefoxTheme(theme) {
        EditAddressTopBar(store)
    }
}

@Preview
@Composable
private fun EditTopBarPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    val address = Address("BEEF", "Work", "Mozilla", "", "", "", "", "", "", "", "")
    val store = AddressStore(AddressState.initial(address = address), listOf())

    FirefoxTheme(theme) {
        EditAddressTopBar(store)
    }
}
