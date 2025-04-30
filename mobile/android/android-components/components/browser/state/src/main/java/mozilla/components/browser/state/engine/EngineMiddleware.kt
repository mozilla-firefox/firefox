/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.engine

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.MainScope
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.EngineAction
import mozilla.components.browser.state.engine.middleware.CrashMiddleware
import mozilla.components.browser.state.engine.middleware.CreateEngineSessionMiddleware
import mozilla.components.browser.state.engine.middleware.EngineDelegateMiddleware
import mozilla.components.browser.state.engine.middleware.ExtensionsProcessMiddleware
import mozilla.components.browser.state.engine.middleware.LinkingMiddleware
import mozilla.components.browser.state.engine.middleware.PdfStateMiddleware
import mozilla.components.browser.state.engine.middleware.SuspendMiddleware
import mozilla.components.browser.state.engine.middleware.TabsRemovedMiddleware
import mozilla.components.browser.state.engine.middleware.TranslationsMiddleware
import mozilla.components.browser.state.engine.middleware.TrimMemoryMiddleware
import mozilla.components.browser.state.engine.middleware.WebExtensionMiddleware
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.lib.state.Middleware

/**
 * Helper for creating a list of [Middleware] instances for supporting all [EngineAction]s.
 */
object EngineMiddleware {
    /**
     * Creates a list of [Middleware] to be installed on a [BrowserStore] in order to support all
     * [EngineAction]s.
     *
     * @param trimMemoryAutomatically Whether a middleware should listen to LowMemoryAction and
     * automatically trim memory by suspending tabs.
     */
    fun create(
        engine: Engine,
        scope: CoroutineScope = MainScope(),
        trimMemoryAutomatically: Boolean = true,
        automaticallyInitializeTranslations: Boolean = true,
    ): List<Middleware<BrowserState, BrowserAction>> {
        return listOf(
            EngineDelegateMiddleware(scope),
            CreateEngineSessionMiddleware(
                engine,
                scope,
            ),
            LinkingMiddleware(scope),
            TabsRemovedMiddleware(scope),
            SuspendMiddleware(scope),
            WebExtensionMiddleware(),
            CrashMiddleware(),
            ExtensionsProcessMiddleware(engine),
            TranslationsMiddleware(engine, scope, automaticallyInitializeTranslations),
            PdfStateMiddleware(scope),
        ) + if (trimMemoryAutomatically) {
            listOf(TrimMemoryMiddleware())
        } else {
            emptyList()
        }
    }
}
