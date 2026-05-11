/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.Settings.Companion.ONE_WEEK_MS

/**
 * Repository for preferences related to the IP Protection bottom sheet.
 */
interface IPProtectionPromptRepository {
    /**
     * Determines whether the IP Protection prompt can be shown.
     *
     * @param currentTimeMillis the current time in milliseconds.
     *
     * @return `true` if the following conditions are met:
     *
     * - The user has not used the built-in VPN before.
     * - The IP Protection feature flag is enabled.
     * - Application has been installed for at least 7 days.
     */
    fun canShowIPProtectionPrompt(currentTimeMillis: Long): Boolean

    /**
     * A boolean to track if we are currently showing the IP protection bottom sheet prompt.
     * This is used when determining if we can show the prompt. We don't want to recreate
     * it if it is already showing.
     */
    var isShowingPrompt: Boolean

    /**
     * A boolean that indicates if the IPProtection onboarding bottom sheet has been already shown to the user.
     *
     * `true` makes the IPProtection bottom sheet appear, while `false` ensures the user does not see
     * the bottom sheet again. This is only shown to the user once and
     * if they dismiss it in anyway (e.g. tap on "Not now" or "Get started") then they will never see it again.
     */
    var hasShownPrompt: Boolean

    /**
     * A boolean that indicates if the user has already toggled the VPN on. This is used to determine if we should show
     * the onboarding card to them, because if the user has already toggled the VPN on, they should not see
     * the onboarding card again.
     */
    val hasAlreadyUsedIPProtection: Boolean
}

/**
 * Default implementation of [IPProtectionPromptRepository].
 *
 * @param settings the preferences settings
 * @param installedTimeMillis returns the application installation timestamp (epoch milliseconds).
 */
class DefaultIPProtectionPromptRepository(
    private val settings: Settings,
    private val installedTimeMillis: () -> Long,
) : IPProtectionPromptRepository {

    override var isShowingPrompt = false

    override var hasShownPrompt: Boolean
        get() = settings.hasShownIPProtectionPrompt
        set(value) {
            settings.hasShownIPProtectionPrompt = value
        }

    override val hasAlreadyUsedIPProtection: Boolean
        get() = settings.hasAlreadyUsedVpn

    override fun canShowIPProtectionPrompt(currentTimeMillis: Long): Boolean =
        settings.isIPProtectionAvailable && isInstalledAtLeastAWeekAgo(currentTimeMillis) &&
            !isShowingPrompt && !hasShownPrompt && !hasAlreadyUsedIPProtection

    private fun isInstalledAtLeastAWeekAgo(currentTimeMillis: Long): Boolean =
        currentTimeMillis - installedTimeMillis() >= ONE_WEEK_MS
}
