/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.settings.privacy

import android.content.Context
import android.view.View
import android.widget.FrameLayout
import android.widget.TextView
import androidx.activity.addCallback
import androidx.core.content.ContextCompat
import androidx.core.graphics.ColorUtils
import androidx.core.view.ViewCompat
import androidx.core.view.isVisible
import com.google.android.material.R as materialR
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import java.security.cert.X509Certificate
import kotlin.math.roundToInt
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl
import mozilla.components.support.utils.R as supportUtilsR
import mozilla.components.support.utils.issuerDisplayName
import mozilla.components.support.utils.toCertificateDetails
import mozilla.components.support.utils.toIssuedTo
import org.mozilla.focus.R
import org.mozilla.focus.databinding.CertificateDetailsBinding

private const val DARK_BACKGROUND_LUMINANCE = 0.5

// The design shows the EU trust mark 58dp wide, which with its aspect ratio is 85dp high. It must also be displayed at
// least 64 pixels wide and 85 pixels high, as required by Commission Implementing Regulation (EU) 2015/806, so on low
// density screens it is widened to 64 pixels, which keeps it more than 85 pixels high.
private const val EU_TRUST_MARK_WIDTH_DP = 58
private const val EU_TRUST_MARK_MIN_WIDTH_PX = 64

/**
 * A bottom sheet panel that shows who the current website's certificate was issued to and who verified it, and, when
 * the website presents a Qualified Website Authentication Certificate (QWAC), the QWAC statement and the EU trust mark.
 *
 * @param context The [Context] used to create the panel.
 * @param qwacStatusRequest The [QwacStatusRequest] for the current website.
 * @param tabUrl The URL of the current website.
 * @param certificate The TLS certificate of the current website, or null if there is none.
 * @param goBack Invoked when the user goes back to the tracking protection panel.
 */
class CertificateDetailsPanel(
    context: Context,
    qwacStatusRequest: QwacStatusRequest,
    tabUrl: String,
    private val certificate: X509Certificate?,
    private val goBack: () -> Unit,
) : BottomSheetDialog(context) {

    private val binding = CertificateDetailsBinding.inflate(layoutInflater, null, false)

    init {
        setContentView(binding.root)
        expandBottomSheet()

        binding.siteHost.text = tabUrl.tryGetHostFromUrl()
        ViewCompat.setAccessibilityHeading(binding.siteHost, true)
        ViewCompat.setAccessibilityHeading(binding.secureConnectionTitle, true)
        ViewCompat.setScreenReaderFocusable(binding.issuedTo, true)
        ViewCompat.setScreenReaderFocusable(binding.verifiedBy, true)
        showCertificate(qwac = null)
        setUpTrustMark()

        binding.detailsBack.setOnClickListener { navigateBack() }
        onBackPressedDispatcher.addCallback(this) { navigateBack() }

        qwacStatusRequest.observe { qwac -> showCertificate(qwac) }
    }

    private fun expandBottomSheet() {
        val bottomSheet = findViewById<View>(materialR.id.design_bottom_sheet) as FrameLayout
        BottomSheetBehavior.from(bottomSheet).state = BottomSheetBehavior.STATE_EXPANDED
    }

    private fun navigateBack() {
        goBack()
        dismiss()
    }

    private fun showCertificate(qwac: X509Certificate?) {
        // The QWAC can differ from the TLS certificate, such as with a 2-QWAC, and it is the one the EU trust mark
        // refers to.
        val certificateDetails = (qwac ?: certificate)?.toCertificateDetails()

        val issuedTo = certificateDetails.toIssuedTo(context.resources.configuration.locales[0])
        binding.issuedTo.isVisible = issuedTo != null
        binding.issuedToOrganization.showText(issuedTo?.organization)
        binding.issuedToLocality.showText(issuedTo?.locality)
        binding.issuedToCountry.showText(issuedTo?.country)

        val issuerName = certificateDetails.issuerDisplayName()
        binding.verifiedBy.isVisible = issuerName != null
        binding.verifiedByIssuer.showText(issuerName)

        binding.qwacStatement.isVisible = qwac != null
        binding.euTrustMark.isVisible = qwac != null
        binding.dragHandle.updateSpacing(isQwac = qwac != null)
    }

    private fun setUpTrustMark() {
        // The colors of the EU trust mark must not be altered, so the inverted asset is used on dark backgrounds
        // instead of tinting it.
        val cardColor = ContextCompat.getColor(context, R.color.certificate_details_card_background)
        val isDarkBackground = ColorUtils.calculateLuminance(cardColor) < DARK_BACKGROUND_LUMINANCE
        binding.euTrustMark.setImageResource(
            if (isDarkBackground) {
                supportUtilsR.drawable.eu_trust_mark_inverted
            } else {
                supportUtilsR.drawable.eu_trust_mark
            }
        )

        val width = (EU_TRUST_MARK_WIDTH_DP * context.resources.displayMetrics.density).roundToInt()
        binding.euTrustMark.layoutParams.width = maxOf(width, EU_TRUST_MARK_MIN_WIDTH_PX)
    }

    private fun TextView.showText(value: String?) {
        text = value
        isVisible = value != null
    }
}
