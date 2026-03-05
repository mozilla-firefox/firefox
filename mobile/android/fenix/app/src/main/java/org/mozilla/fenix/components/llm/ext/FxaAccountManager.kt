/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.llm.ext

import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.service.fxa.manager.SCOPE_PROFILE

internal val FxaAccountManager.accessTokenProvider get() = FxaAccessTokenProvider {
    authenticatedAccount()
        ?.getAccessToken(SCOPE_PROFILE)
        ?.token
}
