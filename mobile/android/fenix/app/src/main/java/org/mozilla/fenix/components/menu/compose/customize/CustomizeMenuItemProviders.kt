/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose.customize

import androidx.fragment.app.Fragment
import androidx.lifecycle.coroutineScope
import mozilla.components.feature.automotive.isAndroidAutomotiveAvailable
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.menu.MenuItemProvidersFactory
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.ext.openSetDefaultBrowserOption
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.translations.TranslationsEnabledSettings

/** Build the [MenuItemProvidersFactory] providing the items shown in the screens customizing the menu. */
internal fun Fragment.buildCustomizeMenuItemProvidersFactory(): MenuItemProvidersFactory {
    val components = requireComponents
    val context = requireContext()
    val activity = requireActivity()

    return MenuItemProvidersFactory(
        context = context,
        accessPoint = MenuAccessPoint.Browser,
        target = MenuTarget.CustomizeMenu,
        browserStore = components.core.store,
        appStore = components.appStore,
        ipProtectionStore = components.ipProtection.store,
        syncStore = components.backgroundServices.syncStore,
        httpClient = components.core.client,
        historyStorage = components.core.historyStorage,
        bookmarksStorage = components.core.bookmarksStorage,
        pinnedSiteStorage = components.core.pinnedSiteStorage,
        permissionStorage = components.core.permissionStorage,
        useCases = components.useCases,
        addonManager = components.addonManager,
        engine = components.core.engine,
        settings = components.settings,
        summarizeFeatureSettings = components.core.summarizeFeatureSettings,
        summarizeEligibilityChecker = components.core.summarizationEligibilityChecker,
        translationsSettings = TranslationsEnabledSettings.dataStore(context),
        isAndroidAutomotiveAvailable = context.isAndroidAutomotiveAvailable(),
        quitApplicationDelegate = activity::finishAndRemoveTask,
        setAsDefaultBrowserDelegate = activity::openSetDefaultBrowserOption,
        tabDataCoordinator = components.tabDataCoordinator,
        menuViewScope = viewLifecycleOwner.lifecycle.coroutineScope,
        applicationScope = components.applicationScope,
        browsingModeManager = (activity as HomeActivity).browsingModeManager,
        tabGroupFeatureConfiguration = components.core.tabGroupDiscoveryFeatureConfiguration,
    )
}
