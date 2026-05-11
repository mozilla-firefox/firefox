/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.addons.ui

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.InfoCard
import mozilla.components.compose.base.InfoType
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.Addon.Companion.isAllURLsPermission
import mozilla.components.feature.addons.Addon.Permission
import org.mozilla.fenix.R
import org.mozilla.fenix.addons.AddonPermissionsUpdateRequest
import org.mozilla.fenix.compose.list.SwitchListItem
import org.mozilla.fenix.compose.list.TextListItem
import org.mozilla.fenix.compose.settings.SettingsSectionHeader
import org.mozilla.fenix.theme.FirefoxTheme
import mozilla.components.feature.addons.R as addonsR

/**
 * The permissions screen for an addon which allows a user to edit the optional permissions.
 */
@Composable
@Suppress("LongParameterList", "LongMethod", "CognitiveComplexMethod")
fun AddonPermissionsScreen(
    permissions: List<String>,
    optionalPermissions: List<Addon.LocalizedPermission>,
    originPermissions: List<Addon.LocalizedPermission>,
    isAllSitesSwitchVisible: Boolean,
    isAllSitesEnabled: Boolean,
    onAddOptionalPermissions: (AddonPermissionsUpdateRequest) -> Unit,
    onRemoveOptionalPermissions: (AddonPermissionsUpdateRequest) -> Unit,
    onAddAllSitesPermissions: () -> Unit,
    onRemoveAllSitesPermissions: () -> Unit,
    onLearnMoreClick: (String) -> Unit,
    learnMoreUrl: String,
    modifier: Modifier = Modifier,
    requiredDataCollectionPermissions: List<String> = emptyList(),
    hasNoneDataCollection: Boolean = false,
    optionalDataCollectionPermissions: List<Addon.LocalizedPermission> = emptyList(),
) {
    Surface {
        val hasNoPermission = permissions.isEmpty() &&
                optionalPermissions.isEmpty() &&
                originPermissions.isEmpty() &&
                requiredDataCollectionPermissions.isEmpty() &&
                optionalDataCollectionPermissions.isEmpty() && !hasNoneDataCollection

        // Early return with a "no permissions required" message when the add-on doesn't have any permission that we can
        // list in this screen.
        if (hasNoPermission) {
            return@Surface LazyColumn(modifier = Modifier.padding(vertical = 8.dp)) {
                item {
                    TextListItem(
                        label = stringResource(R.string.addons_does_not_require_permissions),
                        maxLabelLines = Int.MAX_VALUE,
                    )
                }

                item {
                    HorizontalDivider()
                }

                item {
                    LearnMoreItem(learnMoreUrl, onLearnMoreClick)
                }
            }
        }

        LazyColumn(modifier = modifier.padding(vertical = 8.dp)) {
            if (permissions.isNotEmpty()) {
                // Required Permissions Header
                item {
                    SectionHeader(
                        label = stringResource(R.string.addons_permissions_heading_required_permissions),
                    )
                }

                // Required Permissions
                items(items = permissions) { permission ->
                    TextListItem(
                        label = permission,
                        maxLabelLines = Int.MAX_VALUE,
                    )
                }

                item {
                    HorizontalDivider()
                }
            }

            // Optional Section
            if (optionalPermissions.isNotEmpty() || originPermissions.isNotEmpty()) {
                // Optional Section Header
                item {
                    SectionHeader(
                        label = stringResource(id = R.string.addons_permissions_heading_optional_permissions),
                    )
                }

                // All Sites Toggle if needed
                if (isAllSitesSwitchVisible) {
                    item {
                        AllSitesToggle(
                            enabledAllowForAll = isAllSitesEnabled,
                            onAddAllSitesPermissions,
                            onRemoveAllSitesPermissions,
                        )
                    }
                }

                // Optional Permissions
                items(
                    items = optionalPermissions,
                    key = {
                        it.localizedName
                    },
                ) { optionalPermission ->

                    // Hide <all_urls> permission and use the all_urls toggle instead
                    if (!optionalPermission.permission.isAllURLsPermission()) {
                        OptionalPermissionSwitch(
                            modifier = Modifier
                                .padding(horizontal = 16.dp, vertical = 6.dp),
                            localizedPermission = optionalPermission,
                            type = OptionalPermissionType.PERMISSION,
                            addOptionalPermission = onAddOptionalPermissions,
                            removeOptionalPermission = onRemoveOptionalPermissions,
                        )
                    }
                }

                // Origin Permissions
                items(
                    items = originPermissions,
                    key = {
                        it.permission.name
                    },
                ) { originPermission ->
                    // Hide host permissions when a user has enabled all_urls permission.
                    // Also hide permissions that match all_urls because they are replaced by the all_urls toggle.
                    if (!originPermission.permission.isAllURLsPermission()) {
                        OptionalPermissionSwitch(
                            modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
                            localizedPermission = originPermission,
                            type = OptionalPermissionType.ORIGIN,
                            isEnabled = !isAllSitesEnabled,
                            addOptionalPermission = onAddOptionalPermissions,
                            removeOptionalPermission = onRemoveOptionalPermissions,
                        )
                    }
                }

                item {
                    HorizontalDivider()
                }
            }

            if (requiredDataCollectionPermissions.isNotEmpty() || hasNoneDataCollection) {
                // Optional Section Header
                item {
                    SectionHeader(
                        label = stringResource(id = R.string.addons_permissions_heading_required_data_collection),
                    )
                }

                item {
                    DataCollectionPermission(
                        hasNoneDataCollection,
                        requiredDataCollectionPermissions,
                    )
                }

                item {
                    HorizontalDivider()
                }
            }

            if (optionalDataCollectionPermissions.isNotEmpty()) {
                item {
                    SectionHeader(
                        label = stringResource(id = R.string.addons_permissions_heading_optional_data_collection),
                    )
                }

                items(
                    items = optionalDataCollectionPermissions,
                    key = { it.localizedName },
                ) { optionalPermission ->
                    OptionalPermissionSwitch(
                        modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
                        localizedPermission = optionalPermission,
                        type = OptionalPermissionType.DATA_COLLECTION,
                        addOptionalPermission = onAddOptionalPermissions,
                        removeOptionalPermission = onRemoveOptionalPermissions,
                    )
                }

                item {
                    HorizontalDivider()
                }
            }

            item {
                LearnMoreItem(learnMoreUrl, onLearnMoreClick)
            }
        }
    }
}

