/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.os.SystemClock
import android.view.MotionEvent
import androidx.core.graphics.createBitmap
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.MediumTest
import org.hamcrest.Matchers.closeTo
import org.hamcrest.Matchers.equalTo
import org.junit.Assume.assumeThat
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSession.ContentDelegate
import org.mozilla.geckoview.GeckoSession.ScrollDelegate
import org.mozilla.geckoview.PanZoomController
import org.mozilla.geckoview.ScreenLength
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.AssertCalled
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.WithDisplay
import org.mozilla.geckoview.test.util.AssertUtils

private const val SCREEN_WIDTH = 100
private const val SCREEN_HEIGHT = 200

@RunWith(AndroidJUnit4::class)
@MediumTest
class DynamicToolbarTest : BaseSessionTest() {
    // Makes sure we can load a page when the dynamic toolbar is bigger than the whole content
    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun outOfRangeValue() {
        val dynamicToolbarMaxHeight = SCREEN_HEIGHT + 1
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()
    }

    private fun assertScreenshotResult(result: GeckoResult<Bitmap>, comparisonImage: Bitmap) {
        sessionRule.waitForResult(result).let {
            AssertUtils.assertScreenshotResult(it, comparisonImage)
        }
    }

    /** Returns a whole green Bitmap. This Bitmap would be a reference image of tests in this file. */
    private fun getComparisonScreenshot(width: Int, height: Int): Bitmap {
        val screenshotFile = createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(screenshotFile)
        val paint = Paint()
        paint.color = Color.rgb(0, 128, 0)
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), paint)
        return screenshotFile
    }

    // With the dynamic toolbar max height vh units values exceed
    // the top most window height. This is a test case that exceeded area
    // is rendered properly (on the compositor).
    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun positionFixedElementClipping() {
        sessionRule.display?.run { setDynamicToolbarMaxHeight(SCREEN_HEIGHT / 2) }

        val reference = getComparisonScreenshot(SCREEN_WIDTH, SCREEN_HEIGHT)

        // FIXED_VH is an HTML file which has a position:fixed element whose
        // style is "width: 100%; height: 200vh" and the document is scaled by
        // minimum-scale 0.5, so that the height of the element exceeds the
        // window height.
        mainSession.loadTestPath(BaseSessionTest.FIXED_VH)
        mainSession.waitForPageStop()

        // Scroll down bit, if we correctly render the document, the position
        // fixed element still covers whole the document area.
        mainSession.evaluateJS("window.scrollTo({ top: 100, behavior: 'instant' })")

        // Wait a while to make sure the scrolling result is composited on the compositor
        // since capturePixels() takes a snapshot directly from the compositor without
        // waiting for a corresponding MozAfterPaint on the main-thread so it's possible
        // to take a stale snapshot even if it's a result of syncronous scrolling.
        mainSession.evaluateJS("new Promise(resolve => window.setTimeout(resolve, 1000))")

        sessionRule.display?.let {
            assertScreenshotResult(it.capturePixels(), reference)
        }
    }

    // Asynchronous scrolling with the dynamic toolbar max height causes
    // situations where the visual viewport size gets bigger than the layout
    // viewport on the compositor thread because of 200vh position:fixed
    // elements.  This is a test case that a 200vh position element is
    // properly rendered its positions.
    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun layoutViewportExpansion() {
        sessionRule.display?.run { setDynamicToolbarMaxHeight(SCREEN_HEIGHT / 2) }

        val reference = getComparisonScreenshot(SCREEN_WIDTH, SCREEN_HEIGHT)

        mainSession.loadTestPath(BaseSessionTest.FIXED_VH)
        mainSession.waitForPageStop()

        mainSession.evaluateJS("window.scrollTo(0, 100)")

        // Scroll back to the original position by asynchronous scrolling.
        mainSession.evaluateJS("window.scrollTo({ top: 0, behavior: 'smooth' })")

        mainSession.evaluateJS("new Promise(resolve => window.setTimeout(resolve, 1000))")

        sessionRule.display?.let {
            assertScreenshotResult(it.capturePixels(), reference)
        }
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun visualViewportEvents() {
        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.FIXED_VH)
        mainSession.waitForPageStop()

        val pixelRatio = mainSession.evaluateJS("window.devicePixelRatio") as Double
        val scale = mainSession.evaluateJS("window.visualViewport.scale") as Double

        for (i in 1..dynamicToolbarMaxHeight) {
            // Simulate the dynamic toolbar is going to be hidden.
            sessionRule.display?.run { setVerticalClipping(-i) }

            val expectedViewportHeight = (SCREEN_HEIGHT - dynamicToolbarMaxHeight + i) / scale / pixelRatio
            val promise =
                mainSession.evaluatePromiseJS(
                    """
                    new Promise(resolve => {
                      window.visualViewport.addEventListener('resize', resolve(window.visualViewport.height));
                    });
                    """
                        .trimIndent()
                )

            assertThat(
                "The visual viewport height should be changed in response to the dynamc toolbar transition",
                promise.value as Double,
                closeTo(expectedViewportHeight, .01),
            )
        }
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun percentBaseValueOnPositionFixedElement() {
        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.FIXED_PERCENT)
        mainSession.waitForPageStop()

        val originalHeight =
            mainSession.evaluateJS(
                """
                getComputedStyle(document.querySelector('#fixed-element')).height
                """
                    .trimIndent()
            ) as String

        // Set the vertical clipping value to the middle of toolbar transition.
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight / 2) }

        var height =
            mainSession.evaluateJS(
                """
                getComputedStyle(document.querySelector('#fixed-element')).height
                """
                    .trimIndent()
            ) as String

        assertThat(
            "The %-based height should be the static in the middle of toolbar tansition",
            height,
            equalTo(originalHeight),
        )

        // Set the vertical clipping value to hide the toolbar completely.
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }
        height =
            mainSession.evaluateJS(
                """
                getComputedStyle(document.querySelector('#fixed-element')).height
                """
                    .trimIndent()
            ) as String

        val scale = mainSession.evaluateJS("window.visualViewport.scale") as Double
        val expectedHeight = (SCREEN_HEIGHT / scale).toInt()
        assertThat(
            "The %-based height should be now recomputed based on the screen height",
            height,
            equalTo(expectedHeight.toString() + "px"),
        )
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun resizeEvents() {
        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.FIXED_VH)
        mainSession.waitForPageStop()

        for (i in 1..dynamicToolbarMaxHeight - 1) {
            val promise =
                mainSession.evaluatePromiseJS(
                    """
                    new Promise(resolve => {
                        let fired = false;
                        window.addEventListener('resize', () => { fired = true; }, { once: true });
                        // Note that `resize` event is fired just before rAF callbacks, so under ideal
                        // circumstances waiting for a rAF should be sufficient, even if it's not sufficient
                        // unexpected resize event(s) will be caught in the next loop.
                        requestAnimationFrame(() => { resolve(fired); });
                    });
                    """
                        .trimIndent()
                )

            // Simulate the dynamic toolbar is going to be hidden.
            sessionRule.display?.run { setVerticalClipping(-i) }
            assertThat(
                "'resize' event on window should not be fired in response to the dynamc toolbar transition",
                promise.value as Boolean,
                equalTo(false),
            )
        }

        val promise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve => {
                    window.addEventListener('resize', () => { resolve(true); }, { once: true });
                });
                """
                    .trimIndent()
            )

        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }
        assertThat(
            "'resize' event on window should be fired when the dynamc toolbar is completely hidden",
            promise.value as Boolean,
            equalTo(true),
        )
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun windowInnerHeight() {
        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        // We intentionally use FIXED_BOTTOM instead of FIXED_VH in this test since
        // FIXED_VH has `minimum-scale=0.5` thus we can't properly test window.innerHeight
        // with FXIED_VH for now due to bug 1598487.
        mainSession.loadTestPath(BaseSessionTest.FIXED_BOTTOM)
        mainSession.waitForPageStop()

        val pixelRatio = mainSession.evaluateJS("window.devicePixelRatio") as Double

        for (i in 1..dynamicToolbarMaxHeight - 1) {
            val promise =
                mainSession.evaluatePromiseJS(
                    """
                    new Promise(resolve => {
                      window.visualViewport.addEventListener('resize', resolve(window.innerHeight));
                    });
                    """
                        .trimIndent()
                )

            // Simulate the dynamic toolbar is going to be hidden.
            sessionRule.display?.run { setVerticalClipping(-i) }
            assertThat(
                "window.innerHeight should not be changed in response to the dynamc toolbar transition",
                promise.value as Double,
                closeTo(SCREEN_HEIGHT / 2 / pixelRatio, .01),
            )
        }

        val promise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve => {
                    window.addEventListener('resize', () => { resolve(window.innerHeight); }, { once: true });
                });
                """
                    .trimIndent()
            )

        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }
        assertThat(
            "window.innerHeight should be changed when the dynamc toolbar is completely hidden",
            promise.value as Double,
            closeTo(SCREEN_HEIGHT / pixelRatio, .01),
        )
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun notCrashOnResizeEvent() {
        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.FIXED_VH)
        mainSession.waitForPageStop()

        val promise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve => window.addEventListener('resize', () => resolve(true)));
                """
                    .trimIndent()
            )

        // Do some setVerticalClipping calls that we might try to queue two window resize events.
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight + 1) }
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }

        assertThat("Got a rezie event", promise.value as Boolean, equalTo(true))
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun showDynamicToolbar() {
        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // disabled for frequent failures - on Bug 1855082
        assumeThat(sessionRule.env.isX86, equalTo(false))

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(SHOW_DYNAMIC_TOOLBAR_HTML_PATH + "?target=body")
        mainSession.waitForPageStop()
        mainSession.evaluateJS("window.scrollTo(0, " + dynamicToolbarMaxHeight + ")")
        mainSession.waitUntilCalled(
            object : ScrollDelegate {
                @AssertCalled(count = 1)
                override fun onScrollChanged(session: GeckoSession, scrollX: Int, scrollY: Int) {}
            }
        )

        // Simulate the dynamic toolbar being hidden by the scroll
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }

        mainSession.synthesizeTap(5, 25)

        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1) override fun onShowDynamicToolbar(session: GeckoSession) {}
            }
        )
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun showDynamicToolbarOnFrameReconstruction() {
        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(SHOW_DYNAMIC_TOOLBAR_HTML_PATH + "?target=html")
        mainSession.waitForPageStop()
        mainSession.evaluateJS("window.scrollTo(0, " + dynamicToolbarMaxHeight + ")")
        mainSession.waitUntilCalled(
            object : ScrollDelegate {
                @AssertCalled(count = 1)
                override fun onScrollChanged(session: GeckoSession, scrollX: Int, scrollY: Int) {}
            }
        )

        // Simulate the dynamic toolbar being hidden by the scroll
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }

        mainSession.synthesizeTap(5, 25)

        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1) override fun onShowDynamicToolbar(session: GeckoSession) {}
            }
        )
    }

    @WithDisplay(height = 600, width = 600)
    @Test
    fun hideDynamicToolbarToRevealFocusedInput() {
        // The <input> element on the test page is 80 CSS pixels tall.
        // Its height in screen pixels is that amount multiplied by the device
        // scale, which can be as high as 3 on some devices.
        // Ensure the dynamic toolbar is taller than that.
        val dynamicToolbarMaxHeight = 300
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(HIDE_DYNAMIC_TOOLBAR_HTML_PATH)
        mainSession.waitForPageStop()
        mainSession.evaluateJS("document.getElementById('input1').focus();")
        mainSession.zoomToFocusedInput()

        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1) override fun onHideDynamicToolbar(session: GeckoSession) {}
            }
        )
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun showDynamicToolbarOnOverflowHidden() {
        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(SHOW_DYNAMIC_TOOLBAR_HTML_PATH)
        mainSession.waitForPageStop()
        mainSession.evaluateJS("window.scrollTo(0, " + dynamicToolbarMaxHeight + ")")
        mainSession.waitUntilCalled(
            object : ScrollDelegate {
                @AssertCalled(count = 1)
                override fun onScrollChanged(session: GeckoSession, scrollX: Int, scrollY: Int) {}
            }
        )

        // Simulate the dynamic toolbar being hidden by the scroll
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }

        mainSession.evaluateJS("document.documentElement.style.overflow = 'hidden'")

        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1) override fun onShowDynamicToolbar(session: GeckoSession) {}
            }
        )
    }

    private fun getComputedViewportHeight(style: String): Double {
        val viewportHeight =
            mainSession.evaluateJS(
                """
            const target = document.createElement('div');
            target.style.height = '$style';
            document.body.appendChild(target);
            parseFloat(getComputedStyle(target).height);
            """
                    .trimIndent()
            ) as Double

        return viewportHeight
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun viewportVariants() {
        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.VIEWPORT_PATH)
        mainSession.waitForPageStop()

        val pixelRatio = mainSession.evaluateJS("window.devicePixelRatio") as Double
        val scale = mainSession.evaluateJS("window.visualViewport.scale") as Double

        var smallViewportHeight = getComputedViewportHeight("100svh")
        assertThat(
            "svh value at the initial state",
            smallViewportHeight,
            closeTo((SCREEN_HEIGHT - dynamicToolbarMaxHeight) / scale / pixelRatio, 0.1),
        )

        var largeViewportHeight = getComputedViewportHeight("100lvh")
        assertThat(
            "lvh value at the initial state",
            largeViewportHeight,
            closeTo(SCREEN_HEIGHT / scale / pixelRatio, 0.1),
        )

        var dynamicViewportHeight = getComputedViewportHeight("100dvh")
        assertThat(
            "dvh value at the initial state",
            dynamicViewportHeight,
            closeTo((SCREEN_HEIGHT - dynamicToolbarMaxHeight) / scale / pixelRatio, 0.1),
        )

        // Move down the toolbar at a fourth of its position.
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight / 4) }

        smallViewportHeight = getComputedViewportHeight("100svh")
        assertThat(
            "svh value during toolbar transition",
            smallViewportHeight,
            closeTo((SCREEN_HEIGHT - dynamicToolbarMaxHeight) / scale / pixelRatio, 0.1),
        )

        largeViewportHeight = getComputedViewportHeight("100lvh")
        assertThat(
            "lvh value during toolbar transition",
            largeViewportHeight,
            closeTo(SCREEN_HEIGHT / scale / pixelRatio, 0.1),
        )

        dynamicViewportHeight = getComputedViewportHeight("100dvh")
        assertThat(
            "dvh value during toolbar transition",
            dynamicViewportHeight,
            closeTo((SCREEN_HEIGHT - dynamicToolbarMaxHeight + dynamicToolbarMaxHeight / 4) / scale / pixelRatio, 0.1),
        )
    }

    // With dynamic toolbar, there was a floating point rounding error in Gecko layout side.
    // The error was appeared by user interactive async scrolling, not by programatic async
    // scrolling, e.g. scrollTo() method. If the error happens there will appear 1px gap
    // between <body> and an element which covers up the <body> element.
    // This test simulates the situation.
    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun noGapAppearsBetweenBodyAndElementFullyCoveringBody() {
        // Bug 1764219 - disable the test to reduce intermittent failure rate
        assumeThat(sessionRule.env.isDebugBuild, equalTo(false))
        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        val reference = getComparisonScreenshot(SCREEN_WIDTH, SCREEN_HEIGHT)

        mainSession.loadTestPath(BaseSessionTest.BODY_FULLY_COVERED_BY_GREEN_ELEMENT)
        mainSession.waitForPageStop()
        mainSession.flushApzRepaints()

        // Scrolling down by touch events.
        var downTime = SystemClock.uptimeMillis()
        var down =
            MotionEvent.obtain(
                downTime,
                SystemClock.uptimeMillis(),
                MotionEvent.ACTION_DOWN,
                50f,
                70f,
                0,
            )
        mainSession.panZoomController.onTouchEvent(down)
        var move =
            MotionEvent.obtain(
                downTime,
                SystemClock.uptimeMillis(),
                MotionEvent.ACTION_MOVE,
                50f,
                30f,
                0,
            )
        mainSession.panZoomController.onTouchEvent(move)
        var up =
            MotionEvent.obtain(
                downTime,
                SystemClock.uptimeMillis(),
                MotionEvent.ACTION_UP,
                50f,
                10f,
                0,
            )
        mainSession.panZoomController.onTouchEvent(up)
        mainSession.flushApzRepaints()

        // Scrolling up by touch events to restore the original position.
        downTime = SystemClock.uptimeMillis()
        down =
            MotionEvent.obtain(
                downTime,
                SystemClock.uptimeMillis(),
                MotionEvent.ACTION_DOWN,
                50f,
                10f,
                0,
            )
        mainSession.panZoomController.onTouchEvent(down)
        move =
            MotionEvent.obtain(
                downTime,
                SystemClock.uptimeMillis(),
                MotionEvent.ACTION_MOVE,
                50f,
                30f,
                0,
            )
        mainSession.panZoomController.onTouchEvent(move)
        up =
            MotionEvent.obtain(
                downTime,
                SystemClock.uptimeMillis(),
                MotionEvent.ACTION_UP,
                50f,
                70f,
                0,
            )
        mainSession.panZoomController.onTouchEvent(up)
        mainSession.flushApzRepaints()

        sessionRule.display?.let {
            assertScreenshotResult(it.capturePixels(), reference)
        }
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun zoomedOverflowHidden() {
        val reference = getComparisonScreenshot(SCREEN_WIDTH, SCREEN_HEIGHT)

        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for foreground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.FIXED_BOTTOM)
        mainSession.waitForPageStop()

        // Change the body background color to match the reference image's background color.
        mainSession.evaluateJS("document.body.style.background = 'rgb(0, 128, 0)'")

        // Hide the vertical scrollbar.
        mainSession.evaluateJS("document.documentElement.style.scrollbarWidth = 'none'")

        // Zoom in the content so that the content's visual viewport can be scrollable.
        mainSession.setResolutionAndScaleTo(10.0f)

        // Simulate the dynamic toolbar being hidden by the scroll
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }

        mainSession.flushApzRepaints()

        sessionRule.display?.let {
            assertScreenshotResult(it.capturePixels(), reference)
        }
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun zoomedPositionFixedRoot() {
        val reference = getComparisonScreenshot(SCREEN_WIDTH, SCREEN_HEIGHT)

        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for foreground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.FIXED_BOTTOM)
        mainSession.waitForPageStop()

        // Change the body background color to match the reference image's background color.
        mainSession.evaluateJS("document.body.style.background = 'rgb(0, 128, 0)'")

        // Change the root `overlow` style to make it scrollable and change the position style
        // to `fixed` so that the root container is not scrollable.
        mainSession.evaluateJS("document.body.style.overflow = 'scroll'")
        mainSession.evaluateJS("document.documentElement.style.position = 'fixed'")

        // Hide the vertical scrollbar.
        mainSession.evaluateJS("document.documentElement.style.scrollbarWidth = 'none'")

        // Zoom in the content so that the content's visual viewport can be scrollable.
        mainSession.setResolutionAndScaleTo(10.0f)

        // Simulate the dynamic toolbar being hidden by the scroll
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }

        mainSession.flushApzRepaints()

        sessionRule.display?.let {
            assertScreenshotResult(it.capturePixels(), reference)
        }
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun backgroundImageFixed() {
        val reference = getComparisonScreenshot(SCREEN_WIDTH, SCREEN_HEIGHT)

        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.TOUCH_ACTION_HTML_PATH)
        mainSession.waitForPageStop()

        // Specify the root background-color to match the reference image color and specify
        // `background-attachment: fixed`.
        mainSession.evaluateJS("document.documentElement.style.background = 'linear-gradient(green, green) fixed'")

        // Make the root element scrollable.
        mainSession.evaluateJS("document.documentElement.style.height = '100vh'")

        // Hide the vertical scrollbar.
        mainSession.evaluateJS("document.documentElement.style.scrollbarWidth = 'none'")

        mainSession.flushApzRepaints()

        // Simulate the dynamic toolbar being hidden by the scroll
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }

        mainSession.flushApzRepaints()

        sessionRule.display?.let {
            assertScreenshotResult(it.capturePixels(), reference)
        }
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun backgroundAttachmentFixed() {
        val reference = getComparisonScreenshot(SCREEN_WIDTH, SCREEN_HEIGHT)

        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.TOUCH_ACTION_HTML_PATH)
        mainSession.waitForPageStop()

        // Specify the root background-color to match the reference image color and specify
        // `background-attachment: fixed`.
        mainSession.evaluateJS("document.documentElement.style.background = 'rgb(0, 128, 0) fixed'")

        // Make the root element scrollable.
        mainSession.evaluateJS("document.documentElement.style.height = '100vh'")

        // Hide the vertical scrollbar.
        mainSession.evaluateJS("document.documentElement.style.scrollbarWidth = 'none'")

        mainSession.flushApzRepaints()

        // Simulate the dynamic toolbar being hidden by the scroll
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }

        mainSession.flushApzRepaints()

        sessionRule.display?.let {
            assertScreenshotResult(it.capturePixels(), reference)
        }
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun withIntersectionObserver1() {
        val dynamicToolbarMaxHeight = 20
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.INTERSECTION_OBSERVER_HTML_PATH)
        mainSession.waitForPageStop()

        // Position the target element underneath the dynamic toolbar.
        mainSession.evaluateJS(
            """
            document.querySelector('#target').style.top = 'calc(100svh + 1px)';
            document.querySelector('#target').getBoundingClientRect();
            """
                .trimIndent()
        )

        // Setup an IntersectionObserver to change the target element background color
        // if the target element is considered as "intersecting" by the observer.
        mainSession.evaluatePromiseJS(
            """
            new Promise(resolve => {
              const observer = new IntersectionObserver(entries => {
                const intersected = entries.find(entry => entry.isIntersecting);
                if (intersected) {
                  intersected.target.style.backgroundColor = 'green';
                  resolve(true);
                }
              });

              observer.observe(document.getElementById('target'));
            });
            """
                .trimIndent()
        )

        // Make sure the target background is "red".
        var backgroundColor =
            mainSession.evaluateJS(
                """
                getComputedStyle(document.querySelector('#target')).backgroundColor;
                """
                    .trimIndent()
            ) as String
        assertThat(
            "The background color of the IntersectionObserver's target element should be red",
            backgroundColor,
            equalTo("rgb(255, 0, 0)"),
        )

        // Half collapse the dynamic toolbar, now the target element should be visible.
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight / 2) }

        // But the background color should be still "red".
        backgroundColor =
            mainSession.evaluateJS(
                """
                getComputedStyle(document.querySelector('#target')).backgroundColor;
                """
                    .trimIndent()
            ) as String
        assertThat(
            "The background color of the IntersectionObserver's target element should be still red",
            backgroundColor,
            equalTo("rgb(255, 0, 0)"),
        )
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun withIntersectionObserver2() {
        val dynamicToolbarMaxHeight = 20
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.INTERSECTION_OBSERVER_HTML_PATH)
        mainSession.waitForPageStop()

        // Position the target element out of the layout viewport.
        mainSession.evaluateJS(
            """
            document.querySelector('#target').style.top = 'calc(100lvh + 1px)';
            document.querySelector('#target').getBoundingClientRect();
            """
                .trimIndent()
        )

        // Setup an IntersectionObserver to change the target element background color
        // if the target element is considered as "intersecting" by the observer.
        val promise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve => {
                  const observer = new IntersectionObserver(entries => {
                    const intersected = entries.find(entry => entry.isIntersecting);
                    if (intersected) {
                      intersected.target.style.backgroundColor = 'green';
                      resolve(true);
                    }
                  });

                  observer.observe(document.getElementById('target'));
                });
                """
                    .trimIndent()
            )

        // Make sure the target background is "red".
        var backgroundColor =
            mainSession.evaluateJS(
                """
                getComputedStyle(document.querySelector('#target')).backgroundColor;
                """
                    .trimIndent()
            ) as String
        assertThat(
            "The background color of the IntersectionObserver's target element should be red",
            backgroundColor,
            equalTo("rgb(255, 0, 0)"),
        )

        // Fully collapse the dynamic toolbar, now the target element should NOT be visible.
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }

        backgroundColor =
            mainSession.evaluateJS(
                """
                getComputedStyle(document.querySelector('#target')).backgroundColor;
                """
                    .trimIndent()
            ) as String
        assertThat(
            "The background color of the IntersectionObserver's target element should be still red",
            backgroundColor,
            equalTo("rgb(255, 0, 0)"),
        )

        // Scroll down a bit to move the target element is into the layout viewport.
        mainSession.evaluateJS("window.scrollBy(0, 10)")
        assertThat("resize", promise.value as Boolean, equalTo(true))

        backgroundColor =
            mainSession.evaluateJS(
                """
                getComputedStyle(document.querySelector('#target')).backgroundColor;
                """
                    .trimIndent()
            ) as String
        assertThat(
            "The background color of the IntersectionObserver's target element should have changed to green",
            backgroundColor,
            equalTo("rgb(0, 128, 0)"),
        )
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun withIntersectionObserverWithDesktopMode1() {
        val dynamicToolbarMaxHeight = 20
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.INTERSECTION_OBSERVER_DESKTOP_HTML_PATH)
        mainSession.waitForPageStop()

        // Position the target element underneath the dynamic toolbar.
        mainSession.evaluateJS(
            // The document has 'miminum-scale=0.5' in the meta viewport tag and
            // has 'width: 200%' body element so that it gets scaled by 0.5 initially.
            // Thus the bottom dynamic toolbar is positioned at `200svh`.
            """
            document.querySelector('#target').style.top = 'calc(200svh + 1px)';
            document.querySelector('#target').getBoundingClientRect();
            """
                .trimIndent()
        )

        // Setup an IntersectionObserver to change the target element background color
        // if the target element is considered as "intersecting" by the observer.
        mainSession.evaluatePromiseJS(
            """
            new Promise(resolve => {
              const observer = new IntersectionObserver(entries => {
                const intersected = entries.find(entry => entry.isIntersecting);
                if (intersected) {
                  intersected.target.style.backgroundColor = 'green';
                  resolve(true);
                }
              });

              observer.observe(document.getElementById('target'));
            });
            """
                .trimIndent()
        )

        // Make sure the target background is "red".
        var backgroundColor =
            mainSession.evaluateJS(
                """
                getComputedStyle(document.querySelector('#target')).backgroundColor;
                """
                    .trimIndent()
            ) as String
        assertThat(
            "The background color of the IntersectionObserver's target element should be red",
            backgroundColor,
            equalTo("rgb(255, 0, 0)"),
        )

        // Half collapse the dynamic toolbar, now the target element should be visible.
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight / 2) }

        // But the background color should be still "red".
        backgroundColor =
            mainSession.evaluateJS(
                """
                getComputedStyle(document.querySelector('#target')).backgroundColor;
                """
                    .trimIndent()
            ) as String
        assertThat(
            "The background color of the IntersectionObserver's target element should be still red",
            backgroundColor,
            equalTo("rgb(255, 0, 0)"),
        )
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun withIntersectionObserverWithDesktopMode2() {
        val dynamicToolbarMaxHeight = 20
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for forground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.INTERSECTION_OBSERVER_DESKTOP_HTML_PATH)
        mainSession.waitForPageStop()

        // Position the target element out of the layout viewport.
        mainSession.evaluateJS(
            // Similar to the above test, `200lvh` is the bottom of the dynamic toolbar.
            """
            document.querySelector('#target').style.top = 'calc(200lvh + 1px)';
            document.querySelector('#target').getBoundingClientRect();
            """
                .trimIndent()
        )

        // Setup an IntersectionObserver to change the target element background color
        // if the target element is considered as "intersecting" by the observer.
        val promise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve => {
                  const observer = new IntersectionObserver(entries => {
                    const intersected = entries.find(entry => entry.isIntersecting);
                    if (intersected) {
                      intersected.target.style.backgroundColor = 'green';
                      resolve(true);
                    }
                  });

                  observer.observe(document.getElementById('target'));
                });
                """
                    .trimIndent()
            )

        // Make sure the target background is "red".
        var backgroundColor =
            mainSession.evaluateJS(
                """
                getComputedStyle(document.querySelector('#target')).backgroundColor;
                """
                    .trimIndent()
            ) as String
        assertThat(
            "The background color of the IntersectionObserver's target element should be red",
            backgroundColor,
            equalTo("rgb(255, 0, 0)"),
        )

        // Fully collapse the dynamic toolbar, now the target element should NOT be visible.
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }

        backgroundColor =
            mainSession.evaluateJS(
                """
                getComputedStyle(document.querySelector('#target')).backgroundColor;
                """
                    .trimIndent()
            ) as String
        assertThat(
            "The background color of the IntersectionObserver's target element should be still red",
            backgroundColor,
            equalTo("rgb(255, 0, 0)"),
        )

        // Scroll down a bit to move the target element is into the layout viewport.
        mainSession.evaluateJS("window.scrollBy(0, 10)")
        assertThat("resize", promise.value as Boolean, equalTo(true))

        backgroundColor =
            mainSession.evaluateJS(
                """
                getComputedStyle(document.querySelector('#target')).backgroundColor;
                """
                    .trimIndent()
            ) as String
        assertThat(
            "The background color of the IntersectionObserver's target element should have changed to green",
            backgroundColor,
            equalTo("rgb(0, 128, 0)"),
        )
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun bug1909181() {
        val reference = getComparisonScreenshot(SCREEN_WIDTH, SCREEN_HEIGHT)

        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for foreground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.BUG1909181_HTML_PATH)
        mainSession.waitForPageStop()

        // Zoom in the document.
        mainSession.setResolutionAndScaleTo(5.0f)
        mainSession.flushApzRepaints()

        mainSession.panZoomController.scrollBy(
            ScreenLength.zero(),
            ScreenLength.fromPixels(500.0),
            PanZoomController.SCROLL_BEHAVIOR_AUTO,
        )

        // Simulate the dynamic toolbar being hidden by the scroll
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }

        mainSession.flushApzRepaints()
        mainSession.flushApzRepaints()

        sessionRule.display?.let {
            assertScreenshotResult(it.capturePixels(), reference)
        }
    }

    // Adapted off test bug1909181(). Generally ::-moz-snapshot-containing-block matches
    // the viewport frame size, but on mobile we verify it includes the dynamic toolbar height.
    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun viewTransitionSnapshotSize() {
        val reference = getComparisonScreenshot(SCREEN_WIDTH, SCREEN_HEIGHT)

        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for foreground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.VIEW_TRANSITION_SNAPSHOT_SIZE)
        mainSession.waitForPageStop()

        // Wait for the view transition to start.
        val promise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve => {
                    document.startViewTransition(() => {}).ready
                        .then(() => resolve(true))
                        .catch(() => resolve(false));
                });
                """
                    .trimIndent()
            )
        assertThat("View transition is ready", promise.value as Boolean, equalTo(true))

        // Simulate the dynamic toolbar being hidden by the scroll.
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }

        mainSession.flushApzRepaints()
        mainSession.flushApzRepaints()

        sessionRule.display?.let {
            assertScreenshotResult(it.capturePixels(), reference)
        }
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun hitTestOnPositionSticky() {
        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for foreground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.POSITION_STICKY_HTML_PATH)
        mainSession.waitForPageStop()
        mainSession.flushApzRepaints()

        val clickEventPromise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve => {
                    document.querySelector('button').addEventListener('click', () => {
                        resolve(true);
                    });
                });
                """
                    .trimIndent()
            )

        // Explicitly call `waitForRoundTrip()` to make sure the above event listener
        // has set up in the content.
        mainSession.waitForRoundTrip()

        // Simulate the dynamic toolbar being hidden by the scroll
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }

        // To make sure the dynamic toolbar height has been reflected into APZ.
        mainSession.flushApzRepaints()

        mainSession.synthesizeTap(SCREEN_WIDTH / 2, SCREEN_HEIGHT - dynamicToolbarMaxHeight / 4)

        assertThat("click event", clickEventPromise.value as Boolean, equalTo(true))
    }

    @WithDisplay(height = SCREEN_HEIGHT, width = SCREEN_WIDTH)
    @Test
    fun hitTestOnPositionStickyOnMainThread() {
        val dynamicToolbarMaxHeight = SCREEN_HEIGHT / 2
        sessionRule.display?.run { setDynamicToolbarMaxHeight(dynamicToolbarMaxHeight) }

        // Set active since setVerticalClipping call affects only for foreground tab.
        mainSession.setActive(true)

        mainSession.loadTestPath(BaseSessionTest.POSITION_STICKY_ON_MAIN_THREAD_HTML_PATH)
        mainSession.waitForPageStop()
        mainSession.flushApzRepaints()

        // Scroll to the bottom edge first.
        val scrollPromise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve => {
                    window.addEventListener('scroll', () => {
                        resolve(true);
                    }, { once: true });
                });
                """
                    .trimIndent()
            )
        mainSession.waitForRoundTrip()
        mainSession.evaluateJS(
            """
            document.scrollingElement.scrollTo(0, document.scrollingElement.scrollHeight);
            """
                .trimIndent()
        )

        assertThat("scroll", scrollPromise.value as Boolean, equalTo(true))
        mainSession.flushApzRepaints()

        var clickEventPromise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve => {
                    document.querySelectorAll('button').forEach(element => {
                        element.addEventListener('click', event => {
                            resolve(event.target.id);
                        }, { once: true });
                    });
                });
                """
                    .trimIndent()
            )

        // Explicitly call `waitForRoundTrip()` to make sure the above event listener
        // has set up in the content.
        mainSession.waitForRoundTrip()

        // Simulate the dynamic toolbar being hidden by the scroll
        sessionRule.display?.run { setVerticalClipping(-dynamicToolbarMaxHeight) }

        // To make sure the dynamic toolbar height has been reflected into APZ.
        mainSession.flushApzRepaints()
        // Also to make sure the dynamic toolbar height has been reflected on the main-thread.
        mainSession.promiseAllPaintsDone()

        // Click a point where the dynamic toolbar was covering originally.
        mainSession.synthesizeTap(SCREEN_WIDTH / 2, SCREEN_HEIGHT - dynamicToolbarMaxHeight / 4)
        assertThat("click event on sticky", clickEventPromise.value as String, equalTo("sticky"))

        clickEventPromise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve => {
                    document.querySelectorAll('button').forEach(element => {
                        element.addEventListener('click', event => {
                            resolve(event.target.id);
                        }, { once: true });
                    });
                });
                """
                    .trimIndent()
            )

        mainSession.waitForRoundTrip()

        mainSession.synthesizeTap(
            SCREEN_WIDTH / 2,
            SCREEN_HEIGHT - dynamicToolbarMaxHeight - dynamicToolbarMaxHeight / 4,
        )
        assertThat("click event on not-sticky", clickEventPromise.value as String, equalTo("not-sticky"))
    }
}
