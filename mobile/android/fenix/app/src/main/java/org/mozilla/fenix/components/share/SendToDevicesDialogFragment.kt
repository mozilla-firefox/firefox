/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.share

import android.app.Dialog
import android.content.Intent
import android.content.res.Configuration
import android.net.ConnectivityManager
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.appcompat.content.res.AppCompatResources
import androidx.compose.runtime.collectAsState
import androidx.core.content.getSystemService
import androidx.core.net.toUri
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat.Type.systemBars
import androidx.fragment.app.viewModels
import androidx.fragment.compose.content
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.lifecycleScope
import com.google.android.material.R as materialR
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import com.google.android.material.snackbar.Snackbar
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.concept.sync.AccountObserver
import mozilla.components.concept.sync.AuthType
import mozilla.components.concept.sync.OAuthAccount
import mozilla.components.concept.sync.TabData
import mozilla.components.concept.sync.TabPrivacy
import mozilla.components.feature.accounts.push.SendTabUseCases
import mozilla.components.feature.share.RecentAppsStorage
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.support.utils.ext.isLandscape
import mozilla.components.support.utils.ext.packageManagerCompatHelper
import mozilla.components.support.utils.ext.top
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.GleanMetrics.SyncAccount
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.share.DefaultShareController.Companion.ACTION_COPY_LINK_TO_CLIPBOARD
import org.mozilla.fenix.share.ShareViewModel
import org.mozilla.fenix.share.listadapters.AppShareOption
import org.mozilla.fenix.share.listadapters.SyncShareOption
import org.mozilla.fenix.snackbar.FenixSnackbarDelegate

/** A [BottomSheetDialogFragment] that allows the user to send a tab to their other devices. */
class SendToDevicesDialogFragment : BottomSheetDialogFragment() {

    private val model: ShareViewModel by viewModels {
        object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : ViewModel> create(modelClass: Class<T>): T {
                val app = requireContext().applicationContext
                return ShareViewModel(
                    fxaAccountManager = requireComponents.backgroundServices.accountManager,
                    recentAppsStorage = RecentAppsStorage(app),
                    connectivityManager = app.getSystemService<ConnectivityManager>(),
                    packageManager = app.packageManager,
                    packageName = app.packageName,
                    getCopyApp = ::getCopyApp,
                    queryIntentActivitiesCompat = { intent ->
                        app.packageManagerCompatHelper.queryIntentActivitiesCompat(intent, 0)
                    },
                )
                    as T
            }
        }
    }

    private val sendTabUseCases by lazy {
        SendTabUseCases(requireComponents.backgroundServices.accountManager)
    }

    private var tabs: List<TabData> = emptyList()
    private var hasNavigatedToSignIn = false

    private val accountObserver =
        object : AccountObserver {
            override fun onAuthenticated(account: OAuthAccount, authType: AuthType) {
                onAuthenticated()
            }
        }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ) = content {
        val uiState = model.uiState.collectAsState().value
        SendToDevicesContent(
            uiState = uiState,
            onDismiss = { dismiss() },
            onSendToDevice = { option: SyncShareOption.SingleDevice ->
                sendAndDismiss { sendTabsToDevice(option.device.id, tabs) }
            },
            onSendToAll = {
                sendAndDismiss { sendTabsToAllDevices(tabs) }
            },
        )
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
        requireComponents.backgroundServices.accountManager.register(
            accountObserver,
            owner = this,
            autoPause = false,
        )
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

    override fun onResume() {
        super.onResume()
        checkAuthAndNavigate(requireComponents.backgroundServices.accountManager)
    }

    internal fun onAuthenticated() {
        model.initDataLoad()
    }

    internal fun checkAuthAndNavigate(accountManager: FxaAccountManager) {
        if (accountManager.authenticatedAccount() != null) {
            onAuthenticated()
        } else if (!hasNavigatedToSignIn) {
            hasNavigatedToSignIn = true
            navigateToSignIn()
        }
    }

    internal fun navigateToSignIn() {
        SyncAccount.signInToSendTab.record(NoExtras())
        requireActivity()
            .startActivity(
                Intent(
                        Intent.ACTION_VIEW,
                        "${BuildConfig.DEEP_LINK_SCHEME}://turn_on_sync".toUri(),
                    )
                    .apply {
                        setPackage(requireActivity().packageName)
                        flags = Intent.FLAG_ACTIVITY_CLEAR_TOP
                    }
            )
    }

    private fun getCopyApp(): AppShareOption? {
        val copyIcon = AppCompatResources.getDrawable(requireContext(), R.drawable.ic_share_clipboard)
        return copyIcon?.let {
            AppShareOption(
                requireContext().getString(R.string.share_copy_link_to_clipboard),
                it,
                ACTION_COPY_LINK_TO_CLIPBOARD,
                "",
            )
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
            dismiss()
        }
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

    private suspend fun sendTabsToDevice(
        deviceId: String,
        tabs: List<TabData>,
    ): Boolean {
        return sendTabUseCases.sendToDeviceAsync
            .invoke(
                deviceId = deviceId,
                tabs = tabs,
            )
            .await()
    }

    private suspend fun sendTabsToAllDevices(tabs: List<TabData>): Boolean {
        return sendTabUseCases.sendToAllAsync.invoke(tabs = tabs).await()
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
