/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.studies

import android.content.Context
import android.text.SpannableStringBuilder
import android.text.method.LinkMovementMethod
import android.text.style.ClickableSpan
import android.text.style.URLSpan
import android.view.View
import android.widget.TextView
import androidx.annotation.VisibleForTesting
import androidx.core.content.edit
import androidx.core.text.HtmlCompat
import androidx.core.text.getSpans
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.materialswitch.MaterialSwitch
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.service.nimbus.NimbusApi
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.experiments.nimbus.internal.EnrolledExperiment
import org.mozilla.fenix.GleanMetrics.Preferences
import org.mozilla.fenix.R
import org.mozilla.fenix.databinding.SettingsStudiesBinding
import org.mozilla.fenix.ext.getPreferenceKey
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.settings.SupportUtils.SumoTopic.OPT_OUT_STUDIES
import org.mozilla.fenix.utils.Settings

class StudiesView(
    private val scope: CoroutineScope,
    private val context: Context,
    private val binding: SettingsStudiesBinding,
    private val interactor: StudiesInteractor,
    private val settings: Settings,
    private val experiments: NimbusApi,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val isAttached: () -> Boolean,
) : StudiesAdapterDelegate {
    private val logger = Logger("StudiesView")

    @VisibleForTesting internal lateinit var adapter: StudiesAdapter

    @Suppress("TooGenericExceptionCaught", "ApplySharedPref")
    fun bind() {
        provideStudiesTitle().text = getSwitchTitle()
        provideStudiesSwitch().isChecked = settings.isExperimentationEnabled
        provideStudiesSwitch().setOnClickListener {
            val isChecked = provideStudiesSwitch().isChecked
            Preferences.studiesPreferenceEnabled.record(Preferences.StudiesPreferenceEnabledExtra(isChecked))
            provideStudiesTitle().text = getSwitchCheckedTitle()

            settings.isExperimentationEnabled = isChecked
            settings.hasUserDisabledExperimentation = !isChecked
            val experimentsKey = context.getPreferenceKey(R.string.pref_key_experimentation_v2)
            settings.preferences.edit(commit = true) { putBoolean(experimentsKey, isChecked) }

            // Use experimentParticipation for studies-specific settings
            experiments.experimentParticipation = isChecked
        }
        bindDescription()

        scope.launch {
            try {
                val activeExperiments =
                    withContext(ioDispatcher) {
                        experiments.getActiveExperiments()
                    }
                if (isAttached()) {
                    adapter =
                        StudiesAdapter(
                            this@StudiesView,
                            activeExperiments,
                        )
                    provideStudiesList().adapter = adapter
                }
            } catch (e: Throwable) {
                logger.error("Failed to getActiveExperiments()", e)
            }
        }
    }

    override fun onRemoveButtonClicked(experiment: EnrolledExperiment) {
        interactor.removeStudy(experiment)
        adapter.removeStudy(experiment)
    }

    @VisibleForTesting
    internal fun bindDescription() {
        val sumoUrl = SupportUtils.getGenericSumoURLForTopic(OPT_OUT_STUDIES)
        val description = context.getString(R.string.studies_description_4)
        val learnMore = context.getString(R.string.studies_learn_more)
        val rawText = "$description <a href=\"$sumoUrl\">$learnMore</a>"
        val text = HtmlCompat.fromHtml(rawText, HtmlCompat.FROM_HTML_MODE_COMPACT)

        val spannableStringBuilder = SpannableStringBuilder(text)
        val links = spannableStringBuilder.getSpans<URLSpan>()
        for (link in links) {
            addActionToLinks(spannableStringBuilder, link)
        }
        binding.studiesDescription.text = spannableStringBuilder
        binding.studiesDescription.movementMethod = LinkMovementMethod.getInstance()
    }

    private fun addActionToLinks(
        spannableStringBuilder: SpannableStringBuilder,
        link: URLSpan,
    ) {
        val start = spannableStringBuilder.getSpanStart(link)
        val end = spannableStringBuilder.getSpanEnd(link)
        val flags = spannableStringBuilder.getSpanFlags(link)
        val clickable: ClickableSpan =
            object : ClickableSpan() {
                override fun onClick(view: View) {
                    view.setOnClickListener {
                        interactor.openWebsite(link.url)
                    }
                }
            }
        spannableStringBuilder.setSpan(clickable, start, end, flags)
        spannableStringBuilder.removeSpan(link)
    }

    @VisibleForTesting
    internal fun getSwitchTitle(): String {
        val stringId =
            if (settings.isExperimentationEnabled) {
                R.string.studies_on
            } else {
                R.string.studies_off
            }
        return context.getString(stringId)
    }

    @VisibleForTesting
    internal fun getSwitchCheckedTitle(): String {
        val stringId =
            if (provideStudiesSwitch().isChecked) {
                R.string.studies_on
            } else {
                R.string.studies_off
            }
        return context.getString(stringId)
    }

    @VisibleForTesting internal fun provideStudiesTitle(): TextView = binding.studiesTitle

    @VisibleForTesting internal fun provideStudiesSwitch(): MaterialSwitch = binding.studiesSwitch

    @VisibleForTesting internal fun provideStudiesList(): RecyclerView = binding.studiesList
}
