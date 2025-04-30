/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.microsurvey.ui

import android.app.Dialog
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.navArgs
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import kotlinx.coroutines.launch
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.messaging.MicrosurveyMessageController
import org.mozilla.fenix.microsurvey.ui.ext.toMicrosurveyUIData
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * A bottom sheet fragment for displaying a microsurvey.
 */
class MicrosurveyBottomSheetFragment : BottomSheetDialogFragment() {

    private val args by navArgs<MicrosurveyBottomSheetFragmentArgs>()

    private val microsurveyMessageController by lazy {
        MicrosurveyMessageController(requireComponents.appStore, (activity as HomeActivity))
    }

    private val closeBottomSheet = { dismiss() }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog =
        super.onCreateDialog(savedInstanceState).apply {
            setOnShowListener {
                val bottomSheet = findViewById<View?>(R.id.design_bottom_sheet)
                bottomSheet?.let {
                    it.setBackgroundResource(android.R.color.transparent)
                    val behavior = BottomSheetBehavior.from(it)
                    behavior.setPeekHeightToHalfScreenHeight()
                    behavior.state = BottomSheetBehavior.STATE_HALF_EXPANDED
                }
            }
        }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View = ComposeView(requireContext()).apply {
        val messaging = context.components.nimbus.messaging
        val microsurveyId = args.microsurveyId

        lifecycleScope.launch {
            val microsurveyUIData = messaging.getMessage(microsurveyId)?.toMicrosurveyUIData()
            microsurveyUIData?.let {
                setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
                microsurveyMessageController.onMicrosurveyShown(it.id)
                setContent {
                    FirefoxTheme {
                        val activity = requireActivity() as HomeActivity

                        MicrosurveyBottomSheet(
                            question = it.question,
                            icon = it.icon,
                            answers = it.answers,
                            onPrivacyPolicyLinkClick = {
                                closeBottomSheet()
                                microsurveyMessageController.onPrivacyPolicyLinkClicked(
                                    it.id,
                                    it.utmContent,
                                )
                            },
                            onCloseButtonClicked = {
                                microsurveyMessageController.onMicrosurveyDismissed(it.id)
                                context.settings().shouldShowMicrosurveyPrompt = false
                                activity.isMicrosurveyPromptDismissed.value = true
                                closeBottomSheet()
                            },
                            onSubmitButtonClicked = { answer ->
                                context.settings().shouldShowMicrosurveyPrompt = false
                                activity.isMicrosurveyPromptDismissed.value = true
                                microsurveyMessageController.onSurveyCompleted(it.id, answer)
                            },
                        )
                    }
                }
            }
        }
    }

    private fun BottomSheetBehavior<View>.setPeekHeightToHalfScreenHeight() {
        peekHeight = resources.displayMetrics.heightPixels / 2
    }
}
