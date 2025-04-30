/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

@file:Suppress("ktlint:standard:no-wildcard-imports")

package org.mozilla.geckoview.test

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.os.Build
import android.os.SystemClock
import android.view.*
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.MediumTest
import androidx.test.platform.app.InstrumentationRegistry
import org.hamcrest.Matchers.*
import org.junit.Assert.assertNull
import org.junit.Assume.assumeThat
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.*
import org.mozilla.geckoview.GeckoSession.ContentDelegate
import org.mozilla.geckoview.GeckoSession.ContentDelegate.ContextElement
import org.mozilla.geckoview.GeckoSession.SelectionActionDelegate
import org.mozilla.geckoview.GeckoSession.SelectionActionDelegate.*
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.AssertCalled
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.WithDisplay

@RunWith(AndroidJUnit4::class)
@MediumTest
class ContentDelegateChildTest : BaseSessionTest() {
    private fun sendLongPress(
        x: Float,
        y: Float,
    ) {
        val downTime = SystemClock.uptimeMillis()
        var eventTime = SystemClock.uptimeMillis()
        var event =
            MotionEvent.obtain(
                downTime,
                eventTime,
                MotionEvent.ACTION_DOWN,
                x,
                y,
                0,
            )
        mainSession.panZoomController.onTouchEvent(event)
    }

    private fun sendRightClickDown(
        x: Float,
        y: Float,
    ) {
        val downTime = SystemClock.uptimeMillis()
        var eventTime = SystemClock.uptimeMillis()

        var pp = arrayOf(MotionEvent.PointerProperties())
        pp[0].id = 0
        pp[0].toolType = MotionEvent.TOOL_TYPE_MOUSE

        var pc = arrayOf(MotionEvent.PointerCoords())
        pc[0].x = x
        pc[0].y = y
        pc[0].pressure = 1.0f
        pc[0].size = 1.0f

        var event =
            MotionEvent.obtain(
                downTime,
                eventTime,
                MotionEvent.ACTION_DOWN,
                // pointerCount
                1,
                pp,
                pc,
                // metaState
                0,
                MotionEvent.BUTTON_SECONDARY,
                // xPrecision
                1.0f,
                // yPrecision
                1.0f,
                // deviceId
                0,
                // edgeFlags
                0,
                InputDevice.SOURCE_MOUSE,
                // flags
                0,
            )
        mainSession.panZoomController.onTouchEvent(event)
    }

    private fun sendRightClickUp(
        x: Float,
        y: Float,
    ) {
        val downTime = SystemClock.uptimeMillis()
        var eventTime = SystemClock.uptimeMillis()

        var pp = arrayOf(MotionEvent.PointerProperties())
        pp[0].id = 0
        pp[0].toolType = MotionEvent.TOOL_TYPE_MOUSE

        var pc = arrayOf(MotionEvent.PointerCoords())
        pc[0].x = x
        pc[0].y = y
        pc[0].pressure = 1.0f
        pc[0].size = 1.0f

        var event =
            MotionEvent.obtain(
                downTime,
                eventTime,
                MotionEvent.ACTION_UP,
                // pointerCount
                1,
                pp,
                pc,
                // metaState
                0,
                // buttonState is unset in ACTION_UP
                // buttonState
                0,
                // xPrecision
                1.0f,
                // yPrecision
                1.0f,
                // deviceId
                0,
                // edgeFlags
                0,
                InputDevice.SOURCE_MOUSE,
                // flags
                0,
            )
        mainSession.panZoomController.onTouchEvent(event)
    }

    private fun simulateRightClick(
        x: Float,
        y: Float,
    ) {
        sendRightClickDown(x, y)
        sendRightClickUp(x, y)
    }

    private fun clearClipboard() {
        var clipboard =
            InstrumentationRegistry.getInstrumentation().targetContext.getSystemService(Context.CLIPBOARD_SERVICE)
                as ClipboardManager
        if (Build.VERSION.SDK_INT > Build.VERSION_CODES.P) {
            clipboard.clearPrimaryClip()
        } else {
            clipboard.setPrimaryClip(ClipData.newPlainText("", ""))
        }
    }

