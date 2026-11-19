/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.settings.privacy

import android.content.Context
import android.view.View
import android.widget.FrameLayout
import androidx.appcompat.content.res.AppCompatResources
import com.google.android.material.R as materialR
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import java.security.cert.X509Certificate
import mozilla.components.browser.icons.IconRequest
import mozilla.components.concept.engine.EngineSession
import mozilla.components.support.ktx.android.view.putCompoundDrawablesRelativeWithIntrinsicBounds
import mozilla.components.support.utils.CertificateUtils
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.focus.R
import org.mozilla.focus.databinding.ConnectionDetailsBinding
import org.mozilla.focus.ext.components

/** A bottom sheet panel that displays connection security details for the current site. */
class ConnectionDetailsPanel(
    context: Context,
    private var engineSession: EngineSession?,
    private val tabTitle: String,
    private val tabUrl: String,
    private val isConnectionSecure: Boolean,
    private val goBack: () -> Unit,
) : BottomSheetDialog(context) {

    private var binding: ConnectionDetailsBinding = ConnectionDetailsBinding.inflate(layoutInflater, null, false)

    init {
        setContentView(binding.root)
        expandBottomSheet()

        updateSiteInfo()
        updateConnectionState()
        setListeners()

        engineSession?.qwacStatus { cert -> updateQWACInformation(cert) }
        engineSession = null
    }

    private fun expandBottomSheet() {
        val bottomSheet = findViewById<View>(materialR.id.design_bottom_sheet) as FrameLayout
        BottomSheetBehavior.from(bottomSheet).state = BottomSheetBehavior.STATE_EXPANDED
    }

    private fun updateSiteInfo() {
        binding.siteTitle.text = tabTitle
        binding.siteFullUrl.text = tabUrl

        context.components.icons.loadIntoView(
            binding.siteFavicon,
            IconRequest(tabUrl, isPrivate = true),
        )
    }

    private fun updateConnectionState() {
        binding.securityInfo.text =
            if (isConnectionSecure) {
                context.getString(R.string.secure_connection)
            } else {
                context.getString(R.string.insecure_connection)
            }

        val securityIcon =
            if (isConnectionSecure) {
                AppCompatResources.getDrawable(context, iconsR.drawable.mozac_ic_lock_24)
            } else {
                AppCompatResources.getDrawable(context, iconsR.drawable.mozac_ic_warning_fill_24)
            }

        binding.securityInfo.putCompoundDrawablesRelativeWithIntrinsicBounds(
            start = securityIcon,
            end = null,
            top = null,
            bottom = null,
        )

        binding.qwacIdentity.setVisibility(View.GONE)
        binding.qwacText.setVisibility(View.GONE)
    }

    private fun updateQWACInformation(cert: X509Certificate?) {
        cert?.let {
            binding.qwacIdentity.text =
                context.getString(
                    R.string.qualified_identity,
                    CertificateUtils.subjectOrganization(it) ?: "",
                )
            binding.qwacIdentity.setVisibility(View.VISIBLE)
            binding.qwacText.text = context.getString(R.string.qualified_text)
            binding.qwacText.setVisibility(View.VISIBLE)
        }
    }

    private fun setListeners() {
        binding.detailsBack.setOnClickListener {
            goBack.invoke()
            dismiss()
        }
    }
}
