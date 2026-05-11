/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection

import org.mozilla.fenix.ipprotection.store.IPProtectionPromptRepository

class FakeIPProtectionPromptRepository(
    private val canShowIPProtectionPrompt: Boolean = true,
    override var hasShownPrompt: Boolean = false,
    override val hasAlreadyUsedIPProtection: Boolean = false,
) : IPProtectionPromptRepository {
    override var isShowingPrompt = false

    override fun canShowIPProtectionPrompt(currentTimeMillis: Long) = canShowIPProtectionPrompt
}
