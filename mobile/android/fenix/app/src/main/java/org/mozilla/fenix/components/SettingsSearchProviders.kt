/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import org.mozilla.fenix.settings.ToolbarShortcutSettingsSearchProvider
import org.mozilla.fenix.settings.ai.AIControlsSearchProvider
import org.mozilla.fenix.settings.datachoices.DataChoicesSearchProvider
import org.mozilla.fenix.settings.labs.FirefoxLabsSettingsSearchProvider
import org.mozilla.fenix.settings.pagesummaries.PageSummariesSettingsSearchProvider
import org.mozilla.fenix.settings.search.GoogleLensSettingsSearchProvider
import org.mozilla.fenix.settings.settingssearch.SettingsSearchProvider

/**
 * Builds the list of [SettingsSearchProvider]s covering settings the XML-parsing path in
 * [org.mozilla.fenix.settings.settingssearch.DefaultFenixSettingsIndexer] cannot reach: Compose screens with no backing
 * preference file, and entries inside an XML screen that the parser does not emit (an empty `PreferenceCategory` whose
 * contents are built in code, for instance).
 *
 * This is the source of truth for which providers exist. It is wired into the settings indexer from [Components] and is
 * exercised by `SettingsSearchProviderRegistrationTest` so that a screen losing its provider fails a test rather than
 * silently disappearing from settings search.
 */
internal fun settingsSearchProviders(): List<SettingsSearchProvider> =
    listOf(
        DataChoicesSearchProvider,
        AIControlsSearchProvider,
        PageSummariesSettingsSearchProvider,
        FirefoxLabsSettingsSearchProvider(),
        ToolbarShortcutSettingsSearchProvider,
        GoogleLensSettingsSearchProvider,
    )
