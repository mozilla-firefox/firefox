/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites.controller

import android.annotation.SuppressLint
import android.app.Activity
import android.content.res.ColorStateList
import android.view.LayoutInflater
import android.widget.EditText
import androidx.annotation.VisibleForTesting
import androidx.appcompat.app.AlertDialog
import androidx.core.widget.addTextChangedListener
import androidx.navigation.NavController
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.textfield.TextInputEditText
import com.google.android.material.textfield.TextInputLayout
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.availableSearchEngines
import mozilla.components.browser.state.state.searchEngines
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.feature.top.sites.TopSitesUseCases
import mozilla.components.service.mars.MozAdsUseCases
import mozilla.components.support.ktx.android.content.getColorFromAttr
import mozilla.components.support.ktx.android.view.showKeyboard
import mozilla.components.support.ktx.kotlin.isUrl
import mozilla.components.support.ktx.kotlin.toNormalizedUrl
import mozilla.components.ui.widgets.withCenterAlignedButtons
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.GleanMetrics.ShortcutsLibrary
import org.mozilla.fenix.GleanMetrics.TopSites
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.home.mars.MARSUseCases
import org.mozilla.fenix.home.topsites.ShortcutsFragmentDirections
import org.mozilla.fenix.home.topsites.interactor.TopSiteInteractor
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.utils.Settings
import java.lang.ref.WeakReference
import androidx.appcompat.R as appcompatR
import mozilla.components.ui.icons.R as iconsR

/**
 * An interface that handles the view manipulation of the top sites triggered by the Interactor.
 */
interface TopSiteController {
    /**
     * @see [TopSiteInteractor.onOpenInPrivateTabClicked]
     */
    fun handleOpenInPrivateTabClicked(topSite: TopSite)

    /**
     * @see [TopSiteInteractor.onEditTopSiteClicked]
     */
    fun handleEditTopSiteClicked(topSite: TopSite)

    /**
     * @see [TopSiteInteractor.onRemoveTopSiteClicked]
     */
    fun handleRemoveTopSiteClicked(topSite: TopSite)

    /**
     * @see [TopSiteInteractor.onSelectTopSite]
     */
    fun handleSelectTopSite(topSite: TopSite, position: Int)

    /**
     * @see [TopSiteInteractor.onTopSiteImpression]
     */
    fun handleTopSiteImpression(topSite: TopSite.Provided, position: Int)

    /**
     * @see [TopSiteInteractor.onSettingsClicked]
     */
    fun handleTopSiteSettingsClicked()

    /**
     * @see [TopSiteInteractor.onSponsorPrivacyClicked]
     */
    fun handleSponsorPrivacyClicked()

    /**
     * @see [TopSiteInteractor.onTopSiteLongClicked]
     */
    fun handleTopSiteLongClicked(topSite: TopSite)

    /**
     * @see [TopSiteInteractor.onShowAllTopSitesClicked]
     */
    fun handleShowAllTopSitesClicked()

    /**
     * @see [TopSiteInteractor.onShortcutsLibraryViewed]
     */
    fun handleShortcutsLibraryViewed()
}

/**
 * The default implementation of [TopSiteController].
 */
