/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose.customize

import android.app.Dialog
import android.os.Bundle
import androidx.appcompat.app.AppCompatDialogFragment
import androidx.navigation.fragment.findNavController
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import mozilla.components.compose.menu.store.CustomizableMenuAction.ResetToDefaultConfirmed
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.lib.state.helpers.StoreProvider.Companion.storeProvider
import org.mozilla.fenix.R

/** A [AppCompatDialogFragment] confirming whether to reset the menu customization to default. */
class ResetToDefaultDialogFragment : AppCompatDialogFragment() {

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        return MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.menu_customize_reset_dialog_title)
            .setMessage(R.string.menu_customize_reset_dialog_message)
            .setPositiveButton(R.string.menu_customize_reset_dialog_reset) { _, _ ->
                getMenuStore().dispatch(ResetToDefaultConfirmed)
            }
            .setNegativeButton(R.string.menu_customize_reset_dialog_cancel) { _, _ -> }
            .create()
    }

    private fun getMenuStore(): CustomizableMenuStore {
        val backStackEntry = findNavController().getBackStackEntry(R.id.customize_menu_graph)
        return checkNotNull(backStackEntry.storeProvider.get<CustomizableMenuStore>()) {
            "CustomizableMenuStore should have been initialized by CustomizeMenuFragment"
        }
    }
}
