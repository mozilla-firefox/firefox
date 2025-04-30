/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.webcompat.di

import kotlinx.coroutines.CoroutineScope
import kotlinx.serialization.json.Json
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.service.nimbus.NimbusApi
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.webcompat.WebCompatState
import org.mozilla.fenix.webcompat.middleware.DefaultNimbusExperimentsProvider
import org.mozilla.fenix.webcompat.middleware.DefaultWebCompatReporterRetrievalService
import org.mozilla.fenix.webcompat.middleware.WebCompatInfoDeserializer
import org.mozilla.fenix.webcompat.middleware.WebCompatReporterNavigationMiddleware
import org.mozilla.fenix.webcompat.middleware.WebCompatReporterStorageMiddleware
import org.mozilla.fenix.webcompat.middleware.WebCompatReporterSubmissionMiddleware
import org.mozilla.fenix.webcompat.middleware.WebCompatReporterTelemetryMiddleware

/**
 * Provides middleware for the WebCompat Reporter store.
 */
object WebCompatReporterMiddlewareProvider {

    /**
     * Provides middleware for the WebCompat Reporter.
     *
     * @param browserStore [BrowserStore] used to access [BrowserState].
     * @param appStore [AppStore] used to persist [WebCompatState].
     * @param scope The [CoroutineScope] used for launching coroutines.
     * @param nimbusApi A [NimbusApi] with which to get active/enrolled experiments.
     */
    fun provideMiddleware(
        browserStore: BrowserStore,
        appStore: AppStore,
        scope: CoroutineScope,
        nimbusApi: NimbusApi,
    ) = listOf(
        provideStorageMiddleware(appStore),
        provideSubmissionMiddleware(
            appStore = appStore,
            browserStore = browserStore,
            webCompatInfoDeserializer = provideWebCompatInfoDeserializer(),
            scope = scope,
            nimbusApi = nimbusApi,
        ),
        provideNavigationMiddleware(),
        provideTelemetryMiddleware(),
    )

    private fun provideStorageMiddleware(
        appStore: AppStore,
    ) = WebCompatReporterStorageMiddleware(
        appStore = appStore,
    )

    private fun provideSubmissionMiddleware(
        appStore: AppStore,
        browserStore: BrowserStore,
        webCompatInfoDeserializer: WebCompatInfoDeserializer,
        scope: CoroutineScope,
        nimbusApi: NimbusApi,
    ) = WebCompatReporterSubmissionMiddleware(
        appStore = appStore,
        browserStore = browserStore,
        webCompatReporterRetrievalService = DefaultWebCompatReporterRetrievalService(
            browserStore = browserStore,
            webCompatInfoDeserializer = webCompatInfoDeserializer,
            nimbusExperimentsProvider = DefaultNimbusExperimentsProvider(nimbusApi),
        ),
        scope = scope,
    )

    private fun provideNavigationMiddleware() =
        WebCompatReporterNavigationMiddleware()

    private fun provideTelemetryMiddleware() =
        WebCompatReporterTelemetryMiddleware()

    private val json by lazy {
        Json {
            ignoreUnknownKeys = true
            useAlternativeNames = false
        }
    }

    internal fun provideWebCompatInfoDeserializer() = WebCompatInfoDeserializer(json = json)
}
