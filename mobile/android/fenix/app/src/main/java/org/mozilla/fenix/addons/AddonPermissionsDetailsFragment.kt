/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.addons

import android.content.Context
import android.os.Bundle
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.fragment.app.Fragment
import androidx.fragment.compose.content
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import mozilla.components.browser.state.state.extension.PendingExtensionUpdate
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.Addon.Companion.isAllURLsPermission
import mozilla.components.feature.addons.ui.translateName
import mozilla.components.lib.state.ext.observeAsComposableState
import org.mozilla.fenix.addons.ui.AddonPermissionsScreen
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.openToBrowser
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.theme.FirefoxTheme

/** Optional [Addon.Permission.name] lists for requesting permission updates for an [Addon]. */
data class AddonPermissionsUpdateRequest(
    val optionalPermissions: List<String> = emptyList(),
    val originPermissions: List<String> = emptyList(),
    val dataCollectionPermissions: List<String> = emptyList(),
)

/** A fragment to show and allow a user to change permissions for an addon. */
class AddonPermissionsDetailsFragment : Fragment(), SystemInsetsPaddedFragment {

    private val args by navArgs<AddonPermissionsDetailsFragmentArgs>()

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ) = content {
        val optionalPermissions = rememberSaveable {
            mutableStateOf(args.addon.translateOptionalPermissions(requireContext()))
        }

        val originPermissions = rememberSaveable {
            mutableStateOf(
                args.addon.optionalOrigins.map {
                    Addon.LocalizedPermission(it.name, it)
                }
            )
        }

        val optionalDataCollectionPermissions = rememberSaveable {
            mutableStateOf(args.addon.translateOptionalDataCollectionPermissions(requireContext()))
        }

        // Note: Even if <all_urls> is in optionalPermissions of the extension manifest, it is found in
        // originPermissions of the Addon
        val allSitesHostPermissionsList = rememberSaveable {
            mutableStateOf(args.addon.optionalOrigins.getAllSitesPermissionsList())
        }

        // Update all of the mutable states when an addon is returned from updating permissions
        val onUpdatePermissionsSuccess: (Addon) -> Unit = { updatedAddon ->
            optionalPermissions.value = updatedAddon.translateOptionalPermissions(requireContext())
            originPermissions.value =
                updatedAddon.optionalOrigins.map {
                    Addon.LocalizedPermission(it.name, it)
                }
            optionalDataCollectionPermissions.value =
                updatedAddon.translateOptionalDataCollectionPermissions((requireContext()))
            allSitesHostPermissionsList.value = updatedAddon.optionalOrigins.getAllSitesPermissionsList()
        }

        val pendingUpdate by observePendingUpdate()

        FirefoxTheme {
            AddonPermissionsScreen(
                permissions = args.addon.translatePermissions(requireContext()),
                optionalPermissions = optionalPermissions.value,
                originPermissions = originPermissions.value,
                requiredDataCollectionPermissions =
                    args.addon.translateRequiredDataCollectionPermissions(requireContext()),
                hasNoneDataCollection =
                    args.addon.requiredDataCollectionPermissions.size == 1 &&
                        args.addon.requiredDataCollectionPermissions.contains("none"),
                optionalDataCollectionPermissions = optionalDataCollectionPermissions.value,
                isAllSitesSwitchVisible = allSitesHostPermissionsList.value.isNotEmpty(),
                isAllSitesEnabled = allSitesHostPermissionsList.value.getOrNull(0)?.granted ?: false,
                onAddOptionalPermissions = { permissionRequest ->
                    addOptionalPermissions(permissionRequest, onUpdatePermissionsSuccess)
                },
                onRemoveOptionalPermissions = { permissionRequest ->
                    removeOptionalPermission(permissionRequest, onUpdatePermissionsSuccess)
                },
                onAddAllSitesPermissions = {
                    addOptionalPermissions(
                        AddonPermissionsUpdateRequest(
                            originPermissions = allSitesHostPermissionsList.value.map { it.name }
                        ),
                        onUpdatePermissionsSuccess,
                    )
                },
                onRemoveAllSitesPermissions = {
                    removeOptionalPermission(
                        AddonPermissionsUpdateRequest(
                            originPermissions = allSitesHostPermissionsList.value.map { it.name }
                        ),
                        onUpdatePermissionsSuccess,
                    )
                },
                onLearnMoreClick = { learnMoreUrl ->
                    openWebsite(learnMoreUrl)
                },
                learnMoreUrl = learnMoreUrl(requireContext()),
                hasPendingUpdate = pendingUpdate != null,
                onReviewUpdateClick = { showPendingExtensionUpdate(requireComponents.core.store, args.addon.id) },
                onCancelUpdateClick = { cancelPendingExtensionUpdate(requireComponents.core.store, args.addon.id) },
            )
        }
    }

    @Composable
    private fun observePendingUpdate(): State<PendingExtensionUpdate?> =
        requireComponents.core.store.observeAsComposableState { it.pendingExtensionUpdates[args.addon.id] }

    private fun learnMoreUrl(context: Context) =
        SupportUtils.getSumoURLForTopic(
            context,
            SupportUtils.SumoTopic.MANAGE_OPTIONAL_EXTENSION_PERMISSIONS,
        )

    private fun addOptionalPermissions(
        addPermissionsRequest: AddonPermissionsUpdateRequest,
        onUpdatePermissionsSuccess: (Addon) -> Unit,
    ) {
        requireContext()
            .components
            .addonManager
            .addOptionalPermission(
                args.addon,
                addPermissionsRequest.optionalPermissions,
                addPermissionsRequest.originPermissions,
                addPermissionsRequest.dataCollectionPermissions,
                onSuccess = {
                    onUpdatePermissionsSuccess(it)
                },
                onError = {
                    // No-Op
                },
            )
    }

    private fun removeOptionalPermission(
        removePermissionsRequest: AddonPermissionsUpdateRequest,
        onUpdatePermissionsSuccess: (Addon) -> Unit,
    ) {
        requireContext()
            .components
            .addonManager
            .removeOptionalPermission(
                args.addon,
                removePermissionsRequest.optionalPermissions,
                removePermissionsRequest.originPermissions,
                removePermissionsRequest.dataCollectionPermissions,
                onSuccess = {
                    onUpdatePermissionsSuccess(it)
                },
                onError = {
                    // No-Op
                },
            )
    }

    private fun openWebsite(addonSiteUrl: String) {
        findNavController().openToBrowser()
        requireComponents.useCases.fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = addonSiteUrl,
            newTab = true,
        )
    }

    private fun <T : List<Addon.Permission>> T.getAllSitesPermissionsList(): List<Addon.Permission> {
        return this.mapNotNull {
            if (it.isAllURLsPermission()) {
                it
            } else {
                null
            }
        }
    }

    override fun onResume() {
        super.onResume()
        context?.let {
            showToolbar(title = args.addon.translateName(it))
        }
    }
}
