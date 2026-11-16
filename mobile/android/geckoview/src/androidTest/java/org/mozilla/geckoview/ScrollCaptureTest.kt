/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview

import android.graphics.PixelFormat
import android.graphics.Point
import android.graphics.Rect
import android.media.Image
import android.media.ImageReader
import android.os.Build
import android.os.CancellationSignal
import android.os.SystemClock
import android.view.ScrollCaptureSession
import android.view.Surface
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.MediumTest
import java.util.function.Supplier
import kotlin.math.roundToInt
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.test.BaseSessionTest
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.AssertCalled
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.WithDisplay
import org.mozilla.geckoview.test.util.UiThreadUtils

private const val DISPLAY_WIDTH = 1080
private const val DISPLAY_HEIGHT = 800

private const val WAIT_TIMEOUT_MS = 20000L

// Per-channel tolerance when checking a captured pixel is fullpage.html's solid green (#00ff00).
private const val GREEN_TOLERANCE = 16

@RunWith(AndroidJUnit4::class)
@MediumTest
class ScrollCaptureTest : BaseSessionTest() {
    private fun assumeScrollCaptureSupported() {
        assumeTrue(
            "ScrollCapture requires API ${Build.VERSION_CODES.S}+",
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.S,
        )
    }

    private fun loadFullPage() {
        mainSession.loadTestPath(BaseSessionTest.FULLPAGE_HTML_PATH)
        sessionRule.waitUntilCalled(
            object : GeckoSession.ContentDelegate {
                @AssertCalled(count = 1) override fun onFirstContentfulPaint(session: GeckoSession) {}
            }
        )
    }

    private fun dpr(): Float = (mainSession.evaluateJS("window.devicePixelRatio") as Number).toFloat()

    private fun cssScrollWidth(): Int =
        (mainSession.evaluateJS("document.documentElement.scrollWidth") as Number).toInt()

    private fun cssScrollHeight(): Int =
        (mainSession.evaluateJS("document.documentElement.scrollHeight") as Number).toInt()

    private fun newImageReader(): ImageReader =
        ImageReader.newInstance(DISPLAY_WIDTH, DISPLAY_HEIGHT, PixelFormat.RGBA_8888, 2)

    private fun newScrollCaptureSession(surface: Surface, bounds: Rect): ScrollCaptureSession =
        ScrollCaptureSession(surface, bounds, Point(0, 0))

    private fun pump(ms: Long) {
        val deadline = SystemClock.uptimeMillis() + ms
        UiThreadUtils.waitForCondition({ SystemClock.uptimeMillis() >= deadline }, ms + WAIT_TIMEOUT_MS)
    }

    private fun awaitImage(reader: ImageReader): Image {
        val holder = arrayOfNulls<Image>(1)
        UiThreadUtils.waitForCondition(
            { reader.acquireLatestImage()?.also { holder[0] = it } != null },
            WAIT_TIMEOUT_MS,
        )
        return holder[0] ?: throw AssertionError("No image was produced")
    }

    private fun assertPixelIsGreen(image: Image, x: Int, y: Int) {
        val plane = image.planes[0]
        val buffer = plane.buffer
        val offset = y * plane.rowStride + x * plane.pixelStride
        val r = buffer.get(offset).toInt() and 0xff
        val g = buffer.get(offset + 1).toInt() and 0xff
        val b = buffer.get(offset + 2).toInt() and 0xff
        assertTrue(
            "Pixel ($x,$y) = ($r,$g,$b) should be green",
            r <= GREEN_TOLERANCE && g >= 255 - GREEN_TOLERANCE && b <= GREEN_TOLERANCE,
        )
    }

    private fun requestRegion(surface: Surface, x: Int, y: Int, width: Int, height: Int, scale: Float) {
        val result = GeckoResult<Void>()
        mainSession.mWindow!!.requestRegionToSurface(result, surface, x, y, width, height, scale)
        sessionRule.waitForResult(result)
    }

    @WithDisplay(height = DISPLAY_HEIGHT, width = DISPLAY_WIDTH)
    @Test
    fun contentMetricsReportsPageSizeAndDpr() {
        assumeScrollCaptureSupported()
        loadFullPage()

        val result = GeckoResult<GeckoSession.Window.ContentMetrics>()
        mainSession.mWindow!!.requestContentMetrics(result, GeckoSession.Window.ContentMetrics())
        val metrics = sessionRule.waitForResult(result)

        assertEquals("Content width matches document scrollWidth", cssScrollWidth(), metrics.width)
        assertEquals("Content height matches document scrollHeight", cssScrollHeight(), metrics.height)
        assertEquals("Device pixel ratio matches", dpr(), metrics.devicePixelRatio, 0.001f)
        assertEquals("Initial horizontal scroll is 0", 0f, metrics.scrollX, 0.5f)
        assertEquals("Initial vertical scroll is 0", 0f, metrics.scrollY, 0.5f)
    }

