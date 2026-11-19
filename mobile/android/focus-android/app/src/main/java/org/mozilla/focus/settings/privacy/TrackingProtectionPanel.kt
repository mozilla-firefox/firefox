/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.settings.privacy

import android.content.Context
import android.view.View
import android.widget.FrameLayout
import androidx.appcompat.content.res.AppCompatResources
import androidx.core.view.isVisible
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.setViewTreeLifecycleOwner
import androidx.savedstate.SavedStateRegistryOwner
import androidx.savedstate.setViewTreeSavedStateRegistryOwner
import com.google.android.material.R as materialR
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import java.text.NumberFormat
import java.util.Locale
import mozilla.components.browser.icons.IconRequest
import mozilla.components.support.ktx.android.view.putCompoundDrawablesRelativeWithIntrinsicBounds
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.focus.R
import org.mozilla.focus.databinding.DialogTrackingProtectionSheetBinding
import org.mozilla.focus.engine.EngineSharedPreferencesListener.TrackerChanged
import org.mozilla.focus.ext.components
import org.mozilla.focus.ext.installedDate
import org.mozilla.focus.ext.settings

/** Site state passed to [TrackingProtectionPanel]. */
data class SiteSecurityInfo(
    val tabUrl: String,
    val blockedTrackersCount: Int,
    val isTrackingProtectionOn: Boolean,
    val isConnectionSecure: Boolean,
)

/** Callbacks invoked by [TrackingProtectionPanel]. */
interface TrackingProtectionPanelInteractor {
    /**
     * Called when the user toggles tracking protection for the current site.
     *
     * @param enabled True if tracking protection should be enabled, false otherwise.
     */
    fun toggleTrackingProtection(enabled: Boolean)

    /**
     * Called when the user enables or disables a specific tracker category.
     *
     * @param tracker The tracker identifier, or null to apply to all trackers.
     * @param enabled True if the tracker category should be blocked, false otherwise.
     */
    fun updateTrackingProtectionPolicy(tracker: String?, enabled: Boolean)

    /** Called when the user taps the connection security info row. */
    fun showConnectionInfo()
}

/** A bottom sheet panel that displays tracking protection details and settings for the current site. */
class TrackingProtectionPanel(
    context: Context,
    private val lifecycleOwner: LifecycleOwner,
    private val siteInfo: SiteSecurityInfo,
    private val interactor: TrackingProtectionPanelInteractor,
) : BottomSheetDialog(context) {

    private var binding = DialogTrackingProtectionSheetBinding.inflate(layoutInflater, null, false)

    init {
        initWindow()
        setContentView(binding.root)
        expand()
        updateTitle()
        updateConnectionState()
        updateTrackingProtection()
        updateTrackersBlocked()
        updateTrackersState()
        setListeners()
    }

    private fun initWindow() {
        this.window?.decorView?.let {
            it.setViewTreeLifecycleOwner(lifecycleOwner)
            it.setViewTreeSavedStateRegistryOwner(lifecycleOwner as SavedStateRegistryOwner)
        }
    }

    private fun expand() {
        val bottomSheet = findViewById<View>(materialR.id.design_bottom_sheet) as FrameLayout
        BottomSheetBehavior.from(bottomSheet).state = BottomSheetBehavior.STATE_EXPANDED
    }

    private fun updateTitle() {
        binding.siteTitle.text = siteInfo.tabUrl.tryGetHostFromUrl()
        context.components.icons.loadIntoView(
            binding.siteFavicon,
            IconRequest(siteInfo.tabUrl, isPrivate = true),
        )
    }

    private fun updateConnectionState() {
        binding.securityInfo.text =
            context.getString(
                if (siteInfo.isConnectionSecure) R.string.secure_connection else R.string.insecure_connection
            )
        binding.securityInfo.putCompoundDrawablesRelativeWithIntrinsicBounds(
            start =
                AppCompatResources.getDrawable(
                    context,
                    if (siteInfo.isConnectionSecure) {
                        iconsR.drawable.mozac_ic_lock_24
                    } else {
                        iconsR.drawable.mozac_ic_warning_fill_24
                    },
                ),
            end = AppCompatResources.getDrawable(context, iconsR.drawable.mozac_ic_chevron_right_24),
            top = null,
            bottom = null,
        )
    }

    private fun updateTrackingProtection() {
        binding.enhancedTracking.apply {
            updateDescription(
                context.getString(
                    if (siteInfo.isTrackingProtectionOn) {
                        R.string.enhanced_tracking_protection_state_on
                    } else {
                        R.string.enhanced_tracking_protection_state_off
                    }
                )
            )
            updateIcon(
                icon =
                    if (siteInfo.isTrackingProtectionOn) {
                        iconsR.drawable.mozac_ic_shield_24
                    } else {
                        iconsR.drawable.mozac_ic_shield_slash_24
                    },
                iconContentDescription = context.getString(R.string.enhanced_tracking_protection),
            )
            binding.switchWidget.isChecked = siteInfo.isTrackingProtectionOn
        }
    }

    private fun updateTrackersBlocked() {
        binding.trackersCount.text =
            NumberFormat.getIntegerInstance(Locale.getDefault()).format(siteInfo.blockedTrackersCount)
        binding.trackersCountNote.text = context.getString(R.string.trackers_count_note, context.installedDate)
    }

    private fun updateTrackersState() {
        val settings = context.settings
        with(binding) {
            listOf(advertising, analytics, social, content, trackersAndScriptsHeading).forEach {
                it.isVisible = siteInfo.isTrackingProtectionOn
            }
            advertising.isChecked = settings.shouldBlockAdTrackers()
            analytics.isChecked = settings.shouldBlockAnalyticTrackers()
            social.isChecked = settings.shouldBlockSocialTrackers()
            content.isChecked = settings.shouldBlockOtherTrackers()
        }
    }

    private fun setListeners() {
        with(binding) {
            enhancedTracking.binding.switchWidget.setOnCheckedChangeListener { _, isChecked ->
                interactor.toggleTrackingProtection(isChecked)
                dismiss()
            }
            advertising.onClickListener {
                interactor.updateTrackingProtectionPolicy(TrackerChanged.ADVERTISING.tracker, advertising.isChecked)
            }
            analytics.onClickListener {
                interactor.updateTrackingProtectionPolicy(TrackerChanged.ANALYTICS.tracker, analytics.isChecked)
            }
            social.onClickListener {
                interactor.updateTrackingProtectionPolicy(TrackerChanged.SOCIAL.tracker, social.isChecked)
            }
            content.onClickListener {
                interactor.updateTrackingProtectionPolicy(TrackerChanged.CONTENT.tracker, content.isChecked)
            }
            securityInfo.setOnClickListener {
                interactor.showConnectionInfo()
            }
        }
    }
}