@Composable
private fun DataCollectionPermission(
    hasNoneDataCollection: Boolean,
    requiredDataCollectionPermissions: List<String>,
) {
    TextListItem(
        label = if (hasNoneDataCollection) {
            stringResource(id = R.string.addons_permissions_none_required_data_collection_description)
        } else {
            stringResource(
                id = R.string.addons_permissions_required_data_collection_description_2,
                Addon.formatLocalizedDataCollectionPermissions(requiredDataCollectionPermissions),
            )
        },
        maxLabelLines = Int.MAX_VALUE,
        modifier = Modifier.padding(vertical = 8.dp),
    )
}

/**
 * Toggle that handles requesting adding or removing any all_urls permissions.
 * This includes wildcard urls that are considered all_urls permissions such as
 * http://&#42;/, https:///&#42;/, and file:///&#42;//&#42;
 */
@Composable
private fun AllSitesToggle(
    enabledAllowForAll: Boolean,
    onAddAllSitesPermissions: () -> Unit,
    onRemoveAllSitesPermissions: () -> Unit,
) {
    SwitchListItem(
        label = stringResource(R.string.addons_permissions_allow_for_all_sites),
        checked = enabledAllowForAll,
        modifier = Modifier
            .padding(horizontal = 16.dp, vertical = 6.dp),
        maxLabelLines = Int.MAX_VALUE,
        description = stringResource(R.string.addons_permissions_allow_for_all_sites_subtitle),
        maxDescriptionLines = Int.MAX_VALUE,
        showSwitchAfter = true,
    ) { enabled ->
        if (enabled) {
            onAddAllSitesPermissions()
        } else {
            onRemoveAllSitesPermissions()
        }
    }
}

@Composable
private fun SectionHeader(label: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp),
    ) {
        SettingsSectionHeader(text = label)
    }
}

@Composable
private fun LearnMoreItem(learnMoreUrl: String, onLearnMoreClick: (String) -> Unit) {
    val learnMoreText = stringResource(addonsR.string.mozac_feature_addons_learn_more)
    val learnMoreState = LinkTextState(
        text = learnMoreText,
        url = learnMoreUrl,
        onClick = {
            onLearnMoreClick.invoke(it)
        },
    )

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(16.dp),
    ) {
        LinkText(
            text = learnMoreText,
            linkTextStates = listOf(learnMoreState),
        )
    }
}

/**
 * The type of optional permission to render in `OptionalPermissionSwitch`.
 */
enum class OptionalPermissionType {
    PERMISSION,
    ORIGIN,
    DATA_COLLECTION,
}