    @WithDisplay(height = DISPLAY_HEIGHT, width = DISPLAY_WIDTH)
    @Test
    fun contentMetricsReportsScrollOffset() {
        assumeScrollCaptureSupported()
        loadFullPage()

        mainSession.evaluateJS("window.scrollTo(0, 500)")
        // Let the scroll apply before querying the offset.
        mainSession.evaluateJS("new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)))")

        val result = GeckoResult<GeckoSession.Window.ContentMetrics>()
        mainSession.mWindow!!.requestContentMetrics(result, GeckoSession.Window.ContentMetrics())
        val metrics = sessionRule.waitForResult(result)

        assertTrue("Vertical scroll offset reflects the scroll (was ${metrics.scrollY})", metrics.scrollY > 100f)
    }

    @WithDisplay(height = DISPLAY_HEIGHT, width = DISPLAY_WIDTH)
    @Test
    fun requestRegionToSurfaceRendersVisibleContent() {
        assumeScrollCaptureSupported()
        loadFullPage()
        val scale = dpr()
        val reader = newImageReader()
        try {
            requestRegion(
                reader.surface,
                0,
                0,
                (DISPLAY_WIDTH / scale).roundToInt(),
                (DISPLAY_HEIGHT / scale).roundToInt(),
                scale,
            )
            val image = awaitImage(reader)
            try {
                assertPixelIsGreen(image, image.width / 2, image.height / 2)
            } finally {
                image.close()
            }
        } finally {
            reader.close()
        }
    }

    @WithDisplay(height = DISPLAY_HEIGHT, width = DISPLAY_WIDTH)
    @Test
    fun requestRegionToSurfaceRendersOffscreenContent() {
        assumeScrollCaptureSupported()
        loadFullPage()
        val scale = dpr()
        val cssHeight = (DISPLAY_HEIGHT / scale).roundToInt()
        val cssTop = cssScrollHeight() - cssHeight
        assertTrue(
            "Page must be at least two viewports tall to test off-screen capture" +
                " (cssTop=$cssTop, viewport=$cssHeight)",
            cssTop >= cssHeight,
        )

        val reader = newImageReader()
        try {
            requestRegion(reader.surface, 0, cssTop, (DISPLAY_WIDTH / scale).roundToInt(), cssHeight, scale)
            val image = awaitImage(reader)
            try {
                // fullpage.html paints green over the whole page, so green proves the off-screen
                // region was actually painted; a missing paint would leave the surface's clear or
                // stale contents instead.
                assertPixelIsGreen(image, image.width / 2, image.height / 2)
            } finally {
                image.close()
            }
        } finally {
            reader.close()
        }
    }

    @WithDisplay(height = DISPLAY_HEIGHT, width = DISPLAY_WIDTH)
    @Test
    fun requestRegionToSurfaceRejectsZeroSizeWithoutPosting() {
        assumeScrollCaptureSupported()
        loadFullPage()
        val reader = newImageReader()
        try {
            val result = GeckoResult<Void>()
            mainSession.mWindow!!.requestRegionToSurface(result, reader.surface, 0, 0, 0, 0, dpr())

            val resolved = booleanArrayOf(false)
            val rejected = booleanArrayOf(false)
            result.accept({ _ -> resolved[0] = true }, { _ -> rejected[0] = true })
            UiThreadUtils.waitForCondition({ resolved[0] || rejected[0] }, WAIT_TIMEOUT_MS)

            assertTrue("Zero-size region request should reject", rejected[0])
            assertFalse("Zero-size region request should not resolve", resolved[0])

            pump(300)
            assertNull("Rejected request must not post a frame", reader.acquireLatestImage())
        } finally {
            reader.close()
        }
    }

