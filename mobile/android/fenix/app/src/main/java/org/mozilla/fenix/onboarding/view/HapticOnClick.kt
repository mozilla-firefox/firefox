/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import androidx.compose.runtime.Composable
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import org.mozilla.fenix.FeatureFlags

/** Wraps [onClick] so that pressing an onboarding button also plays a confirmation haptic. */
@Composable
internal fun hapticOnClick(onClick: () -> Unit): () -> Unit {
    if (!FeatureFlags.onboardingHapticsEnabled) return onClick
    val haptic = LocalHapticFeedback.current
    return {
        haptic.performHapticFeedback(HapticFeedbackType.Confirm)
        onClick()
    }
}

/** Wraps a link's [onClick] so that pressing an onboarding link also plays a confirmation haptic. */
@Composable
internal fun hapticOnLinkClick(onClick: (String) -> Unit): (String) -> Unit {
    if (!FeatureFlags.onboardingHapticsEnabled) return onClick
    val haptic = LocalHapticFeedback.current
    return { url ->
        haptic.performHapticFeedback(HapticFeedbackType.Confirm)
        onClick(url)
    }
}

/** Wraps [onCheckedChange] so that flipping an onboarding toggle also plays the matching toggle haptic. */
@Composable
internal fun hapticOnCheckedChange(onCheckedChange: (Boolean) -> Unit): (Boolean) -> Unit {
    if (!FeatureFlags.onboardingHapticsEnabled) return onCheckedChange
    val haptic = LocalHapticFeedback.current
    return { checked ->
        haptic.performHapticFeedback(if (checked) HapticFeedbackType.ToggleOn else HapticFeedbackType.ToggleOff)
        onCheckedChange(checked)
    }
}
