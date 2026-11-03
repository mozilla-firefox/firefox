/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import kotlin.time.Instant

/** An in-memory [IPProtectionRepository] used for unit tests. */
class FakeIPProtectionRepository(
    override var hasAlreadyUsedVpn: Boolean = false,
    override var lastDataLimitNoticeCycleResetTime: Instant? = null,
) : IPProtectionRepository