    @WithDisplay(height = DISPLAY_HEIGHT, width = DISPLAY_WIDTH)
    @Test
    fun onScrollCaptureSearchReportsSurfaceBounds() {
        assumeScrollCaptureSupported()
        loadFullPage()
        val callback = GeckoViewScrollCaptureCallback(Supplier { mainSession })

        val bounds = arrayOfNulls<Rect>(1)
        callback.onScrollCaptureSearch(CancellationSignal()) { rect -> bounds[0] = rect }
        UiThreadUtils.waitForCondition({ bounds[0] != null }, WAIT_TIMEOUT_MS)

        assertEquals("Surface bounds match the display", Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT), bounds[0])
    }

    @WithDisplay(height = DISPLAY_HEIGHT, width = DISPLAY_WIDTH)
    @Test
    fun onScrollCaptureImageRequestRendersTile() {
        assumeScrollCaptureSupported()
        loadFullPage()
        val callback = GeckoViewScrollCaptureCallback(Supplier { mainSession })
        val reader = newImageReader()
        try {
            val session = newScrollCaptureSession(reader.surface, Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT))

            val started = booleanArrayOf(false)
            callback.onScrollCaptureStart(session, CancellationSignal()) { started[0] = true }
            UiThreadUtils.waitForCondition({ started[0] }, WAIT_TIMEOUT_MS)

            val captureArea = Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT)
            val captured = arrayOfNulls<Rect>(1)
            callback.onScrollCaptureImageRequest(session, CancellationSignal(), captureArea) { rect ->
                captured[0] = rect
            }
            UiThreadUtils.waitForCondition({ captured[0] != null }, WAIT_TIMEOUT_MS)

            val tile = captured[0]!!
            val slack = Math.ceil(dpr().toDouble()).toInt() + 1
            assertTrue("Captured rect $tile falls within requested $captureArea", captureArea.contains(tile))
            assertTrue(
                "Captured rect $tile should cover requested $captureArea within snapping",
                tile.width() >= captureArea.width() - slack && tile.height() >= captureArea.height() - slack,
            )

            val image = awaitImage(reader)
            try {
                assertPixelIsGreen(image, image.width / 2, image.height / 2)
            } finally {
                image.close()
            }
        } finally {
            callback.onScrollCaptureEnd {}
            reader.close()
        }
    }

    @WithDisplay(height = DISPLAY_HEIGHT, width = DISPLAY_WIDTH)
    @Test
    fun imageRequestBeforeStartReturnsEmpty() {
        assumeScrollCaptureSupported()
        loadFullPage()
        val callback = GeckoViewScrollCaptureCallback(Supplier { mainSession })
        val reader = newImageReader()
        try {
            val session = newScrollCaptureSession(reader.surface, Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT))

            // No onScrollCaptureStart, so there are no content metrics and nothing to capture.
            val captured = arrayOfNulls<Rect>(1)
            callback.onScrollCaptureImageRequest(
                session,
                CancellationSignal(),
                Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT),
            ) { rect ->
                captured[0] = rect
            }
            UiThreadUtils.waitForCondition({ captured[0] != null }, WAIT_TIMEOUT_MS)

            assertTrue("Capture without start reports an empty rect", captured[0]!!.isEmpty)
        } finally {
            reader.close()
        }
    }

    @WithDisplay(height = DISPLAY_HEIGHT, width = DISPLAY_WIDTH)
    @Test
    fun imageRequestAfterCancelReturnsEmptyWithoutPosting() {
        assumeScrollCaptureSupported()
        loadFullPage()
        val callback = GeckoViewScrollCaptureCallback(Supplier { mainSession })
        val reader = newImageReader()
        try {
            val session = newScrollCaptureSession(reader.surface, Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT))

            val started = booleanArrayOf(false)
            callback.onScrollCaptureStart(session, CancellationSignal()) { started[0] = true }
            UiThreadUtils.waitForCondition({ started[0] }, WAIT_TIMEOUT_MS)

            // An already-cancelled request must short-circuit before the native paint: report an
            // empty rect and never post a frame to the surface.
            val signal = CancellationSignal()
            signal.cancel()
            val captured = arrayOfNulls<Rect>(1)
            callback.onScrollCaptureImageRequest(session, signal, Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT)) { rect ->
                captured[0] = rect
            }
            UiThreadUtils.waitForCondition({ captured[0] != null }, WAIT_TIMEOUT_MS)

            assertTrue("Cancelled request reports an empty rect", captured[0]!!.isEmpty)

            pump(300)
            assertNull("Cancelled request must not post a frame", reader.acquireLatestImage())
        } finally {
            callback.onScrollCaptureEnd {}
            reader.close()
        }
    }

    @WithDisplay(height = DISPLAY_HEIGHT, width = DISPLAY_WIDTH)
    @Test
    fun fullCaptureReportsComplete() {
        assumeScrollCaptureSupported()
        loadFullPage()
        val callback = GeckoViewScrollCaptureCallback(Supplier { mainSession })
        val reader = newImageReader()
        try {
            val session = newScrollCaptureSession(reader.surface, Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT))

            val started = booleanArrayOf(false)
            callback.onScrollCaptureStart(session, CancellationSignal()) { started[0] = true }
            UiThreadUtils.waitForCondition({ started[0] }, WAIT_TIMEOUT_MS)

            val captured = arrayOfNulls<Rect>(1)
            callback.onScrollCaptureImageRequest(
                session,
                CancellationSignal(),
                Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT),
            ) { rect ->
                captured[0] = rect
            }
            UiThreadUtils.waitForCondition({ captured[0] != null }, WAIT_TIMEOUT_MS)
            assertFalse("Tile should render a non-empty rect", captured[0]!!.isEmpty)

            // onScrollCaptureEnd computes the per-capture verdict from the tile tally.
            callback.onScrollCaptureEnd {}
            assertEquals(
                "A full successful capture reports OUTCOME_COMPLETE",
                GeckoViewScrollCaptureCallback.OUTCOME_COMPLETE,
                callback.mLastCaptureOutcome,
            )
        } finally {
            reader.close()
        }
    }
}
