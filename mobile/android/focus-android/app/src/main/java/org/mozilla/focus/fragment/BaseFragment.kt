/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.fragment

import android.content.res.Resources
import android.view.animation.Animation
import android.view.animation.AnimationSet
import android.view.animation.AnimationUtils
import androidx.core.view.isInvisible
import androidx.fragment.app.Fragment
import org.mozilla.focus.ext.hideToolbar
import org.mozilla.focus.ext.requireComponents
import org.mozilla.focus.state.Screen

/** Base [Fragment] class for the application. */
abstract class BaseFragment : Fragment() {
    private var animationSet: AnimationSet? = null

    override fun onResume() {
        super.onResume()
        hideToolbar()
        view?.isInvisible = requireComponents.appStore.state.screen is Screen.Locked
    }

    /** Cancels any ongoing animations on this fragment. */
    fun cancelAnimation() {
        if (animationSet != null) {
            animationSet!!.duration = 0
            animationSet!!.cancel()
        }
    }

    override fun onPause() {
        super.onPause()
        view?.isInvisible = requireComponents.appStore.state.screen is Screen.Locked
    }

    override fun onCreateAnimation(transit: Int, enter: Boolean, nextAnim: Int): Animation? {
        var animation = super.onCreateAnimation(transit, enter, nextAnim)
        if (animation == null && nextAnim != 0) {
            animation =
                try {
                    AnimationUtils.loadAnimation(activity, nextAnim)
                } catch (e: Resources.NotFoundException) {
                    return null
                }
        }
        return if (animation != null) {
            val animSet = AnimationSet(true)
            animSet.addAnimation(animation)
            animationSet = animSet
            animSet
        } else {
            null
        }
    }
}
