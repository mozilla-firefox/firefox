/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.share

import android.app.Dialog
import android.content.Context
import android.content.res.Configuration
import android.net.ConnectivityManager
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.platform.ComposeView
import androidx.core.content.getSystemService
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat.Type.systemBars
import androidx.fragment.compose.content
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.viewModelScope
import com.google.android.material.R as materialR
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import com.google.android.material.snackbar.Snackbar
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.concept.sync.TabData
import mozilla.components.concept.sync.TabPrivacy
import mozilla.components.feature.accounts.push.SendTabUseCases
import mozilla.components.lib.state.helpers.StoreProvider.Companion.fragmentStore
import mozilla.components.lib.state.helpers.StoreProvider.Companion.storeProvider
import mozilla.components.service.fxa.manager.SCOPE_PROFILE
import mozilla.components.service.fxa.manager.SCOPE_SYNC
import mozilla.components.support.utils.ext.isLandscape
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.SyncAuth
import org.mozilla.fenix.R
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.share.store.ShareUiAction
import org.mozilla.fenix.components.share.store.ShareUiState
import org.mozilla.fenix.components.share.store.ShareUiStore
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.settings.account.SignOutFragment
import org.mozilla.fenix.share.listadapters.SyncShareOption
import org.mozilla.fenix.snackbar.FenixSnackbarDelegate

/** A [BottomSheetDialogFragment] that allows the user to send a tab to their other devices. */
class SendToDevicesDialogFragment : BottomSheetDialogFragment() {

    private lateinit var shareUiStore: ShareUiStore
    private lateinit var deviceObserver: ShareUiDevicesObserver

    private val sendTabUseCases by lazy {
        SendTabUseCases(requireComponents.backgroundServices.accountManager)
    }

