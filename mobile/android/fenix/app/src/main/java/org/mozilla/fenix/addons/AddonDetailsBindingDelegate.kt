/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.addons

import android.net.Uri
import android.text.SpannableStringBuilder
import android.text.method.LinkMovementMethod
import android.text.style.ClickableSpan
import android.text.style.URLSpan
import android.view.View
import android.widget.TextView
import androidx.annotation.VisibleForTesting
import androidx.core.net.toUri
import androidx.core.text.HtmlCompat
import androidx.core.text.getSpans
import androidx.core.view.isVisible
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.ui.translateDescription
import mozilla.components.feature.addons.ui.updatedAtDate
import mozilla.components.support.ktx.android.content.getColorFromAttr
import org.mozilla.fenix.databinding.FragmentAddOnDetailsBinding
import org.mozilla.fenix.ext.addUnderline
import java.text.DateFormat
import java.text.NumberFormat
import java.util.Locale
import com.google.android.material.R as materialR
import mozilla.components.feature.addons.R as addonsR

interface AddonDetailsInteractor {

    /**
     * Open the given URL in the browser.a
     */
    fun openWebsite(url: Uri)

    /**
     * Display the updater dialog.
     */
    fun showUpdaterDialog(addon: Addon)
}

/**
 * Shows the details of an add-on.
 */
class AddonDetailsBindingDelegate(
    private val binding: FragmentAddOnDetailsBinding,
    private val interactor: AddonDetailsInteractor,
) {

    private val dateFormatter = DateFormat.getDateInstance()
    private val numberFormatter = NumberFormat.getNumberInstance(Locale.getDefault())

    fun bind(addon: Addon) {
        bindDetails(addon)
        bindAuthor(addon)
        bindVersion(addon)
        bindLastUpdated(addon)
        bindHomepage(addon)
        bindRating(addon)
        bindDetailUrl(addon)
    }

    private fun bindRating(addon: Addon) {
        addon.rating?.let { rating ->
            val resources = binding.root.resources
            val ratingContentDescription =
                resources.getString(addonsR.string.mozac_feature_addons_rating_content_description_2)
            binding.ratingLabel.contentDescription = String.format(ratingContentDescription, rating.average)
            binding.ratingView.rating = rating.average

            val reviewCount = resources.getString(addonsR.string.mozac_feature_addons_user_rating_count_2)
            binding.reviewCount.contentDescription = String.format(reviewCount, numberFormatter.format(rating.reviews))
            binding.reviewCount.text = numberFormatter.format(rating.reviews)

            if (addon.ratingUrl.isNotBlank()) {
                binding.reviewCount.setTextColor(binding.root.context.getColorFromAttr(materialR.attr.colorTertiary))
                binding.reviewCount.addUnderline()
                binding.reviewCount.setOnClickListener {
                    interactor.openWebsite(addon.ratingUrl.toUri())
                }
            }
        }
    }

    private fun bindHomepage(addon: Addon) {
        if (addon.homepageUrl.isBlank()) {
            binding.homePageLabel.isVisible = false
            binding.homePageDivider.isVisible = false
            return
        }

        binding.homePageLabel.addUnderline()
        binding.homePageLabel.setOnClickListener {
            interactor.openWebsite(addon.homepageUrl.toUri())
        }
    }

    private fun bindLastUpdated(addon: Addon) {
        if (addon.updatedAt.isBlank()) {
            binding.lastUpdatedLabel.isVisible = false
            binding.lastUpdatedText.isVisible = false
            binding.lastUpdatedDivider.isVisible = false
            return
        }

        val formattedDate = dateFormatter.format(addon.updatedAtDate)
        binding.lastUpdatedText.text = formattedDate
        binding.lastUpdatedLabel.joinContentDescriptions(formattedDate)
    }

    private fun bindVersion(addon: Addon) {
        var version = addon.installedState?.version
        if (version.isNullOrEmpty()) {
            version = addon.version
        }
        binding.versionText.text = version

        if (addon.isInstalled()) {
            binding.versionText.setOnLongClickListener {
                interactor.showUpdaterDialog(addon)
                true
            }
        } else {
            binding.versionText.setOnLongClickListener(null)
        }
        binding.versionLabel.joinContentDescriptions(version)
    }

    private fun bindAuthor(addon: Addon) {
        val author = addon.author
        if (author == null || author.name.isBlank()) {
            binding.authorLabel.isVisible = false
            binding.authorText.isVisible = false
            binding.authorDivider.isVisible = false
            return
        }

        binding.authorText.text = author.name

        if (author.url.isNotBlank()) {
            binding.authorText.setTextColor(binding.root.context.getColorFromAttr(materialR.attr.colorTertiary))
            binding.authorText.addUnderline()
            binding.authorText.setOnClickListener {
                interactor.openWebsite(author.url.toUri())
            }
        }
        binding.authorLabel.joinContentDescriptions(author.name)
    }

    private fun bindDetails(addon: Addon) {
        val detailsText = addon.translateDescription(binding.root.context)

        val parsedText = detailsText.replace("\n", "<br/>")
        val text = HtmlCompat.fromHtml(parsedText, HtmlCompat.FROM_HTML_MODE_COMPACT)

        val spannableStringBuilder = SpannableStringBuilder(text)
        val links = spannableStringBuilder.getSpans<URLSpan>()
        for (link in links) {
            addActionToLinks(spannableStringBuilder, link)
        }
        binding.details.text = spannableStringBuilder
        binding.details.movementMethod = LinkMovementMethod.getInstance()
    }

    private fun addActionToLinks(
        spannableStringBuilder: SpannableStringBuilder,
        link: URLSpan,
    ) {
        val start = spannableStringBuilder.getSpanStart(link)
        val end = spannableStringBuilder.getSpanEnd(link)
        val flags = spannableStringBuilder.getSpanFlags(link)
        val clickable: ClickableSpan = object : ClickableSpan() {
            override fun onClick(view: View) {
                view.setOnClickListener {
                    interactor.openWebsite(link.url.toUri())
                }
            }
        }
        spannableStringBuilder.setSpan(clickable, start, end, flags)
        spannableStringBuilder.removeSpan(link)
    }

    private fun bindDetailUrl(addon: Addon) {
        if (addon.detailUrl.isBlank()) {
            binding.detailUrl.isVisible = false
            binding.detailUrlDivider.isVisible = false
            return
        }

        binding.detailUrl.addUnderline()
        binding.detailUrl.setOnClickListener {
            interactor.openWebsite(addon.detailUrl.toUri())
        }
    }

    @VisibleForTesting
    internal fun TextView.joinContentDescriptions(text: String) {
        this.contentDescription = "${this.text} $text"
    }
}
