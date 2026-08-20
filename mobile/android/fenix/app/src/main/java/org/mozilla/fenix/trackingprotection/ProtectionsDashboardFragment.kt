/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.trackingprotection

import android.app.Dialog
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.compose.runtime.getValue
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat.Type.systemBars
import androidx.fragment.compose.content
import androidx.navigation.fragment.navArgs
import com.google.android.material.R as materialR
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.runIfFragmentIsAttached

/** [BottomSheetDialog] showing the global protections dashboard. */
class ProtectionsDashboardFragment : BottomSheetDialogFragment() {
    private val args by navArgs<ProtectionsDashboardFragmentArgs>()
    private val trackersBlockedFeature = ViewBoundFeatureWrapper<TrackersBlockedFeature>()

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog =
        (super.onCreateDialog(savedInstanceState) as BottomSheetDialog).apply {
            setOnShowListener {
                runIfFragmentIsAttached {
                    val bottomSheet = findViewById<FrameLayout>(materialR.id.design_bottom_sheet)
                    bottomSheet?.let {
                        ViewCompat.setOnApplyWindowInsetsListener(it) { view, insets ->
                            val systemBarInsets = insets.getInsets(systemBars())
                            view.setPadding(0, systemBarInsets.top, 0, systemBarInsets.bottom)
                            insets
                        }
                    }
                    bottomSheet?.setBackgroundResource(R.drawable.bottom_sheet_with_top_rounded_corners)

                    behavior.peekHeight = context.resources.displayMetrics.heightPixels
                    behavior.state = BottomSheetBehavior.STATE_EXPANDED
                }
            }
        }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ) = content {
        val appStore = requireComponents.appStore
        val blockedTrackersState by appStore.observeAsComposableState { state ->
            state.blockedTrackersState
        }

        ProtectionsDashboardContent(
            totalTrackersBlocked = blockedTrackersState.trackersBlockedCount,
            trackersBlockedThisWeek = blockedTrackersState.trackersBlockedThisWeek,
            earliestTrackingDate = blockedTrackersState.earliestTrackingDate,
        ) {
            dismiss()
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        trackersBlockedFeature.set(
            feature =
                TrackersBlockedFeature(
                    browserStore = requireComponents.core.store,
                    appStore = requireComponents.appStore,
                    currentSessionId = args.customTabSessionId,
                    trackingProtectionUseCases = requireComponents.useCases.trackingProtectionUseCases,
                ),
            owner = viewLifecycleOwner,
            view = view,
        )
    }
}