    private fun verifyActionMenuShown(expectedActions: Array<String>) {
        mainSession.waitUntilCalled(
            object : ContentDelegate, SelectionActionDelegate {
                @AssertCalled(false)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {}

                @AssertCalled
                override fun onShowActionRequest(
                    session: GeckoSession,
                    selection: SelectionActionDelegate.Selection,
                ) {
                    assertThat(
                        "Actions must be valid",
                        selection.availableActions.toTypedArray(),
                        arrayContainingInAnyOrder(*expectedActions),
                    )
                }
            },
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestContextMenuOnAudio() {
        mainSession.loadTestPath(CONTEXT_MENU_AUDIO_HTML_PATH)
        mainSession.waitForPageStop()
        sendLongPress(0f, 0f)

        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {
                    assertThat(
                        "Type should be audio.",
                        element.type,
                        equalTo(ContextElement.TYPE_AUDIO),
                    )
                    assertThat(
                        "The element source should be the mp3 file.",
                        element.srcUri,
                        endsWith("owl.mp3"),
                    )
                }
            },
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestContextMenuOnBlobBuffered() {
        mainSession.loadTestPath(CONTEXT_MENU_BLOB_BUFFERED_HTML_PATH)
        mainSession.waitForPageStop()
        mainSession.waitForRoundTrip()
        sendLongPress(50f, 50f)

        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {
                    assertThat(
                        "Type should be video.",
                        element.type,
                        equalTo(ContextElement.TYPE_VIDEO),
                    )
                    assertNull(
                        "Buffered blob should not have a srcUri.",
                        element.srcUri,
                    )
                }
            },
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestContextMenuOnBlobFull() {
        mainSession.loadTestPath(CONTEXT_MENU_BLOB_FULL_HTML_PATH)
        mainSession.waitForPageStop()
        mainSession.waitForRoundTrip()
        sendLongPress(50f, 50f)

        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {
                    assertThat(
                        "Type should be image.",
                        element.type,
                        equalTo(ContextElement.TYPE_IMAGE),
                    )
                    assertThat(
                        "Alternate text should match.",
                        element.altText,
                        equalTo("An orange circle."),
                    )
                    assertThat(
                        "The element source should begin with blob.",
                        element.srcUri,
                        startsWith("blob:"),
                    )
                }
            },
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestContextMenuOnImageNested() {
        mainSession.loadTestPath(CONTEXT_MENU_IMAGE_NESTED_HTML_PATH)
        mainSession.waitForPageStop()
        sendLongPress(50f, 50f)

        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {
                    assertThat(
                        "Type should be image.",
                        element.type,
                        equalTo(ContextElement.TYPE_IMAGE),
                    )
                    assertThat(
                        "Alternate text should match.",
                        element.altText,
                        equalTo("Test Image"),
                    )
                    assertThat(
                        "The element source should be the image file.",
                        element.srcUri,
                        endsWith("test.gif"),
                    )
                }
            },
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestContextMenuOnImage() {
        mainSession.loadTestPath(CONTEXT_MENU_IMAGE_HTML_PATH)
        mainSession.waitForPageStop()
        sendLongPress(50f, 50f)

        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {
                    assertThat(
                        "Type should be image.",
                        element.type,
                        equalTo(ContextElement.TYPE_IMAGE),
                    )
                    assertThat(
                        "Alternate text should match.",
                        element.altText,
                        equalTo("Test Image"),
                    )
                    assertThat(
                        "The element source should be the image file.",
                        element.srcUri,
                        endsWith("test.gif"),
                    )
                }
            },
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestContextMenuOnLink() {
        mainSession.loadTestPath(CONTEXT_MENU_LINK_HTML_PATH)
        mainSession.waitForPageStop()
        sendLongPress(50f, 50f)

        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {
                    assertThat(
                        "Type should be none.",
                        element.type,
                        equalTo(ContextElement.TYPE_NONE),
                    )
                    assertThat(
                        "The element link title should be the title of the anchor.",
                        element.title,
                        equalTo("Hello Link Title"),
                    )
                    assertThat(
                        "The element link URI should be the href of the anchor.",
                        element.linkUri,
                        endsWith("hello.html"),
                    )
                    assertThat(
                        "The element link text content should be the text content of the anchor.",
                        element.textContent,
                        equalTo("Hello World"),
                    )
                }
            },
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestContextMenuOnLinkText() {
        mainSession.loadTestPath(CONTEXT_MENU_LINK_TEXT_HTML_PATH)
        mainSession.waitForPageStop()
        sendLongPress(50f, 50f)

        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {
                    assertThat(
                        "The element link title should not exceed a maximum of 4096 chars.",
                        element.title?.length,
                        equalTo(4096),
                    )
                    assertThat(
                        "The element link alternative text should not exceed a maximum of 4096 chars.",
                        element.altText?.length,
                        equalTo(4096),
                    )
                    assertThat(
                        "The element link text content should not exceed a maximum of 4096 chars.",
                        element.textContent?.length,
                        equalTo(4096),
                    )
                }
            },
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestContextMenuOnVideo() {
        // Bug 1700243
        assumeThat(sessionRule.env.isIsolatedProcess, equalTo(false))
        mainSession.loadTestPath(CONTEXT_MENU_VIDEO_HTML_PATH)
        mainSession.waitForPageStop()
        sendLongPress(50f, 50f)

        mainSession.waitUntilCalled(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {
                    assertThat(
                        "Type should be video.",
                        element.type,
                        equalTo(ContextElement.TYPE_VIDEO),
                    )
                    assertThat(
                        "The element source should be the video file.",
                        element.srcUri,
                        endsWith("short.mp4"),
                    )
                }
            },
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestContextMenuOnLinkRightClickMouseUp() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                "ui.context_menus.after_mouseup" to true,
            ),
        )
        mainSession.loadTestPath(CONTEXT_MENU_LINK_HTML_PATH)
        mainSession.waitForPageStop()

        sendRightClickDown(50f, 50f)

        mainSession.delegateDuringNextWait(
            object : ContentDelegate {
                @AssertCalled(false)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {}
            },
        )

        sendRightClickUp(50f, 50f)

        mainSession.delegateUntilTestEnd(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {
                    assertThat(
                        "Type should be none.",
                        element.type,
                        equalTo(ContextElement.TYPE_NONE),
                    )
                    assertThat(
                        "The element link title should be the title of the anchor.",
                        element.title,
                        equalTo("Hello Link Title"),
                    )
                    assertThat(
                        "The element link URI should be the href of the anchor.",
                        element.linkUri,
                        endsWith("hello.html"),
                    )
                    assertThat(
                        "The element link text content should be the text content of the anchor.",
                        element.textContent,
                        equalTo("Hello World"),
                    )
                }
            },
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun requestContextMenuOnLinkRightClickMouseDown() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                "ui.context_menus.after_mouseup" to false,
            ),
        )
        mainSession.loadTestPath(CONTEXT_MENU_LINK_HTML_PATH)
        mainSession.waitForPageStop()

        sendRightClickDown(50f, 50f)

        mainSession.delegateDuringNextWait(
            object : ContentDelegate {
                @AssertCalled(count = 1)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {
                    assertThat(
                        "Type should be none.",
                        element.type,
                        equalTo(ContextElement.TYPE_NONE),
                    )
                    assertThat(
                        "The element link title should be the title of the anchor.",
                        element.title,
                        equalTo("Hello Link Title"),
                    )
                    assertThat(
                        "The element link URI should be the href of the anchor.",
                        element.linkUri,
                        endsWith("hello.html"),
                    )
                    assertThat(
                        "The element link text content should be the text content of the anchor.",
                        element.textContent,
                        equalTo("Hello World"),
                    )
                }
            },
        )

        sendRightClickUp(50f, 50f)

        mainSession.delegateUntilTestEnd(
            object : ContentDelegate {
                @AssertCalled(false)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {}
            },
        )
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun notRequestContextMenuWithPreventDefault() {
        mainSession.loadTestPath(CONTEXT_MENU_LINK_HTML_PATH)
        mainSession.waitForPageStop()

        val contextmenuEventPromise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve => {
                    document.documentElement.addEventListener('contextmenu', event => {
                        event.preventDefault();
                        resolve(true);
                    }, { once: true });
                });
                """.trimIndent(),
            )

        mainSession.delegateUntilTestEnd(
            object : ContentDelegate {
                @AssertCalled(false)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {
                }
            },
        )

        sendLongPress(50f, 50f)

        assertThat("contextmenu", contextmenuEventPromise.value as Boolean, equalTo(true))

        mainSession.waitForRoundTrip()
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun noContextMenuWithSelection() {
        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        // When right-click by mouse, we don't show context menu if there is a selection range.

        mainSession.evaluateJS(
            """
            window.getSelection().setBaseAndExtent(document.querySelector('p'), 0,
                                                   document.querySelector('p'), 1)
            """.trimIndent(),
        )

        simulateRightClick(50f, 50f)

        verifyActionMenuShown(
            arrayOf(
                ACTION_COPY,
                ACTION_HIDE,
                ACTION_SELECT_ALL,
                ACTION_UNSELECT,
            ),
        )

        // Calling preventDefault doesn't show action menu

        val contextmenuEventPromise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve =>
                    document.documentElement.addEventListener('contextmenu', event => {
                        event.preventDefault();
                        resolve();
                    }, { once: true })
                )
                """.trimIndent(),
            )

        simulateRightClick(50f, 50f)

        mainSession.delegateDuringNextWait(
            object : ContentDelegate, SelectionActionDelegate {
                @AssertCalled(false)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {}

                @AssertCalled(false)
                override fun onShowActionRequest(
                    session: GeckoSession,
                    selection: SelectionActionDelegate.Selection,
                ) {}
            },
        )

        contextmenuEventPromise.value
    }

    @WithDisplay(width = 100, height = 100)
    @Test
    fun noContextMenuWithSelectionOnEditable() {
        clearClipboard()

        mainSession.loadTestPath(HELLO_HTML_PATH)
        mainSession.waitForPageStop()

        // Create content editable

        val contentEditablePromise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(resolve => {
                    let p = document.querySelector('p');
                    p.style.width = '200px';
                    p.style.height = '200px';
                    p.contentEditable = true;
                    p.focus();
                    window.setTimeout(() => {
                        window.getSelection().setBaseAndExtent(p, 0, p, 1);
                        resolve();
                    }, 100);
                });
                """.trimIndent(),
            )

        contentEditablePromise.value

        simulateRightClick(50f, 50f)

        verifyActionMenuShown(
            arrayOf(
                ACTION_COLLAPSE_TO_START,
                ACTION_COLLAPSE_TO_END,
                ACTION_COPY,
                ACTION_CUT,
                ACTION_DELETE,
                ACTION_HIDE,
            ),
        )
    }

    @WithDisplay(width = 300, height = 300)
    @Test
    fun contextMenuOnTextControl() {
        clearClipboard()

        mainSession.loadTestPath(TEXT_CONTROL_PATH)
        mainSession.waitForPageStop()

        // Click text control with selection shows action menu

        mainSession.evaluateJS(
            """
            document.querySelector('input[type=text]').focus();
            document.querySelector('input[type=text]').setSelectionRange(0, 100);
            """.trimIndent(),
        )

        simulateRightClick(50f, 50f)

        verifyActionMenuShown(
            arrayOf(
                ACTION_COLLAPSE_TO_START,
                ACTION_COLLAPSE_TO_END,
                ACTION_COPY,
                ACTION_CUT,
                ACTION_DELETE,
                ACTION_HIDE,
            ),
        )

        // Click non-text control doesn't show action menu and context menu

        val contextmenuEventPromise =
            mainSession.evaluatePromiseJS(
                """
                new Promise(
                    resolve => document.documentElement.addEventListener('contextmenu', resolve, { once: true }))
                """.trimIndent(),
            )

        sendRightClickDown(250f, 10f)
        sendRightClickUp(250f, 10f)

        mainSession.delegateDuringNextWait(
            object : ContentDelegate, SelectionActionDelegate {
                @AssertCalled(false)
                override fun onContextMenu(
                    session: GeckoSession,
                    screenX: Int,
                    screenY: Int,
                    element: ContextElement,
                ) {}

                @AssertCalled(false)
                override fun onShowActionRequest(
                    session: GeckoSession,
                    selection: SelectionActionDelegate.Selection,
                ) {}
            },
        )

        contextmenuEventPromise.value
    }
}
