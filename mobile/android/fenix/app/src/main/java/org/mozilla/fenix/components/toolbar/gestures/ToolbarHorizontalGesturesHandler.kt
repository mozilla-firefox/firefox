/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.toolbar.gestures

import android.animation.Animator
import android.animation.AnimatorListenerAdapter
import android.animation.ValueAnimator
import android.app.Activity
import android.graphics.PointF
import android.graphics.Rect
import android.view.View
import android.view.ViewConfiguration
import androidx.core.animation.doOnEnd
import androidx.core.graphics.contains
import androidx.core.graphics.toPoint
import androidx.core.view.isVisible
import androidx.interpolator.view.animation.LinearOutSlowInInterpolator
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import mozilla.components.browser.state.selector.getNormalOrPrivateTabs
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.isActive
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.ktx.android.content.pixelSizeFor
import mozilla.components.support.ktx.android.view.getRectWithViewLocation
import mozilla.components.support.utils.ext.bottom
import mozilla.components.support.utils.ext.getWindowInsets
import mozilla.components.support.utils.ext.isKeyboardVisible
import mozilla.components.support.utils.ext.mandatorySystemGestureInsets
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.SwipeGestureListener
import org.mozilla.fenix.browser.TabPreview
import org.mozilla.fenix.components.toolbar.gestures.ToolbarHorizontalGesturesHandler.Companion.GESTURE_FINISH_PERCENT
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.maxActiveTime

