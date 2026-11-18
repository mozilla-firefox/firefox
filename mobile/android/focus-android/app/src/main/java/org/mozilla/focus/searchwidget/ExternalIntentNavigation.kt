/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.searchwidget

import android.content.Context
import android.os.Bundle
import androidx.annotation.VisibleForTesting
import mozilla.components.browser.state.search.SearchAccessPoint
import mozilla.components.browser.state.state.SessionState
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags
import mozilla.components.feature.search.widget.BaseVoiceSearchActivity
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.focus.GleanMetrics.SearchWidget
import org.mozilla.focus.activity.IntentReceiverActivity
import org.mozilla.focus.ext.components
import org.mozilla.focus.ext.settings
import org.mozilla.focus.perf.Performance
import org.mozilla.focus.state.AppAction
import org.mozilla.focus.utils.SearchUtils

/** Handles all actions from outside the app that starts it. */
object ExternalIntentNavigation {

    /**
     * Handle all the navigation from search widget, app icon, lockscreen and custom tab.
     *
     * @param bundle External Intent [Bundle] with data based on which destination will be computed.
     * @param context Android [Context] used for system interactions or accessing dependencies.
     */
    fun handleAppNavigation(
        bundle: Bundle?,
        context: Context,
    ) {
        if (handleWidgetVoiceSearch(bundle, context)) return

        if (handleWidgetTextSearch(bundle, context)) return

        if (handleBrowserTabAlreadyOpen(context)) return

        handleAppOpened(bundle, context)
    }

    /**
     * Handle the app being opened with no specified destination. This can show the onboarding or the app's home screen.
     */
    @VisibleForTesting
    internal fun handleAppOpened(
        bundle: Bundle?,
        context: Context,
    ) {
        if (context.settings.isFirstRun && !Performance.processIntentIfPerformanceTest(bundle, context)) {
            context.components.appStore.dispatch(AppAction.ShowFirstRun)
        }
    }

    /**
     * Try navigating to the currently selected tab.
     *
     * @return Whether or not the app navigated to the currently selected tab.
     */
    @VisibleForTesting
    internal fun handleBrowserTabAlreadyOpen(context: Context): Boolean {
        val tabId = context.components.store.state.selectedTabId

        return when (tabId != null) {
            true -> {
                context.components.appStore.dispatch(AppAction.OpenTab(tabId))
                true
            }
            else -> false
        }
    }

    /**
     * Try navigating to search by text.
     *
     * @return Whether or not a widget interaction was detected and the app opened the search screen for it.
     */
    @VisibleForTesting
    internal fun handleWidgetTextSearch(bundle: Bundle?, context: Context): Boolean {
        val searchWidgetIntent = bundle?.getBoolean(IntentReceiverActivity.SEARCH_WIDGET_EXTRA, false)

        return when (searchWidgetIntent == true) {
            true -> {
                SearchWidget.newTabButton.record(NoExtras())
                context.components.appStore.dispatch(AppAction.ShowHomeScreen(SearchAccessPoint.WIDGET))
                true
            }
            else -> false
        }
    }

    /**
     * Try opening a new tab for the user voice search.
     *
     * @return Whether or not a voice search was identified and the app opened a new tab for it.
     */
    @VisibleForTesting
    internal fun handleWidgetVoiceSearch(bundle: Bundle?, context: Context): Boolean {
        val voiceSearchText = bundle?.getString(BaseVoiceSearchActivity.SPEECH_PROCESSING)

        return when (!voiceSearchText.isNullOrEmpty()) {
            true -> {
                val tabId =
                    context.components.tabsUseCases.addTab(
                        url =
                            SearchUtils.createSearchUrl(
                                context = context,
                                text = voiceSearchText,
                                accessPoint = SearchAccessPoint.WIDGET,
                            ),
                        source = SessionState.Source.External.ActionSend(null),
                        searchTerms = voiceSearchText,
                        selectTab = true,
                        private = true,
                        flags = LoadUrlFlags.external(),
                    )
                context.components.appStore.dispatch(AppAction.OpenTab(tabId))
                true
            }
            else -> false
        }
    }
}
