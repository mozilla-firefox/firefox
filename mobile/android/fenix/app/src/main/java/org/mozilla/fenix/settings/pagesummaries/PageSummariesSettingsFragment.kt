/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.pagesummaries

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.fragment.app.Fragment
import androidx.fragment.compose.content
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import mozilla.components.feature.summarize.settings.SummarizationSettings
import mozilla.components.feature.summarize.settings.SummarizeSettingsContent
import mozilla.components.feature.summarize.settings.SummarizeSettingsMiddleware
import mozilla.components.feature.summarize.settings.SummarizeSettingsStore
import mozilla.components.feature.summarize.settings.summarizeSettingsReducer
import mozilla.components.lib.state.helpers.StoreProvider.Companion.storeProvider
import org.mozilla.fenix.R
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.settings.SettingsState
import org.mozilla.fenix.settings.SettingsStore
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.settings.settingsReducer
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * A fragment displaying the Page Summaries settings screen.
 */
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
        val summarizeSettings = SummarizationSettings.dataStore(requireContext())
        val navController = findNavController()
        val settingsStore: SettingsStore =
            navController.getBackStackEntry(R.id.settingsFragment)
                .storeProvider.get<SettingsState, SettingsStore> { persistedState ->
                    SettingsStore(persistedState ?: SettingsState(), ::settingsReducer, middleware = listOf())
                }

        return content {
            val store = remember {
                SummarizeSettingsStore(
                    initialState = settingsStore.state.summarizeSettingsState,
                    reducer = ::summarizeSettingsReducer,
                    middleware = listOf(
                        SummarizeSettingsMiddleware(
                            settings = summarizeSettings,
                            onLearnMoreClicked = { openLearnMoreLink() },
                            scope = viewLifecycleOwner.lifecycleScope,
                        ),
                    ),
                )
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
