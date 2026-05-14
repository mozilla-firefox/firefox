/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import mozilla.components.lib.state.Store

/**
 * [Store] for handling [SettingsState] and dispatching [SettingsAction]s.
 */
typealias SettingsStore = Store<SettingsState, SettingsAction>
