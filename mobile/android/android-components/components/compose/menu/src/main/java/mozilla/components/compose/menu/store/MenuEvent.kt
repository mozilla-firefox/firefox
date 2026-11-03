/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.store

import mozilla.components.lib.state.Action

/** [Action]s to be dispatched on [MenuStore] when the user interacts with a menu item. */
interface MenuEvent : MenuAction, CustomizableMenuAction