@Composable
private fun OptionalPermissionSwitch(
    modifier: Modifier,
    localizedPermission: Addon.LocalizedPermission,
    type: OptionalPermissionType,
    isEnabled: Boolean = true,
    addOptionalPermission: (AddonPermissionsUpdateRequest) -> Unit,
    removeOptionalPermission: (AddonPermissionsUpdateRequest) -> Unit,
) {
    SwitchListItem(
        label = localizedPermission.localizedName,
        checked = localizedPermission.permission.granted,
        modifier = modifier,
        enabled = isEnabled,
        showSwitchAfter = true,
    ) { enabled ->
        val request = AddonPermissionsUpdateRequest(
            optionalPermissions = when (type) {
                OptionalPermissionType.PERMISSION -> listOf(localizedPermission.permission.name)
                else -> emptyList()
            },
            originPermissions = when (type) {
                OptionalPermissionType.ORIGIN -> listOf(localizedPermission.permission.name)
                else -> emptyList()
            },
            dataCollectionPermissions = when (type) {
                OptionalPermissionType.DATA_COLLECTION -> listOf(localizedPermission.permission.name)
                else -> emptyList()
            },
        )

        if (enabled) {
            addOptionalPermission(request)
        } else {
            removeOptionalPermission(request)
        }
    }

    if (localizedPermission.permission.name == "userScripts") {
        InfoCard(
            type = InfoType.Warning,
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 6.dp),
            description = stringResource(addonsR.string.mozac_feature_addons_permissions_user_scripts_extra_warning),
        )
    }
}

@Composable
@PreviewLightDark
private fun AddonPermissionsScreenPreview() {
    val permissions: List<String> = listOf("Permission required 1", "Permission required 2")
    val optionalPermissions: List<Addon.LocalizedPermission> = listOf(
        Addon.LocalizedPermission(
            "Optional Permission 1",
            Permission("Optional permission 1", false),
        ),
    )
    val originPermissions: List<Addon.LocalizedPermission> = listOf(
        Addon.LocalizedPermission(
            "https://required.website",
            Permission("https://required.website", true),
        ),
        Addon.LocalizedPermission(
            "https://optional-suggested.website...",
            Permission("https://optional-suggested.website...", false),
        ),
        Addon.LocalizedPermission(
            "https://user-added.website.com",
            Permission("https://user-added.website.com", false),
        ),
    )
    val requiredDataCollectionPermissions: List<String> = listOf("location", "browsing activity")
    val optionalDataCollectionPermissions: List<Addon.LocalizedPermission> = listOf(
        Addon.LocalizedPermission(
            "Share location with extension developer",
            Permission("location", false),
        ),
        Addon.LocalizedPermission(
            "Share technical and interaction data with extension developer",
            Permission("technicalAndInteraction", false),
        ),
    )

    FirefoxTheme {
        AddonPermissionsScreen(
            permissions = permissions,
            optionalPermissions = optionalPermissions,
            originPermissions = originPermissions,
            requiredDataCollectionPermissions = requiredDataCollectionPermissions,
            optionalDataCollectionPermissions = optionalDataCollectionPermissions,
            isAllSitesSwitchVisible = true,
            isAllSitesEnabled = false,
            onAddOptionalPermissions = { _ -> },
            onRemoveOptionalPermissions = { _ -> },
            onAddAllSitesPermissions = {},
            onRemoveAllSitesPermissions = {},
            onLearnMoreClick = { _ -> },
            learnMoreUrl = "",
        )
    }
}

@Composable
@PreviewLightDark
private fun AddonPermissionsScreenWithPermissionsPreview() {
    FirefoxTheme {
        AddonPermissionsScreen(
            permissions = emptyList(),
            optionalPermissions = emptyList(),
            originPermissions = emptyList(),
            isAllSitesSwitchVisible = true,
            isAllSitesEnabled = false,
            onAddOptionalPermissions = { _ -> },
            onRemoveOptionalPermissions = { _ -> },
            onAddAllSitesPermissions = {},
            onRemoveAllSitesPermissions = {},
            onLearnMoreClick = { _ -> },
            learnMoreUrl = "",
        )
    }
}

@Composable
@PreviewLightDark
private fun AddonPermissionsScreenWithUserScriptsPermissionsPreview() {
    val optionalPermissions: List<Addon.LocalizedPermission> = listOf(
        Addon.LocalizedPermission(
            "Allow unverified third-party scripts to access your data",
            Permission("userScripts", false),
        ),
    )

    FirefoxTheme {
        AddonPermissionsScreen(
            permissions = emptyList(),
            optionalPermissions = optionalPermissions,
            originPermissions = emptyList(),
            isAllSitesSwitchVisible = true,
            isAllSitesEnabled = false,
            onAddOptionalPermissions = { _ -> },
            onRemoveOptionalPermissions = { _ -> },
            onAddAllSitesPermissions = {},
            onRemoveAllSitesPermissions = {},
            onLearnMoreClick = { _ -> },
            learnMoreUrl = "",
        )
    }
}
