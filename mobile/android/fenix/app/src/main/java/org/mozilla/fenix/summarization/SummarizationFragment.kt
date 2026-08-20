/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import android.app.Dialog
import android.content.Context
import android.content.DialogInterface
import android.graphics.Color
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
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
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import com.google.android.material.R as materialR
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import mozilla.components.browser.state.selector.findTabOrCustomTabOrSelectedTab
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.summarize.SummarizationState
import mozilla.components.feature.summarize.SummarizationUi
import mozilla.components.feature.summarize.SummarizeSettingsActionWrapper
import mozilla.components.feature.summarize.ViewDismissed
import mozilla.components.feature.summarize.settings.LearnMoreHandled
import mozilla.components.feature.summarize.settings.SummarizeSettingsState
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.android.view.setNavigationBarColorCompat
import mozilla.components.support.utils.ext.top
import org.mozilla.fenix.R
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.tabstray.ext.toDisplayTitle
import org.mozilla.fenix.theme.FirefoxTheme

private const val HIDING_FRICTION = 0.9f

private fun Context.getConnectionType(): ConnectionType {
    val connectivityManager = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    val capabilities = connectivityManager.getNetworkCapabilities(connectivityManager.activeNetwork)
    return when {
        capabilities == null -> ConnectionType.NONE
        capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> ConnectionType.WIFI
        capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) -> ConnectionType.CELLULAR
        else -> ConnectionType.OTHER
    }
}

/** Summarization UI entry fragment. */
class SummarizationFragment : BottomSheetDialogFragment() {
    private val args by navArgs<SummarizationFragmentArgs>()
    private val browserStore: BrowserStore
        get() = requireComponents.core.store

    private val currentTab: SessionState?
        get() = browserStore.state.findTabOrCustomTabOrSelectedTab(args.sessionId)

    private val isEngineAvailable: Boolean
        get() = currentTab?.engineState?.engineSession != null

    private val storeViewModel: SummarizationStoreViewModel by viewModels {
        val title = currentTab?.toDisplayTitle() ?: ""
        val cache = requireComponents.core.summarizationSettingsBinding

        SummarizationStoreViewModel.factory(
            currentTab = currentTab,
            initializedFromShake = args.fromShake,
            pageTitle = title,
            connectionType = requireContext().getConnectionType(),
            llmProvider = requireComponents.llm.mlpaProvider,
            settings = requireComponents.summarizationSettings,
            loadCachedSettings = {
                SummarizeSettingsState(
                    isFeatureEnabled = cache.isFeatureEnabled.value,
                    isGestureEnabled = cache.isGestureEnabled.value,
                    shakeSensitivity = cache.shakeSensitivity.value,
                )
            },
            errorReporter = { tag, exception ->
                requireComponents.analytics.crashReporter.submitCaughtException(exception)
                Logger(tag).error(exception.message ?: "", exception)
            },
        )
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // if we're recreating the backstack while resuming, we need to check that the tab hasn't been killed in the
        // background
        if (savedInstanceState != null && !isEngineAvailable) {
            dismiss()
        }
    }

    override fun onStart() {
        super.onStart()
        val bottomSheet = dialog?.findViewById<View>(materialR.id.design_bottom_sheet)
        bottomSheet?.let { sheet ->
            with(BottomSheetBehavior.from(sheet)) {
                skipCollapsed = true
                state = BottomSheetBehavior.STATE_EXPANDED
                hideFriction = HIDING_FRICTION
            }
        }
    }

    override fun onDismiss(dialog: DialogInterface) {
        super.onDismiss(dialog)
        storeViewModel.store.dispatch(ViewDismissed(isEngineAvailable))
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog =
        super.onCreateDialog(savedInstanceState).apply {
            setOnShowListener {
                val bottomSheet = findViewById<View>(materialR.id.design_bottom_sheet) ?: return@setOnShowListener
                ViewCompat.setOnApplyWindowInsetsListener(bottomSheet) { view, insets ->
                    // edge-to-edge workaround
                    // exclude the bottom insets so that we can handle the insets in compose
                    view.setPadding(0, insets.top(), 0, 0)
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
    ): View {
        return content {
            val state by storeViewModel.store.stateFlow.collectAsStateWithLifecycle()
            LaunchedEffect(state) {
                when (val currentState = state) {
                    SummarizationState.LearnMoreAboutCloudSupportedFeatures -> {
                        openLearnMoreLink(SupportUtils.SumoTopic.CLOUD_SUPPORTED_FEATURES)
                        dismiss()
                    }
                    SummarizationState.LearnMoreAboutShakeConsent -> {
                        openLearnMoreLink(SupportUtils.SumoTopic.PAGE_SUMMARIZATION)
                    }
                    is SummarizationState.Settings -> {
                        if (currentState.settingsState.isLearnMoreRequested) {
                            openLearnMoreLink(SupportUtils.SumoTopic.PAGE_SUMMARIZATION)
                            storeViewModel.store.dispatch(SummarizeSettingsActionWrapper(LearnMoreHandled))
                        }
                    }
                    SummarizationState.Finished.NavigatedToSignIn -> {
                        navigateToSignIn()
                        dismiss()
                    }
                    is SummarizationState.Finished -> {
                        dismiss()
                    }
                    else -> {}
                }
            }

            FirefoxTheme {
                SummarizationUi(
                    productName = getString(R.string.app_name),
                    store = storeViewModel.store,
                    resolveError = { throwable -> ErrorCodeLookup.lookup(throwable).code },
                )
            }
        }
    }

    private fun navigateToSignIn() {
        val directions =
            SummarizationFragmentDirections.actionSummarizationFragmentToTurnOnSyncFragment(
                entrypoint = FenixFxAEntryPoint.ShakeToSummarize
            )
        findNavController().navigate(directions)
    }

    private fun openLearnMoreLink(topic: SupportUtils.SumoTopic) {
        val url = SupportUtils.getGenericSumoURLForTopic(topic)
        SupportUtils.launchSandboxCustomTab(requireContext(), url)
    }
}