@Suppress("LongParameterList")
class DefaultTopSiteController(
    private val activityRef: WeakReference<Activity>,
    private val navControllerRef: WeakReference<NavController>,
    private val store: BrowserStore,
    private val settings: Settings,
    private val addTabUseCase: TabsUseCases.AddNewTabUseCase,
    private val selectTabUseCase: TabsUseCases.SelectTabUseCase,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val topSitesUseCases: TopSitesUseCases,
    private val marsUseCases: MARSUseCases,
    private val mozAdsUseCases: MozAdsUseCases,
    private val viewLifecycleScope: CoroutineScope,
) : TopSiteController {

    private val activity: Activity
        get() = requireNotNull(activityRef.get())

    private val navController: NavController
        get() = requireNotNull(navControllerRef.get())

    override fun handleOpenInPrivateTabClicked(topSite: TopSite) {
        if (topSite is TopSite.Provided) {
            TopSites.openContileInPrivateTab.record(NoExtras())
        } else {
            TopSites.openInPrivateTab.record(NoExtras())
        }

        activity.components.appStore.dispatch(
            AppAction.BrowsingModeManagerModeChanged(BrowsingMode.Private),
        )

        if (navController.currentDestination?.id == R.id.shortcutsFragment) {
            navController.navigate(ShortcutsFragmentDirections.actionShortcutsFragmentToBrowserFragment())
        } else {
            navController.navigate(R.id.browserFragment)
        }

        fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = topSite.url,
            newTab = true,
            private = true,
        )
    }

    @SuppressLint("InflateParams")
    override fun handleEditTopSiteClicked(topSite: TopSite) {
        activity.let {
            val customLayout =
                LayoutInflater.from(it).inflate(R.layout.top_sites_edit_dialog, null)
            val titleEditText = customLayout.findViewById<EditText>(R.id.top_site_title)
            val urlEditText = customLayout.findViewById<TextInputEditText>(R.id.top_site_url)
            val urlLayout = customLayout.findViewById<TextInputLayout>(R.id.top_site_url_layout)

            titleEditText.setText(topSite.title)
            urlEditText.setText(topSite.url)

            MaterialAlertDialogBuilder(it).apply {
                setTitle(R.string.top_sites_edit_dialog_title)
                setView(customLayout)
                setPositiveButton(R.string.top_sites_edit_dialog_save) { _, _ -> }
                setNegativeButton(R.string.top_sites_rename_dialog_cancel) { dialog, _ ->
                    dialog.cancel()
                }
            }.show().withCenterAlignedButtons().also { dialog ->
                dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                    val urlText = urlEditText.text.toString()

                    if (urlText.isUrl()) {
                        viewLifecycleScope.launch {
                            updateTopSite(
                                topSite = topSite,
                                title = titleEditText.text.toString(),
                                url = urlText.toNormalizedUrl(),
                            )
                        }

                        dialog.dismiss()
                    } else {
                        val criticalColor = ColorStateList.valueOf(
                            activity.getColorFromAttr(appcompatR.attr.colorError),
                        )
                        urlLayout.setErrorIconTintList(criticalColor)
                        urlLayout.setErrorTextColor(criticalColor)
                        urlLayout.boxStrokeErrorColor = criticalColor

                        urlLayout.error =
                            activity.resources.getString(R.string.top_sites_edit_dialog_url_error)

                        urlLayout.setErrorIconDrawable(iconsR.drawable.mozac_ic_warning_fill_24)
                    }
                }

                urlEditText.addTextChangedListener {
                    urlLayout.error = null
                    urlLayout.errorIconDrawable = null
                }

                titleEditText.setSelection(0, titleEditText.text.length)
                titleEditText.showKeyboard()
            }
        }
    }

    @VisibleForTesting
    internal suspend fun updateTopSite(topSite: TopSite, title: String, url: String) {
        if (topSite is TopSite.Frecent) {
            topSitesUseCases.addPinnedSites(
                title = title,
                url = url,
            )
        } else {
            topSitesUseCases.updateTopSites(
                topSite = topSite,
                title = title,
                url = url,
            )
        }
    }

    override fun handleRemoveTopSiteClicked(topSite: TopSite) {
        TopSites.remove.record(NoExtras())

        when (topSite.url) {
            SupportUtils.GOOGLE_URL -> TopSites.googleTopSiteRemoved.record(NoExtras())
        }

        viewLifecycleScope.launch {
            with(activity.components.useCases.topSitesUseCase) {
                removeTopSites(topSite)
            }
        }
    }

    override fun handleSelectTopSite(topSite: TopSite, position: Int) {
        when (topSite) {
            is TopSite.Default -> TopSites.openDefault.record(NoExtras())
            is TopSite.Frecent -> TopSites.openFrecency.record(NoExtras())
            is TopSite.Pinned -> TopSites.openPinned.record(NoExtras())
            is TopSite.Provided -> {
                if (settings.enableMozillaAdsClient) {
                    sendMozAdsClickInteraction(clickUrl = topSite.clickUrl)
                } else {
                    sendMarsTopSiteCallback(topSite.clickUrl)
                }

                TopSites.openContileTopSite.record(NoExtras()).also {
                    recordTopSitesClickTelemetry(topSite, position)
                }
            }
        }

        when (topSite.url) {
            SupportUtils.GOOGLE_URL -> TopSites.openGoogleSearchAttribution.record(NoExtras())
        }

        val availableEngines: List<SearchEngine> = getAvailableSearchEngines()
        val searchAccessPoint = MetricsUtils.Source.TOPSITE

        availableEngines.firstOrNull { engine ->
            engine.resultUrls.firstOrNull { it.contains(topSite.url) } != null
        }?.let { searchEngine ->
            MetricsUtils.recordSearchMetrics(
                searchEngine,
                searchEngine == store.state.search.selectedOrDefaultSearchEngine,
                searchAccessPoint,
                activity.components.nimbus.events,
            )
        }

        if (settings.enableHomepageAsNewTab) {
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = appendSearchAttributionToUrlIfNeeded(topSite.url),
                newTab = false,
                private = false,
            )
        } else {
            val existingTabForUrl = when (topSite) {
                is TopSite.Frecent, is TopSite.Pinned -> {
                    store.state.tabs.firstOrNull { topSite.url == it.content.url }
                }

                else -> null
            }

            if (existingTabForUrl == null) {
                TopSites.openInNewTab.record(NoExtras())

                addTabUseCase.invoke(
                    url = appendSearchAttributionToUrlIfNeeded(topSite.url),
                    selectTab = true,
                    startLoading = true,
                )
            } else {
                selectTabUseCase.invoke(existingTabForUrl.id)
            }
        }

        if (navController.currentDestination?.id == R.id.shortcutsFragment) {
            navController.navigate(ShortcutsFragmentDirections.actionShortcutsFragmentToBrowserFragment())
        } else {
            navController.navigate(R.id.browserFragment)
        }
    }

    @VisibleForTesting
    internal fun recordTopSitesClickTelemetry(topSite: TopSite.Provided, position: Int) {
        TopSites.contileClick.record(
            TopSites.ContileClickExtra(
                position = position + 1,
                source = "newtab",
            ),
        )

        topSite.id?.let { TopSites.contileTileId.set(it) }
        topSite.title?.let { TopSites.contileAdvertiser.set(it.lowercase()) }

        Pings.topsitesImpression.submit()
    }

    override fun handleTopSiteImpression(topSite: TopSite.Provided, position: Int) {
        if (settings.enableMozillaAdsClient) {
            sendMozAdsImpressionInteraction(impressionUrl = topSite.impressionUrl)
        } else {
            sendMarsTopSiteCallback(topSite.impressionUrl)
        }

        TopSites.contileImpression.record(
            TopSites.ContileImpressionExtra(
                position = position + 1,
                source = "newtab",
            ),
        )

        topSite.id?.let { TopSites.contileTileId.set(it) }
        topSite.title?.let { TopSites.contileAdvertiser.set(it.lowercase()) }

        Pings.topsitesImpression.submit()
    }

    private fun sendMarsTopSiteCallback(url: String) {
        viewLifecycleScope.launch {
            marsUseCases.recordInteraction(url)
        }
    }

    private fun sendMozAdsClickInteraction(clickUrl: String) {
        viewLifecycleScope.launch {
            mozAdsUseCases.recordClickInteraction(clickUrl = clickUrl)
        }
    }

    private fun sendMozAdsImpressionInteraction(impressionUrl: String) {
        viewLifecycleScope.launch {
            mozAdsUseCases.recordImpressionInteraction(impressionUrl = impressionUrl)
        }
    }

    override fun handleTopSiteSettingsClicked() {
        TopSites.contileSettings.record(NoExtras())
        navController.navigate(R.id.homeSettingsFragment)
    }

    override fun handleSponsorPrivacyClicked() {
        TopSites.contileSponsorsAndPrivacy.record(NoExtras())

        if (navController.currentDestination?.id == R.id.shortcutsFragment) {
            navController.navigate(ShortcutsFragmentDirections.actionShortcutsFragmentToBrowserFragment())
        } else {
            navController.navigate(R.id.browserFragment)
        }

        fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.SPONSOR_PRIVACY),
            newTab = true,
            private = false,
        )
    }

    override fun handleTopSiteLongClicked(topSite: TopSite) {
        TopSites.longPress.record(TopSites.LongPressExtra(topSite.type))
    }

    override fun handleShowAllTopSitesClicked() {
        navController.nav(
            R.id.homeFragment,
            HomeFragmentDirections.actionHomeFragmentToShortcutsFragment(),
        )
    }

    override fun handleShortcutsLibraryViewed() {
        ShortcutsLibrary.viewed.record(NoExtras())
    }

    /**
     * Append a search attribution query to any provided search engine URL based on the
     * user's current region.
     */
    private fun appendSearchAttributionToUrlIfNeeded(url: String): String {
        if (url == SupportUtils.GOOGLE_URL) {
            store.state.search.region?.let { region ->
                return when (region.current) {
                    "US" -> SupportUtils.GOOGLE_US_URL
                    else -> SupportUtils.GOOGLE_XX_URL
                }
            }
        }

        return url
    }

    @VisibleForTesting
    internal fun getAvailableSearchEngines() =
        activity.components.core.store.state.search.searchEngines +
            activity.components.core.store.state.search.availableSearchEngines
}
