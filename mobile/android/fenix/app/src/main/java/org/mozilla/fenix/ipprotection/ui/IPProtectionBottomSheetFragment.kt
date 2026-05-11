/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.ui

import android.app.Dialog
import android.content.DialogInterface
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.compose.content
import androidx.navigation.fragment.navArgs
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptAction
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptPreferencesMiddleware
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptState
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptStore
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptTelemetryMiddleware
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.theme.FirefoxTheme
import com.google.android.material.R as materialR

/**
 * [BottomSheetDialogFragment] wrapper for the compose [IPProtectionBottomSheet].
 */
class IPProtectionBottomSheetFragment : BottomSheetDialogFragment() {

    private val args by navArgs<IPProtectionBottomSheetFragmentArgs>()

    private var isAlreadyShowing: Boolean = false

    private val ipProtectionPromptStore by fragmentStore(IPProtectionPromptState) {
        IPProtectionPromptStore(
            initialState = it,
            middleware = listOf(
                IPProtectionPromptPreferencesMiddleware(
                    repository = requireComponents.ipProtectionPromptRepository,
                ),
                IPProtectionPromptTelemetryMiddleware(),
            ),
        )
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog =
        super.onCreateDialog(savedInstanceState).apply {
            setOnShowListener {
                val bottomSheet = findViewById<View?>(materialR.id.design_bottom_sheet)
                bottomSheet?.setBackgroundResource(android.R.color.transparent)

                if (!isAlreadyShowing) {
                    ipProtectionPromptStore.dispatch(IPProtectionPromptAction.OnImpression(args.surface))
                    isAlreadyShowing = true
                }
            }
        }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        isAlreadyShowing = savedInstanceState?.getBoolean(IS_ALREADY_SHOW_KEY) ?: false
        ipProtectionPromptStore.dispatch(IPProtectionPromptAction.OnPromptCreated)
        return content {
            FirefoxTheme {
                IPProtectionBottomSheet(
                    maxGib = FxNimbus.features.ipProtection.value().dataLimitGigabyte,
                    onDismiss = { dismiss() },
                    onDismissRequest = {
                        ipProtectionPromptStore.dispatch(
                            IPProtectionPromptAction.OnPromptManuallyDismissed(args.surface),
                        )
                        dismiss()
                    },
                    onGetStartedClicked = {
                        ipProtectionPromptStore.dispatch(
                            IPProtectionPromptAction.OnGetStartedClicked(args.surface),
                        )
                        dismiss()
                    },
                    onLearnMoreClicked = {
                        ipProtectionPromptStore.dispatch(
                            IPProtectionPromptAction.OnBrowseWithExtraProtectionClicked(args.surface),
                        )
                        SupportUtils.launchSandboxCustomTab(
                            requireActivity(),
                            SupportUtils.getSumoURLForTopic(
                                requireActivity(),
                                SupportUtils.SumoTopic.VPN,
                                useMobilePage = false,
                            ),
                        )
                        dismiss()
                    },
                )
            }
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putBoolean(IS_ALREADY_SHOW_KEY, isAlreadyShowing)
    }

    override fun onDismiss(dialog: DialogInterface) {
        super.onDismiss(dialog)
        ipProtectionPromptStore.dispatch(IPProtectionPromptAction.OnPromptDismissed)
    }

    companion object {
        private const val IS_ALREADY_SHOW_KEY = "is_already_showing"
    }
}
