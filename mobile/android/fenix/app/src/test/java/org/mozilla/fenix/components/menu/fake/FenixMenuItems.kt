/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.fake

import org.mozilla.fenix.components.menu.configuration.FenixMenuItem

/** One instance of each type of menu item. */
fun allMenuItems(): List<FenixMenuItem> =
    FenixMenuItem::class.sealedSubclasses.map { type ->
        type.objectInstance
            ?: when (type) {
                FenixMenuItem.More::class -> FenixMenuItem.More(subMenuItems = emptyList())
                else -> error("Unknown menu item type $type")
            }
    }