    private var tabs: List<TabData> = emptyList()

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): ComposeView {
        shareUiStore =
            fragmentStore(ShareUiState.initial) {
                    ShareUiStore(initialState = it)
                }
                .value

        return content {
            val uiState by shareUiStore.stateFlow.collectAsState()
            SendToDevicesContent(
                uiState = uiState,
                onDismiss = { dismiss() },
                onSend = { devices: Set<SyncShareOption.SingleDevice> ->
                    sendAndDismiss {
                        sendTabUseCases.sendToDeviceAsync.invoke(devices.map { it.device }, tabs).await()
                    }
                },
                onDeviceSelectionToggle = { shareUiStore.dispatch(ShareUiAction.DeviceSelectionToggle(it)) },
                onSignInClicked = {
                    reconnectToSync(requireContext())
                },
                onSignOutClicked = {
                    removeAccountFromSync()
                },
                onRetryClicked = { deviceObserver.refreshDevices(null) },
                onLearnMoreClicked = {
                    openLearnMoreLink()
                },
            )
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        val app = requireContext().applicationContext
        deviceObserver =
            ShareUiDevicesObserver(
                store = shareUiStore,
                fxaAccountManager = requireComponents.backgroundServices.accountManager,
                connectivityManager = app.getSystemService<ConnectivityManager>(),
                scope = storeProvider.viewModelScope,
            )
        viewLifecycleOwner.lifecycle.addObserver(deviceObserver)
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        return super.onCreateDialog(savedInstanceState).apply {
            setOnShowListener {
                val bottomSheet = findViewById<View>(materialR.id.design_bottom_sheet) ?: return@setOnShowListener
                ViewCompat.setOnApplyWindowInsetsListener(bottomSheet) { view, insets ->
                    val systemBarInsets = insets.getInsets(systemBars())
                    view.setPadding(0, systemBarInsets.top, 0, systemBarInsets.bottom)
                    insets
                }
                bottomSheet.setBackgroundResource(R.drawable.bottom_sheet_with_top_rounded_corners)
            }
        }
    }

    override fun onStart() {
        super.onStart()
        updateSheetHeight()
        loadTabData(arguments)
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        updateSheetHeight()
    }

    private fun updateSheetHeight() {
        val bottomSheet = dialog?.findViewById<View>(materialR.id.design_bottom_sheet) ?: return
        BottomSheetBehavior.from(bottomSheet).peekHeight =
            if (requireContext().isLandscape()) {
                resources.displayMetrics.heightPixels
            } else {
                BottomSheetBehavior.PEEK_HEIGHT_AUTO
            }
    }

    internal fun loadTabData(bundle: Bundle?) {
        val urls = bundle?.getStringArrayList(EXTRA_URLS).orEmpty()
        val titles = bundle?.getStringArrayList(EXTRA_TITLES).orEmpty()
        val privacy =
            if (bundle?.getString(EXTRA_PRIVACY) == PRIVACY_PRIVATE) {
                TabPrivacy.Private
            } else {
                TabPrivacy.Normal
            }
        tabs = urls.mapIndexed { i, url ->
            TabData(url = url, title = titles.getOrNull(i).orEmpty(), privacy = privacy)
        }
    }

    private fun reconnectToSync(context: Context) {
        context.components.services.accountsAuthFeature.beginAuthentication(
            context,
            FenixFxAEntryPoint.ShareMenu,
            setOf(SCOPE_PROFILE, SCOPE_SYNC),
        )
        SyncAuth.useEmailProblem.record(NoExtras())
    }

    private fun removeAccountFromSync() {
        val fragmentManager = parentFragmentManager
        dismiss()
        if (fragmentManager.findFragmentByTag("SignOutFragment") == null) {
            SignOutFragment().show(fragmentManager, "SignOutFragment")
        }
    }

    private fun sendAndDismiss(send: suspend () -> Boolean) {
        val delegate = FenixSnackbarDelegate(requireActivity().findViewById(android.R.id.content))
        lifecycleScope.launch {
            showSendResult(
                retryScope = requireActivity().lifecycleScope,
                onSuccess = { text -> delegate.show(text = text, duration = Snackbar.LENGTH_SHORT) },
                onFailure = { onRetry ->
                    delegate.show(
                        text = R.string.sync_sent_tab_error_snackbar,
                        duration = Snackbar.LENGTH_LONG,
                        isError = true,
                        action = R.string.sync_sent_tab_error_snackbar_action,
                    ) {
                        onRetry()
                    }
                },
                send = send,
            )
            dismissAllowingStateLoss()
        }
    }

    private fun openLearnMoreLink() {
        val url = SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.CONNECT_ADDITIONAL_DEVICES_TO_SYNC)
        SupportUtils.launchSandboxCustomTab(requireContext(), url)
    }

    /**
     * Displays a notification with the result of sending tabs.
     *
     * Shows a success message if [send] succeeds, or triggers [onFailure] if it fails. If the user retries,
     * [showSendResult] is called again inside [retryScope]. This ensures the retry operation continues even if the
     * calling component is dismissed.
     */
    internal suspend fun showSendResult(
        retryScope: CoroutineScope,
        onSuccess: (Int) -> Unit,
        onFailure: (onRetry: () -> Unit) -> Unit,
        send: suspend () -> Boolean,
    ) {
        if (send()) {
            onSuccess(
                if (tabs.size == 1) {
                    R.string.sync_sent_tab_snackbar_2
                } else {
                    R.string.sync_sent_tabs_snackbar_2
                }
            )
            return
        }

        onFailure {
            retryScope.launch {
                showSendResult(retryScope, onSuccess, onFailure, send)
            }
        }
    }

    companion object {
        const val TAG = "SendToDevicesDialogFragment"

        internal const val EXTRA_URLS = "urls"
        internal const val EXTRA_TITLES = "titles"
        internal const val EXTRA_PRIVACY = "privacy"
        internal const val PRIVACY_PRIVATE = "PRIVATE"
        internal const val PRIVACY_NORMAL = "NORMAL"

        /**
         * Creates a new instance of [SendToDevicesDialogFragment] with the provided URLs, titles, and privacy status.
         *
         * @param urls The URLs of the tabs to be sent.
         * @param titles The titles of the tabs to be sent, aligned by index with [urls].
         * @param isPrivate Whether the tabs are private or not.
         */
        fun newInstance(urls: List<String>, titles: List<String>, isPrivate: Boolean) =
            SendToDevicesDialogFragment().apply {
                arguments =
                    Bundle().apply {
                        putStringArrayList(EXTRA_URLS, ArrayList(urls))
                        putStringArrayList(EXTRA_TITLES, ArrayList(titles))
                        putString(EXTRA_PRIVACY, if (isPrivate) PRIVACY_PRIVATE else PRIVACY_NORMAL)
                    }
            }
    }
}
