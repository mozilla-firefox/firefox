/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import android.app.Dialog
import android.graphics.Color
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.core.view.ViewCompat
import androidx.fragment.app.viewModels
import androidx.fragment.compose.content
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewModelScope
import androidx.navigation.fragment.navArgs
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import kotlinx.coroutines.suspendCancellableCoroutine
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.concept.engine.EngineSession
import mozilla.components.feature.summarize.SummarizationState
import mozilla.components.feature.summarize.SummarizationUi
import mozilla.components.feature.summarize.content.PageContentExtractor
import mozilla.components.feature.summarize.content.PageMetadata
import mozilla.components.feature.summarize.content.PageMetadataExtractor
import mozilla.components.feature.summarize.settings.SummarizationSettings
import mozilla.components.feature.summarize.settings.SummarizeSettingsMiddleware
import mozilla.components.feature.summarize.settings.SummarizeSettingsState
import mozilla.components.feature.summarize.settings.SummarizeSettingsStore
import mozilla.components.feature.summarize.settings.summarizeSettingsReducer
import mozilla.components.support.ktx.android.view.setNavigationBarColorCompat
import mozilla.components.support.utils.ext.left
import mozilla.components.support.utils.ext.right
import mozilla.components.support.utils.ext.top
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.settings.SupportUtils
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
                    continuation.resumeWithException(PageContentExtractor.Exception())
                },
            )
        }
    }
}

private fun EngineSession?.asPageMetadataExtractor(): PageMetadataExtractor = {
    runCatching {
        suspendCancellableCoroutine { continuation ->
            this!!.getPageMetadata(
                onResult = { metadata ->
                    continuation.resume(
                        PageMetadata(
                            structuredDataTypes = metadata.structuredDataTypes,
                            language = metadata.language,
                        ),
                    )
                },
                onException = { error ->
                    continuation.resumeWithException(PageMetadataExtractor.Exception())
                },
            )
        }
    }
}

const val HALF_EXPANDED_RATIO = 0.75f

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
            settings = SummarizationSettings.dataStore(requireContext()),
            pageContentExtractor = engineSession.asPageContentExtractor(),
            pageMetadataExtractor = engineSession.asPageMetadataExtractor(),
            errorReporter = { requireComponents.analytics.crashReporter.submitCaughtException(it) },
        )
    }

    override fun onStart() {
        super.onStart()
        val bottomSheet = dialog?.findViewById<View>(materialR.id.design_bottom_sheet)
        bottomSheet?.let { sheet ->
            val behavior = BottomSheetBehavior.from(sheet)
            behavior.state = BottomSheetBehavior.STATE_EXPANDED
            behavior.isFitToContents = false
            behavior.halfExpandedRatio = HALF_EXPANDED_RATIO
            behavior.expandedOffset = 0
        }
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog =
        super.onCreateDialog(savedInstanceState).apply {
            setOnShowListener {
                val bottomSheet = findViewById<View>(materialR.id.design_bottom_sheet) ?: return@setOnShowListener
                ViewCompat.setOnApplyWindowInsetsListener(bottomSheet) { view, insets ->
                    // edge-to-edge workaround
                    // exclude the bottom insets so that we can handle the insets in compose
                    view.setPadding(insets.left(), insets.top(), insets.right(), 0)
                    insets
                }
                bottomSheet.setBackgroundResource(android.R.color.transparent)
                dialog?.window?.setNavigationBarColorCompat(Color.TRANSPARENT)
            }
        }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View = content {
        val summarizeSettings = SummarizationSettings.dataStore(requireContext())

        val state by storeViewModel.store.stateFlow.collectAsStateWithLifecycle()
        LaunchedEffect(state) {
            when (state) {
                SummarizationState.Finished.LearnMoreAboutShakeConsent -> {
                    openLearnMoreLink()
                    dismiss()
                }
                is SummarizationState.Finished -> {
                    dismiss()
                }
                else -> {}
            }
        }

        val settingsStore = SummarizeSettingsStore(
            initialState = SummarizeSettingsState(),
            reducer = ::summarizeSettingsReducer,
            middleware = listOf(
                SummarizeSettingsMiddleware(
                    settings = summarizeSettings,
                    onLearnMoreClicked = { openLearnMoreLink() },
                    storeViewModel.viewModelScope,
                ),
            ),
        )

        FirefoxTheme {
            SummarizationUi(
                productName = getString(R.string.app_name),
                store = storeViewModel.store,
                settingsStore = settingsStore,
            )
        }
    }

    private fun openLearnMoreLink() {
        val url = SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.PAGE_SUMMARIZATION)
        SupportUtils.launchSandboxCustomTab(requireContext(), url)
    }
}