/** Handles intercepting touch events on the toolbar for swipe gestures and executes the necessary animations. */
@Suppress("LargeClass", "TooManyFunctions")
class ToolbarHorizontalGesturesHandler(
    private val activity: Activity,
    private val contentLayout: View,
    private val tabPreview: TabPreview,
    private val toolbarLayoutRect: () -> Rect,
    private val navBarLayoutRect: () -> Rect? = { null },
    private val store: BrowserStore,
    private val selectTabUseCase: TabsUseCases.SelectTabUseCase,
    private val onSwipeStarted: () -> Unit,
    private val onTabSwitched: (TabSessionState) -> Unit = {},
) : SwipeGestureListener {

    private enum class GestureDirection {
        LEFT_TO_RIGHT,
        RIGHT_TO_LEFT,
    }

    private sealed class Destination {
        data class Tab(val tab: TabSessionState) : Destination()

        object None : Destination()
    }

    private val windowWidth: Int
        get() = activity.resources.displayMetrics.widthPixels

    private val previewOffset = activity.pixelSizeFor(R.dimen.browser_fragment_gesture_preview_offset)

    private val touchSlop = ViewConfiguration.get(activity).scaledTouchSlop
    private val minimumFlingVelocity = ViewConfiguration.get(activity).scaledMinimumFlingVelocity

    private var gestureDirection = GestureDirection.LEFT_TO_RIGHT

    override fun onSwipeStarted(start: PointF, next: PointF): Boolean {
        val dx = next.x - start.x
        val dy = next.y - start.y
        gestureDirection =
            if (dx < 0) {
                GestureDirection.RIGHT_TO_LEFT
            } else {
                GestureDirection.LEFT_TO_RIGHT
            }

        @Suppress("ComplexCondition")
        return if (
            !activity.window.decorView.isKeyboardVisible() &&
                start.isInToolbar() &&
                abs(dx) > touchSlop &&
                abs(dy) < abs(dx)
        ) {
            preparePreview(getDestination())
            onSwipeStarted.invoke()
            true
        } else {
            false
        }
    }

    override fun onSwipeUpdate(distanceX: Float, distanceY: Float) {
        when (getDestination()) {
            is Destination.Tab -> {
                // Restrict the range of motion for the views so you can't start a swipe in one direction
                // then move your finger far enough or in the other direction and make the content visually
                // start sliding off screen.
                tabPreview.translationX =
                    when (gestureDirection) {
                        GestureDirection.RIGHT_TO_LEFT ->
                            min(
                                    windowWidth.toFloat() + previewOffset,
                                    tabPreview.translationX - distanceX,
                                )
                                .coerceAtLeast(0f)
                        GestureDirection.LEFT_TO_RIGHT ->
                            max(
                                    -windowWidth.toFloat() - previewOffset,
                                    tabPreview.translationX - distanceX,
                                )
                                .coerceAtMost(0f)
                    }
                contentLayout.translationX =
                    when (gestureDirection) {
                        GestureDirection.RIGHT_TO_LEFT ->
                            min(
                                    0f,
                                    contentLayout.translationX - distanceX,
                                )
                                .coerceAtLeast(-windowWidth.toFloat() - previewOffset)
                        GestureDirection.LEFT_TO_RIGHT ->
                            max(
                                    0f,
                                    contentLayout.translationX - distanceX,
                                )
                                .coerceAtMost(windowWidth.toFloat() + previewOffset)
                    }
            }
            is Destination.None -> {
                // If there is no "next" tab to swipe to in the gesture direction, only do a
                // partial animation to show that we are at the end of the tab list
                val maxContentHidden = contentLayout.width * OVERSCROLL_HIDE_PERCENT
                contentLayout.translationX =
                    when (gestureDirection) {
                        GestureDirection.RIGHT_TO_LEFT ->
                            max(
                                    -maxContentHidden.toFloat(),
                                    contentLayout.translationX - distanceX,
                                )
                                .coerceAtMost(0f)
                        GestureDirection.LEFT_TO_RIGHT ->
                            min(
                                    maxContentHidden.toFloat(),
                                    contentLayout.translationX - distanceX,
                                )
                                .coerceAtLeast(0f)
                    }
            }
        }
    }

    override fun onSwipeFinished(
        velocityX: Float,
        velocityY: Float,
    ) {
        val destination = getDestination()
        if (destination is Destination.Tab && isGestureComplete(velocityX)) {
            animateToNextTab(destination.tab)
        } else {
            animateCanceledGesture(velocityX)
        }
    }

    @Suppress("CognitiveComplexMethod")
    private fun getDestination(): Destination {
        val isLtr = activity.resources.configuration.layoutDirection == View.LAYOUT_DIRECTION_LTR
        val currentTab = store.state.selectedTab ?: return Destination.None
        val tabs =
            store.state.getNormalOrPrivateTabs(currentTab.content.private).filter {
                it.isActive(maxActiveTime)
            }

        val currentIndex = tabs.indexOfFirst { it.id == currentTab.id }

        return if (currentIndex == -1) {
            Destination.None
        } else {
            val index =
                when (gestureDirection) {
                    GestureDirection.RIGHT_TO_LEFT ->
                        if (isLtr) {
                            currentIndex + 1
                        } else {
                            currentIndex - 1
                        }
                    GestureDirection.LEFT_TO_RIGHT ->
                        if (isLtr) {
                            currentIndex - 1
                        } else {
                            currentIndex + 1
                        }
                }

            if (index < tabs.count() && index >= 0) {
                Destination.Tab(tabs.elementAt(index))
            } else {
                Destination.None
            }
        }
    }

    private fun preparePreview(destination: Destination) {
        if (destination !is Destination.Tab) {
            return
        }

        tabPreview.loadDestinationPreview(destination.tab)

        tabPreview.translationX =
            when (gestureDirection) {
                GestureDirection.RIGHT_TO_LEFT -> windowWidth.toFloat() + previewOffset
                GestureDirection.LEFT_TO_RIGHT -> -windowWidth.toFloat() - previewOffset
            }
        tabPreview.alpha = 1f
        tabPreview.isVisible = true
    }

    /**
     * Checks if the gesture is complete based on the position of tab preview and the velocity of the gesture. A
     * completed gesture means the user has indicated they want to swipe to the next tab. The gesture is considered
     * complete if one of the following is true:
     *
     * 1. The user initiated a fling in the same direction as the initial movement
     * 2. There is no fling initiated, but the percentage of the tab preview shown is at least [GESTURE_FINISH_PERCENT]
     *
     * If the user initiated a fling in the opposite direction of the initial movement, the gesture is always considered
     * incomplete.
     */
    private fun isGestureComplete(velocityX: Float): Boolean {
        val previewWidth = tabPreview.getRectWithViewLocation().visibleWidth.toDouble()
        val velocityMatchesDirection =
            when (gestureDirection) {
                GestureDirection.RIGHT_TO_LEFT -> velocityX <= 0
                GestureDirection.LEFT_TO_RIGHT -> velocityX >= 0
            }
        val reverseFling = abs(velocityX) >= minimumFlingVelocity && !velocityMatchesDirection

        return !reverseFling &&
            (previewWidth / windowWidth >= GESTURE_FINISH_PERCENT || abs(velocityX) >= minimumFlingVelocity)
    }

    private fun getAnimator(finalContextX: Float, duration: Long): ValueAnimator {
        return ValueAnimator.ofFloat(contentLayout.translationX, finalContextX).apply {
            this.duration = duration
            this.interpolator = LinearOutSlowInInterpolator()
            addUpdateListener { animator ->
                val value = animator.animatedValue as Float
                contentLayout.translationX = value
                tabPreview.translationX =
                    when (gestureDirection) {
                        GestureDirection.RIGHT_TO_LEFT -> value + windowWidth + previewOffset
                        GestureDirection.LEFT_TO_RIGHT -> value - windowWidth - previewOffset
                    }
            }
        }
    }

    private fun animateToNextTab(tab: TabSessionState) {
        val browserFinalXCoordinate: Float =
            when (gestureDirection) {
                GestureDirection.RIGHT_TO_LEFT -> -windowWidth.toFloat() - previewOffset
                GestureDirection.LEFT_TO_RIGHT -> windowWidth.toFloat() + previewOffset
            }

        // Finish animating the contentLayout off screen and tabPreview on screen
        getAnimator(browserFinalXCoordinate, FINISHED_GESTURE_ANIMATION_DURATION)
            .apply {
                doOnEnd {
                    contentLayout.translationX = 0f
                    selectTabUseCase(tab.id)
                    onTabSwitched.invoke(tab)

                    // Fade out the tab preview to prevent flickering
                    val shortAnimationDuration = activity.resources.getInteger(android.R.integer.config_shortAnimTime)
                    tabPreview
                        .animate()
                        .alpha(0f)
                        .setDuration(shortAnimationDuration.toLong())
                        .setListener(
                            object : AnimatorListenerAdapter() {
                                override fun onAnimationEnd(animation: Animator) {
                                    tabPreview.isVisible = false
                                    Events.toolbarTabSwipe.record(NoExtras())
                                }
                            }
                        )
                }
            }
            .start()
    }

    private fun animateCanceledGesture(velocityX: Float) {
        val duration =
            if (abs(velocityX) >= minimumFlingVelocity) {
                CANCELED_FLING_ANIMATION_DURATION
            } else {
                CANCELED_GESTURE_ANIMATION_DURATION
            }

        getAnimator(0f, duration)
            .apply {
                doOnEnd {
                    tabPreview.isVisible = false
                }
            }
            .start()
    }

    private fun PointF.isInToolbar(): Boolean {
        val toolbarLocation = toolbarLayoutRect()
        val navBarLocation = navBarLayoutRect()
        // In Android 10, the system gesture touch area overlaps the bottom of the toolbar, so
        // lets make our swipe area taller by that amount
        activity.window.decorView.getWindowInsets()?.let { insets ->
            if (activity.components.settings.shouldUseBottomToolbar) {
                toolbarLocation.top -= (insets.mandatorySystemGestureInsets().bottom - insets.bottom())
            }
        }
        return toolbarLocation.contains(toPoint()) || navBarLocation?.contains(toPoint()) == true
    }

    private val Rect.visibleWidth: Int
        get() =
            if (left < 0) {
                right
            } else {
                windowWidth - left
            }

    companion object {
        /**
         * The percentage of the tab preview that needs to be visible to consider the tab switching gesture complete.
         */
        private const val GESTURE_FINISH_PERCENT = 0.25

        /**
         * The percentage of the content view that can be hidden by the tab switching gesture if there is not tab
         * available to switch to
         */
        private const val OVERSCROLL_HIDE_PERCENT = 0.20

        /** Animation duration when switching to another tab */
        private const val FINISHED_GESTURE_ANIMATION_DURATION = 250L

        /** Animation duration gesture is canceled due to the swipe not being far enough */
        private const val CANCELED_GESTURE_ANIMATION_DURATION = 200L

        /** Animation duration gesture is canceled due to a swipe in the opposite direction */
        private const val CANCELED_FLING_ANIMATION_DURATION = 150L
    }
}
