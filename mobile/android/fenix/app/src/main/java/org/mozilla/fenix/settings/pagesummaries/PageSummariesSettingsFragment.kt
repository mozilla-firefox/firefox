/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.pagesummaries

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.fragment.app.Fragment
import androidx.fragment.compose.content
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.lifecycleScope
import mozilla.components.feature.summarize.settings.LearnMoreHandled
import mozilla.components.feature.summarize.settings.SummarizeSettingsContent
import mozilla.components.feature.summarize.settings.SummarizeSettingsMiddleware
import mozilla.components.feature.summarize.settings.SummarizeSettingsState
import mozilla.components.feature.summarize.settings.SummarizeSettingsStore
import mozilla.components.feature.summarize.settings.asMiddleware
import mozilla.components.feature.summarize.settings.summarizeSettingsReducer
import org.mozilla.fenix.R
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.theme.FirefoxTheme

/** A fragment displaying the Page Summaries settings screen. */
class PageSummariesSettingsFragment : Fragment(), SystemInsetsPaddedFragment {

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_page_summaries))
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        val summarizeSettings = requireComponents.summarizationSettings
        val cache = requireComponents.core.summarizationSettingsBinding
        val store =
            SummarizeSettingsStore(
                initialState =
                    SummarizeSettingsState(
                        isFeatureEnabled = cache.isFeatureEnabled.value,
                        isGestureEnabled = cache.isGestureEnabled.value,
                        shakeSensitivity = cache.shakeSensitivity.value,
                    ),
                reducer = ::summarizeSettingsReducer,
                middleware =
                    listOf(
                        SummarizeSettingsMiddleware(
                                settings = summarizeSettings,
                                scope = viewLifecycleOwner.lifecycleScope,
                            )
                            .asMiddleware()
                    ),
            )

        return content {
            val state by store.stateFlow.collectAsStateWithLifecycle()
            LaunchedEffect(state.isLearnMoreRequested) {
                if (state.isLearnMoreRequested) {
                    openLearnMoreLink()
                    store.dispatch(LearnMoreHandled)
                }
            }

            FirefoxTheme {
                SummarizeSettingsContent(
                    store = store,
                    modifier = Modifier.padding(horizontal = 16.dp),
                )
            }
        }
    }

    private fun openLearnMoreLink() {
        val url = SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.PAGE_SUMMARIZATION)
        SupportUtils.launchSandboxCustomTab(requireContext(), url)
    }
}
