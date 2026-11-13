/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.experiments

import android.app.Dialog
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.res.stringResource
import androidx.fragment.compose.content
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import com.google.android.material.R as materialR
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import kotlinx.coroutines.launch
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.experiments.view.UninstallSurveyBottomSheet
import org.mozilla.fenix.ext.openToBrowser
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.intent.maybeTriggerDeviceUninstallPrompt
import org.mozilla.fenix.messaging.MicrosurveyMessageController
import org.mozilla.fenix.microsurvey.ui.MicrosurveyBottomSheetFragmentArgs
import org.mozilla.fenix.microsurvey.ui.ext.MicrosurveyUIData
import org.mozilla.fenix.microsurvey.ui.ext.toMicrosurveyUIData
import org.mozilla.fenix.theme.FirefoxTheme

/** A bottom sheet fragment for displaying a microsurvey. */
class UninstallSurveyBottomSheetFragment : BottomSheetDialogFragment() {

    private val args by navArgs<MicrosurveyBottomSheetFragmentArgs>()

    private val microsurveyMessageController by lazy {
        MicrosurveyMessageController(
            appStore = requireComponents.appStore,
            openUrlInBrowser = { url ->
                findNavController().openToBrowser()
                requireComponents.useCases.fenixBrowserUseCases.loadUrlOrSearch(
                    searchTermOrURL = url,
                    newTab = true,
                )
            },
        )
    }

    private var microsurveyUIData by mutableStateOf<MicrosurveyUIData?>(null)

    private val closeBottomSheet: () -> Unit = { findNavController().popBackStack() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setStyle(STYLE_NORMAL, materialR.style.Theme_Design_BottomSheetDialog)
        val messaging = requireComponents.nimbus.messaging
        val microsurveyId = args.microsurveyId

        lifecycleScope.launch {
            val message = messaging.getMessage(microsurveyId)

            if (message == null) {
                // If the server returns no data (e.g., experiment expired), bypass the survey and trigger the system
                // uninstall prompt directly so the user's action isn't interrupted.
                requireActivity().maybeTriggerDeviceUninstallPrompt()
                closeBottomSheet()
                return@launch
            }
            microsurveyUIData = message.toMicrosurveyUIData()
        }
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val dialog = super.onCreateDialog(savedInstanceState) as BottomSheetDialog

        dialog.behavior.apply {
            state = BottomSheetBehavior.STATE_EXPANDED
            skipCollapsed = true
            isFitToContents = false
        }

        dialog.setOnShowListener {
            val bottomSheet = dialog.findViewById<View?>(materialR.id.design_bottom_sheet)
            bottomSheet?.let {
                it.setBackgroundResource(android.R.color.transparent)
                it.layoutParams.height = ViewGroup.LayoutParams.MATCH_PARENT
            }
        }
        return dialog
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ) = content {
        FirefoxTheme {
            val activity = requireActivity() as HomeActivity

            microsurveyUIData?.let {
                LaunchedEffect(it.id) {
                    microsurveyMessageController.onMicrosurveyShown(it.id)
                }
                UninstallSurveyBottomSheet(
                    question = it.question,
                    icon = it.icon,
                    answers = it.answers,
                    isSubmitAlwaysEnabled = true,
                    buttonLabel = stringResource(id = R.string.uninstall_survey_button_label),
                    onPrivacyPolicyLinkClick = {
                        closeBottomSheet()
                        microsurveyMessageController.onPrivacyPolicyLinkClicked(
                            it.id,
                            it.utmContent,
                        )
                    },
                    onCloseButtonClicked = {
                        microsurveyMessageController.onMicrosurveyDismissed(it.id)
                        closeBottomSheet()
                    },
                    onSubmitButtonClicked = { answer ->
                        microsurveyMessageController.onSurveyCompleted(it.id, answer)
                        activity.maybeTriggerDeviceUninstallPrompt()
                        closeBottomSheet()
                    },
                )
            }
        }
    }
}
