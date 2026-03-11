/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import android.app.Dialog
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.viewModels
import androidx.compose.runtime.LaunchedEffect
import androidx.fragment.compose.content
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation.fragment.navArgs
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import kotlinx.coroutines.suspendCancellableCoroutine
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.concept.engine.EngineSession
import mozilla.components.feature.summarize.SummarizationSettings
import mozilla.components.feature.summarize.SummarizationState
import mozilla.components.feature.summarize.SummarizationUi
import mozilla.components.feature.summarize.content.PageContentExtractor
import mozilla.components.feature.summarize.settings.SummarizeSettingsMiddleware
import mozilla.components.feature.summarize.settings.SummarizeSettingsState
import mozilla.components.feature.summarize.settings.SummarizeSettingsStore
import mozilla.components.feature.summarize.settings.summarizeSettingsReducer
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.theme.FirefoxTheme
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import com.google.android.material.R as materialR

/**
 * Gets the content for a given engine session.
 */
private fun EngineSession?.asPageContentExtractor(): PageContentExtractor = {
    runCatching {
        suspendCancellableCoroutine { continuation ->
            this!!.getPageContent(
                onResult = { content ->
                    continuation.resume(content)
                },
                onException = { error ->
                    continuation.resumeWithException(error)
                },
            )
        }
    }
}

/**
 * Summarization UI entry fragment.
 */
class SummarizationFragment : BottomSheetDialogFragment() {
    private val args by navArgs<SummarizationFragmentArgs>()
    private val storeViewModel: SummarizationStoreViewModel by viewModels {
        val engineSession = requireComponents.core.store.state.selectedTab?.engineState?.engineSession
        val provider = requireComponents.llm.mlpaProvider
        SummarizationStoreViewModel.factory(
            initializedFromShake = args.fromShake,
            llmProvider = provider,
            settings = SummarizationSettings.sharedPrefs(requireContext()),
            pageContentExtractor = engineSession.asPageContentExtractor(),
        )
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog =
        super.onCreateDialog(savedInstanceState).apply {
            setOnShowListener {
                val bottomSheet = findViewById<View?>(materialR.id.design_bottom_sheet)
                bottomSheet?.setBackgroundResource(android.R.color.transparent)
            }
        }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View = content {
        val summarizeSettings = requireContext().components.core.summarizeFeatureSettings
        val settingsStore = SummarizeSettingsStore(
            initialState = SummarizeSettingsState(
                summarizePagesEnabled = summarizeSettings.summarizePagesEnabled,
                shakeToSummarizeEnabled = summarizeSettings.shakeToSummarizeEnabled,
            ),
            reducer = ::summarizeSettingsReducer,
            middleware = listOf(
                SummarizeSettingsMiddleware(
                    settings = summarizeSettings,
                    onLearnMoreClicked = {},
                ),
            ),
        )

        val state = storeViewModel.store.stateFlow.collectAsStateWithLifecycle()
        LaunchedEffect(state.value) {
            if (state.value is SummarizationState.Finished) {
                dismiss()
            }
        }

        FirefoxTheme {
            SummarizationUi(
                productName = getString(R.string.app_name),
                store = storeViewModel.store,
                settingsStore = settingsStore,
            )
        }
    }
}
