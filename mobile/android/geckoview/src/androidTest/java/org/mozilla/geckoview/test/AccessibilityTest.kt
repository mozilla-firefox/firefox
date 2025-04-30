/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test

import android.graphics.Rect
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.text.InputType
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import android.view.accessibility.AccessibilityNodeInfo.CHECKED_STATE_FALSE
import android.view.accessibility.AccessibilityNodeInfo.CHECKED_STATE_TRUE
import android.view.accessibility.AccessibilityNodeProvider
import android.view.accessibility.AccessibilityRecord
import android.widget.EditText
import android.widget.FrameLayout
import androidx.test.ext.junit.rules.ActivityScenarioRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.MediumTest
import androidx.test.platform.app.InstrumentationRegistry
import org.hamcrest.Matchers.* // ktlint-disable no-wildcard-imports
import org.junit.After
import org.junit.Assume.assumeThat
import org.junit.Before
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith
import org.mozilla.geckoview.AllowOrDeny
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.AssertCalled
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.ShouldContinue
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.WithDisplay

const val DISPLAY_WIDTH = 480
const val DISPLAY_HEIGHT = 640

@RunWith(AndroidJUnit4::class)
@MediumTest
@WithDisplay(width = DISPLAY_WIDTH, height = DISPLAY_HEIGHT)
class AccessibilityTest : BaseSessionTest() {
    lateinit var view: View
    val screenRect = Rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT)
    val provider: AccessibilityNodeProvider get() = view.accessibilityNodeProvider
    private val nodeInfos = mutableListOf<AccessibilityNodeInfo>()
    private val activityRule = ActivityScenarioRule(GeckoViewTestActivity::class.java)

    @get:Rule
    override val rules: RuleChain = RuleChain.outerRule(activityRule).around(sessionRule)

    // Given a child ID, return the virtual descendent ID.
    private fun getVirtualDescendantId(childId: Long): Int {
        try {
            val getVirtualDescendantIdMethod =
                AccessibilityNodeInfo::class.java.getMethod("getVirtualDescendantId", Long::class.java)
            val virtualDescendantId = getVirtualDescendantIdMethod.invoke(null, childId) as Int
            return if (virtualDescendantId == Int.MAX_VALUE) -1 else virtualDescendantId
        } catch (ex: Exception) {
            return 0
        }
    }

    // Retrieve the virtual descendent ID of the event's source.
    private fun getSourceId(event: AccessibilityEvent): Int {
        try {
            val getSourceIdMethod =
                AccessibilityRecord::class.java.getMethod("getSourceNodeId")
            return getVirtualDescendantId(getSourceIdMethod.invoke(event) as Long)
        } catch (ex: Exception) {
            return 0
        }
    }

    private fun createNodeInfo(id: Int): AccessibilityNodeInfo {
        val node = provider.createAccessibilityNodeInfo(id)
        nodeInfos.add(node!!)
        return node
    }

    // Get a child ID by index.
    private fun AccessibilityNodeInfo.getChildId(index: Int): Int {
        try {
            val field = AccessibilityNodeInfo::class.java.getDeclaredField("mChildNodeIds")
            field.setAccessible(true)
            val id = Class.forName("android.util.LongArray").getMethod("get", Int::class.java).invoke(field.get(this), index) as Long
            return getVirtualDescendantId(id)
        } catch (ex: Exception) {
            return getVirtualDescendantId(
                AccessibilityNodeInfo::class.java.getMethod(
                    "getChildId",
                    Int::class.java,
                ).invoke(this, index) as Long,
            )
        }
    }

    private interface EventDelegate {
        fun onAccessibilityFocused(event: AccessibilityEvent) { }
        fun onAccessibilityFocusCleared(event: AccessibilityEvent) { }
        fun onClicked(event: AccessibilityEvent) { }
        fun onFocused(event: AccessibilityEvent) { }
        fun onSelected(event: AccessibilityEvent) { }
        fun onScrolled(event: AccessibilityEvent) { }
        fun onTextSelectionChanged(event: AccessibilityEvent) { }
        fun onTextChanged(event: AccessibilityEvent) { }
        fun onTextTraversal(event: AccessibilityEvent) { }
        fun onWinContentChanged(event: AccessibilityEvent) { }
        fun onWinStateChanged(event: AccessibilityEvent) { }
        fun onAnnouncement(event: AccessibilityEvent) { }
    }

    @Before fun setup() {
        // We initialize a view with a parent and grandparent so that the
        // accessibility events propagate up at least to the parent.
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        view = FrameLayout(context)
        FrameLayout(context).addView(view)
        FrameLayout(context).addView(view.parent as View)

        // Force on accessibility and assign the session's accessibility
        // object a view.
        sessionRule.runtime.settings.forceEnableAccessibility = true
        mainSession.accessibility.view = view

        // Set up an external delegate that will intercept accessibility events.
        @Suppress("DEPRECATION") // TYPE_ANNOUNCEMENT is deprecated but triggered by live regions
        sessionRule.addExternalDelegateUntilTestEnd(
            EventDelegate::class,
            { newDelegate ->
                (view.parent as View).setAccessibilityDelegate(object : View.AccessibilityDelegate() {
                    override fun onRequestSendAccessibilityEvent(host: ViewGroup, child: View, event: AccessibilityEvent): Boolean {
                        when (event.eventType) {
                            AccessibilityEvent.TYPE_VIEW_FOCUSED -> newDelegate.onFocused(event)
                            AccessibilityEvent.TYPE_VIEW_CLICKED -> newDelegate.onClicked(event)
                            AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED -> newDelegate.onAccessibilityFocused(event)
                            AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED -> newDelegate.onAccessibilityFocusCleared(event)
                            AccessibilityEvent.TYPE_VIEW_SELECTED -> newDelegate.onSelected(event)
                            AccessibilityEvent.TYPE_VIEW_SCROLLED -> newDelegate.onScrolled(event)
                            AccessibilityEvent.TYPE_VIEW_TEXT_SELECTION_CHANGED -> newDelegate.onTextSelectionChanged(event)
                            AccessibilityEvent.TYPE_VIEW_TEXT_CHANGED -> newDelegate.onTextChanged(event)
                            AccessibilityEvent.TYPE_VIEW_TEXT_TRAVERSED_AT_MOVEMENT_GRANULARITY -> newDelegate.onTextTraversal(event)
                            AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED -> newDelegate.onWinContentChanged(event)
                            AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED -> newDelegate.onWinStateChanged(event)
                            AccessibilityEvent.TYPE_ANNOUNCEMENT -> newDelegate.onAnnouncement(event)
                            else -> {}
                        }
                        return false
                    }
                })
            },
            { (view.parent as View).setAccessibilityDelegate(null) },
            object : EventDelegate { },
        )
    }

    @After fun teardown() {
        sessionRule.runtime.settings.forceEnableAccessibility = false
        mainSession.accessibility.view = null
        if (Build.VERSION.SDK_INT < 33) {
            nodeInfos.forEach { node ->
                @Suppress("DEPRECATION")
                node.recycle()
            }
        }
    }

    private fun waitForInitialFocus(moveToFirstChild: Boolean = false) {
        sessionRule.waitUntilCalled(object : GeckoSession.NavigationDelegate {
            override fun onLoadRequest(
                session: GeckoSession,
                request: GeckoSession.NavigationDelegate.LoadRequest,
            ): GeckoResult<AllowOrDeny>? {
                return GeckoResult.allow()
            }
        })
        // XXX: Sometimes we get the window state change of the initial
        // about:blank page loading. Need to figure out how to ignore that.
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onFocused(event: AccessibilityEvent) { }

            @AssertCalled
            override fun onWinStateChanged(event: AccessibilityEvent) { }

            @AssertCalled
            override fun onWinContentChanged(event: AccessibilityEvent) { }
        })

        if (moveToFirstChild) {
            provider.performAction(
                View.NO_ID,
                AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT,
                null,
            )
        }
    }

    @Test fun testRootNode() {
        assertThat("provider is not null", provider, notNullValue())
        val node = createNodeInfo(AccessibilityNodeProvider.HOST_VIEW_ID)
        assertThat(
            "Root node should have WebView class name",
            node.className.toString(),
            equalTo("android.webkit.WebView"),
        )
    }

    @Test fun testPageLoad() {
        mainSession.loadTestPath(INPUTS_PATH)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onFocused(event: AccessibilityEvent) { }
        })
    }

    @Test fun testAccessibilityFocusAboutMozilla() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        mainSession.loadUri("about:license")

        sessionRule.waitUntilCalled(object : GeckoSession.NavigationDelegate {
            override fun onLoadRequest(
                session: GeckoSession,
                request: GeckoSession.NavigationDelegate.LoadRequest,
            ): GeckoResult<AllowOrDeny>? {
                return GeckoResult.allow()
            }
        })

        // XXX: Local pages do not dispatch focus events when loaded
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled
            override fun onWinStateChanged(event: AccessibilityEvent) { }

            @AssertCalled
            override fun onWinContentChanged(event: AccessibilityEvent) { }
        })

        provider.performAction(
            View.NO_ID,
            AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT,
            null,
        )

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Header is a11y focused",
                    node.contentDescription.toString(),
                    equalTo("Licenses"),
                )
            }
        })

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT,
            null,
        )

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Next text leaf is focused",
                    node.text.toString(),
                    equalTo("All of the "),
                )
            }
        })

        val bundle = Bundle()
        bundle.putString(AccessibilityNodeInfo.ACTION_ARGUMENT_HTML_ELEMENT_STRING, "LINK")

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, bundle)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on a with href",
                    node.contentDescription as String,
                    equalTo("free"),
                )
            }
        })
    }

    @Test fun testAccessibilityFocus() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        mainSession.loadTestPath(INPUTS_PATH)
        waitForInitialFocus(true)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Label accessibility focused",
                    node.className.toString(),
                    equalTo("android.view.View"),
                )
                assertThat("Text node should not be focusable", node.isFocusable, equalTo(false))
                assertThat("Text node should be a11y focused", node.isAccessibilityFocused, equalTo(true))
                assertThat("Text node should not be clickable", node.isClickable, equalTo(false))
            }
        })

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT,
            null,
        )

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Editbox accessibility focused",
                    node.className.toString(),
                    equalTo("android.widget.EditText"),
                )
                assertThat("Entry node should be focusable", node.isFocusable, equalTo(true))
                assertThat("Entry node should be a11y focused", node.isAccessibilityFocused, equalTo(true))
                assertThat("Entry node should be clickable", node.isClickable, equalTo(true))
            }
        })

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS,
            null,
        )

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocusCleared(event: AccessibilityEvent) {
                assertThat("Accessibility focused node is now cleared", getSourceId(event), equalTo(nodeId))
                val node = createNodeInfo(nodeId)
                assertThat("Entry node should node be a11y focused", node.isAccessibilityFocused, equalTo(false))
            }
        })
    }

    fun loadTestPage(page: String) {
        mainSession.loadTestPath("/assets/www/accessibility/$page.html")
    }

    @Test fun testTextEntryNode() {
        loadTestPage("test-text-entry-node")
        waitForInitialFocus()

        mainSession.evaluateJS("document.querySelector('input[aria-label=Naame]').focus()")

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onFocused(event: AccessibilityEvent) {
                val nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Focused EditBox",
                    node.className.toString(),
                    equalTo("android.widget.EditText"),
                )
                assertThat(
                    "Hint has field name",
                    node.extras.getString("AccessibilityNodeInfo.hint"),
                    equalTo("Naame description"),
                )
            }
        })

        mainSession.evaluateJS("document.querySelector('input[aria-label=Last]').focus()")

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onFocused(event: AccessibilityEvent) {
                val nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Focused EditBox",
                    node.className.toString(),
                    equalTo("android.widget.EditText"),
                )
                assertThat(
                    "Hint has field name",
                    node.extras.getString("AccessibilityNodeInfo.hint"),
                    equalTo("Last, required"),
                )
            }
        })
    }

    @Test fun testMoveCaretAccessibilityFocus() {
        loadTestPage("test-move-caret-accessibility-focus")
        waitForInitialFocus(false)

        mainSession.evaluateJS(
            """
            this.select = function select(node, start, end) {
                let r = new Range();
                r.setStart(node, start);
                r.setEnd(node, end);
                let s = getSelection();
                s.removeAllRanges();
                s.addRange(r);
            };
            this.select(document.querySelector('p').childNodes[2], 2, 6);
            """.trimIndent(),
        )

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                val node = createNodeInfo(getSourceId(event))
                assertThat("Text node should match text", node.text as String, equalTo(", sweet "))
            }
        })

        mainSession.evaluateJS(
            """
            this.select(document.querySelector('p').lastElementChild.firstChild, 1, 2);
            """.trimIndent(),
        )

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                val node = createNodeInfo(getSourceId(event))
                assertThat("Text node should match text", node.text as String, equalTo("world"))
            }
        })

        // This focuses the link.
        mainSession.finder.find("sweet", 0)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                val node = createNodeInfo(getSourceId(event))
                assertThat("Text node should match text", node.contentDescription as String, equalTo("sweet"))
            }
        })

        // reset caret position
        mainSession.evaluateJS(
            """
            this.select(document.body, 0, 0);
            // Changing DOM selection doesn't focus the document! Force focus
            // here so we can use that to determine when this is done.
            document.activeElement.blur();
            """.trimIndent(),
        )
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onFocused(event: AccessibilityEvent) {}
        })

        mainSession.finder.find("Hell", 0)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                val node = createNodeInfo(getSourceId(event))
                assertThat("Text node should match text", node.text as String, equalTo("Hello "))
            }
        })
    }

    private fun waitUntilTextSelectionChanged(fromIndex: Int, toIndex: Int, text: String) {
        var eventFromIndex = -1
        var eventToIndex = -1
        var eventText = ""
        do {
            sessionRule.waitUntilCalled(object : EventDelegate {
                override fun onTextSelectionChanged(event: AccessibilityEvent) {
                    eventFromIndex = event.fromIndex
                    eventToIndex = event.toIndex
                    eventText = event.text[0].toString()
                }
            })
        } while (fromIndex != eventFromIndex || toIndex != eventToIndex)
        assertThat("text selection event text matches", eventText, equalTo(text))
    }

    private fun waitUntilTextTraversed(
        fromIndex: Int,
        toIndex: Int,
        expectedNode: Int? = null,
    ): Int {
        var nodeId: Int = AccessibilityNodeProvider.HOST_VIEW_ID
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onTextTraversal(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                if (expectedNode != null) {
                    assertThat("Node matches", nodeId, equalTo(expectedNode))
                }
                assertThat("fromIndex matches", event.fromIndex, equalTo(fromIndex))
                assertThat("toIndex matches", event.toIndex, equalTo(toIndex))
            }
        })
        return nodeId
    }

    private fun waitUntilClick(checked: Boolean) {
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onClicked(event: AccessibilityEvent) {
                var nodeId = getSourceId(event)
                var node = createNodeInfo(nodeId)
                assertThat("Event's checked state matches", event.isChecked, equalTo(checked))

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.BAKLAVA) {
                    val checkedState = if (checked) CHECKED_STATE_TRUE else CHECKED_STATE_FALSE
                    assertThat("Checkbox node has correct checked state", node.checked, equalTo(checkedState))
                } else {
                    @Suppress("DEPRECATION")
                    assertThat("Checkbox node has correct checked state", node.isChecked, equalTo(checked))
                }
            }
        })
    }

    private fun waitUntilSelect(selected: Boolean) {
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onSelected(event: AccessibilityEvent) {
                var nodeId = getSourceId(event)
                var node = createNodeInfo(nodeId)
                assertThat("Selectable node has correct selected state", node.isSelected, equalTo(selected))
            }
        })
    }

    private fun setSelectionArguments(start: Int, end: Int): Bundle {
        val arguments = Bundle(2)
        arguments.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_START_INT, start)
        arguments.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_END_INT, end)
        return arguments
    }

    private fun moveByGranularityArguments(granularity: Int, extendSelection: Boolean = false): Bundle {
        val arguments = Bundle(2)
        arguments.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT, granularity)
        arguments.putBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN, extendSelection)
        return arguments
    }

    @Test fun testClipboard() {
        // disabled for having over 120+ failures in the last 7 days - turned permafailing on Bug 1837126
        assumeThat(sessionRule.env.isDebugBuild, equalTo(true))
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            // Writing clipboard requires foreground on Android 10.
            activityRule.scenario?.onActivity { activity ->
                activity.onWindowFocusChanged(true)
            }
        }

        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        loadTestPage("test-clipboard")
        waitForInitialFocus()

        mainSession.evaluateJS("document.querySelector('input').focus()")

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Focused EditBox",
                    node.className.toString(),
                    equalTo("android.widget.EditText"),
                )
            }

            @AssertCalled(count = 1)
            override fun onTextSelectionChanged(event: AccessibilityEvent) {
                assertThat("fromIndex should be at start", event.fromIndex, equalTo(0))
                assertThat("toIndex should be at start", event.toIndex, equalTo(0))
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SET_SELECTION, setSelectionArguments(5, 11))
        waitUntilTextSelectionChanged(5, 11, "hello cruel world")

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_COPY, null)

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SET_SELECTION, setSelectionArguments(11, 11))
        waitUntilTextSelectionChanged(11, 11, "hello cruel world")

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_PASTE, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onTextChanged(event: AccessibilityEvent) {
                assertThat("text should be pasted", event.text[0].toString(), equalTo("hello cruel cruel world"))
                assertThat("fromIndex is correct", event.fromIndex, equalTo(12))
                assertThat("addedCount is correct", event.addedCount, equalTo(6))
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SET_SELECTION, setSelectionArguments(17, 23))
        waitUntilTextSelectionChanged(17, 23, "hello cruel cruel world")

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_PASTE, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled
            override fun onTextChanged(event: AccessibilityEvent) {
                assertThat("text should be pasted", event.text[0].toString(), equalTo("hello cruel cruel cruel"))
                assertThat("fromIndex is correct", event.fromIndex, equalTo(18))
                assertThat("removedCount is correct", event.removedCount, equalTo(5))
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SET_SELECTION, setSelectionArguments(0, 0))
        waitUntilTextSelectionChanged(0, 0, "hello cruel cruel cruel")

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD, true),
        )
        waitUntilTextSelectionChanged(0, 5, "hello cruel cruel cruel")

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_CUT, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled
            override fun onTextChanged(event: AccessibilityEvent) {
                assertThat("text should be cut", event.text[0].toString(), equalTo(" cruel cruel cruel"))
                assertThat("fromIndex is correct", event.fromIndex, equalTo(0))
                assertThat("removedCount is correct", event.removedCount, equalTo(5))
            }
        })
    }

    @Test fun testMoveByCharacter() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        mainSession.loadTestPath(LOREM_IPSUM_HTML_PATH)
        waitForInitialFocus(true)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat("Accessibility focus on first text leaf", node.text as String, startsWith("Lorem ipsum"))
            }
        })

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER),
        )
        waitUntilTextTraversed(0, 1, nodeId) // "L"

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER),
        )
        waitUntilTextTraversed(1, 2, nodeId) // "o"

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER),
        )
        waitUntilTextTraversed(0, 1, nodeId) // "L"
    }

    @Test fun testMoveByWord() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        mainSession.loadTestPath(LOREM_IPSUM_HTML_PATH)
        waitForInitialFocus(true)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat("Accessibility focus on first text leaf", node.text as String, startsWith("Lorem ipsum"))
            }
        })

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD),
        )
        waitUntilTextTraversed(0, 5, nodeId) // "Lorem"

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD),
        )
        waitUntilTextTraversed(6, 11, nodeId) // "ipsum"

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD),
        )
        waitUntilTextTraversed(0, 5, nodeId) // "Lorem"
    }

    @Test fun testMoveByLine() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        mainSession.loadTestPath(LOREM_IPSUM_HTML_PATH)
        waitForInitialFocus(true)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat("Accessibility focus on first text leaf", node.text as String, startsWith("Lorem ipsum"))
            }
        })

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_LINE),
        )
        waitUntilTextTraversed(0, 18, nodeId) // "Lorem ipsum dolor "

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_LINE),
        )
        waitUntilTextTraversed(18, 28, nodeId) // "sit amet, "

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_LINE),
        )
        waitUntilTextTraversed(0, 18, nodeId) // "Lorem ipsum dolor "
    }

    @Test fun testMoveByCharacterAtEdges() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        mainSession.loadTestPath(LOREM_IPSUM_HTML_PATH)
        waitForInitialFocus()

        // Move to the first link containing "anim id".
        val bundle = Bundle()
        bundle.putString(AccessibilityNodeInfo.ACTION_ARGUMENT_HTML_ELEMENT_STRING, "LINK")
        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, bundle)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat("Accessibility focus on link", node.contentDescription as String, startsWith("anim id"))
            }
        })

        var success: Boolean
        // Navigate forward through "anim id" character by character.
        for (start in 0..6) {
            success = provider.performAction(
                nodeId,
                AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
                moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER),
            )
            assertThat("Next char should succeed", success, equalTo(true))
            waitUntilTextTraversed(start, start + 1, nodeId)
        }

        // Try to navigate forward past end.
        success = provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER),
        )
        assertThat("Next char should fail at end", success, equalTo(false))

        // We're already on "d". Navigate backward through "anim i".
        for (start in 5 downTo 0) {
            success = provider.performAction(
                nodeId,
                AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY,
                moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER),
            )
            assertThat("Prev char should succeed", success, equalTo(true))
            waitUntilTextTraversed(start, start + 1, nodeId)
        }

        // Try to navigate backward past start.
        success = provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER),
        )
        assertThat("Prev char should fail at start", success, equalTo(false))
    }

    @Test fun testMoveByWordAtEdges() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        mainSession.loadTestPath(LOREM_IPSUM_HTML_PATH)
        waitForInitialFocus()

        // Move to the first link containing "anim id".
        val bundle = Bundle()
        bundle.putString(AccessibilityNodeInfo.ACTION_ARGUMENT_HTML_ELEMENT_STRING, "LINK")
        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, bundle)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat("Accessibility focus on link", node.contentDescription as String, startsWith("anim id"))
            }
        })

        var success: Boolean
        success = provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD),
        )
        assertThat("Next word should succeed", success, equalTo(true))
        waitUntilTextTraversed(0, 4, nodeId) // "anim"

        success = provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD),
        )
        assertThat("Next word should succeed", success, equalTo(true))
        waitUntilTextTraversed(5, 7, nodeId) // "id"

        // Try to navigate forward past end.
        success = provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD),
        )
        assertThat("Next word should fail at end", success, equalTo(false))

        success = provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD),
        )
        assertThat("Prev word should succeed", success, equalTo(true))
        waitUntilTextTraversed(0, 4, nodeId) // "anim"

        // Try to navigate backward past start.
        success = provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD),
        )
        assertThat("Prev word should fail at start", success, equalTo(false))
    }

    @Test fun testMoveAtEndOfTextTrailingWhitespace() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        mainSession.loadTestPath(LOREM_IPSUM_HTML_PATH)
        waitForInitialFocus(true)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat("Accessibility focus on first text leaf", node.text as String, startsWith("Lorem ipsum"))
            }
        })

        // Initial move backward to move to last word.
        var success = provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD),
        )
        assertThat("Prev word should succeed", success, equalTo(true))
        waitUntilTextTraversed(418, 424, nodeId) // "mollit"

        // Try to move forward past last word.
        success = provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD),
        )
        assertThat("Next word should fail at last word", success, equalTo(false))

        // Move forward by character (onto trailing space).
        success = provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER),
        )
        assertThat("Next char should succeed", success, equalTo(true))
        waitUntilTextTraversed(424, 425, nodeId) // " "

        // Try to move forward past last character.
        success = provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY,
            moveByGranularityArguments(AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER),
        )
        assertThat("Next char should fail at last char", success, equalTo(false))
    }

    @Test fun testHeadings() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        loadTestPage("test-headings")
        waitForInitialFocus()

        val bundle = Bundle()
        bundle.putString(AccessibilityNodeInfo.ACTION_ARGUMENT_HTML_ELEMENT_STRING, "HEADING")

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, bundle)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat("Accessibility focus on first heading", node.contentDescription as String, startsWith("Fried cheese"))
                assertThat(
                    "First heading is level 1",
                    node.extras.getCharSequence("AccessibilityNodeInfo.roleDescription")!!.toString(),
                    equalTo("heading level 1"),
                )
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, bundle)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat("Accessibility focus on second heading", node.contentDescription as String, startsWith("Popcorn shrimp"))
                assertThat(
                    "Second heading is level 2",
                    node.extras.getCharSequence("AccessibilityNodeInfo.roleDescription")!!.toString(),
                    equalTo("heading level 2"),
                )
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, bundle)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat("Accessibility focus on second heading", node.contentDescription as String, startsWith("Chicken fingers"))
                assertThat(
                    "Third heading is level 3",
                    node.extras.getCharSequence("AccessibilityNodeInfo.roleDescription")!!.toString(),
                    equalTo("heading level 3"),
                )
            }
        })
    }

    @Test fun testCheckbox() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        loadTestPage("test-checkbox")
        waitForInitialFocus(true)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                var node = createNodeInfo(nodeId)
                assertThat("Checkbox node is checkable", node.isCheckable, equalTo(true))
                assertThat("Checkbox node is clickable", node.isClickable, equalTo(true))
                assertThat("Checkbox node is focusable", node.isFocusable, equalTo(true))

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.BAKLAVA) {
                    assertThat("Checkbox node is not checked", node.checked, equalTo(CHECKED_STATE_FALSE))
                } else {
                    @Suppress("DEPRECATION")
                    assertThat("Checkbox node is not checked", node.isChecked, equalTo(false))
                }

                assertThat("Checkbox node has correct role", node.text.toString(), equalTo("many option"))
                assertThat(
                    "Hint has description",
                    node.extras.getString("AccessibilityNodeInfo.hint"),
                    equalTo("description"),
                )
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_CLICK, null)
        waitUntilClick(true)

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_CLICK, null)
        waitUntilClick(false)
    }

    @Test fun testExpandable() {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        loadTestPage("test-expandable")
        waitForInitialFocus(true)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat("button is expandable", node.actionList, hasItem(AccessibilityNodeInfo.AccessibilityAction.ACTION_EXPAND))
                assertThat("button is not collapsable", node.actionList, not(hasItem(AccessibilityNodeInfo.AccessibilityAction.ACTION_COLLAPSE)))
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_EXPAND, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onClicked(event: AccessibilityEvent) {
                assertThat("Clicked event is from same node", getSourceId(event), equalTo(nodeId))
                val node = createNodeInfo(nodeId)
                assertThat("button is collapsable", node.actionList, hasItem(AccessibilityNodeInfo.AccessibilityAction.ACTION_COLLAPSE))
                assertThat("button is not expandable", node.actionList, not(hasItem(AccessibilityNodeInfo.AccessibilityAction.ACTION_EXPAND)))
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_COLLAPSE, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onClicked(event: AccessibilityEvent) {
                assertThat("Clicked event is from same node", getSourceId(event), equalTo(nodeId))
                val node = createNodeInfo(nodeId)
                assertThat("button is expandable", node.actionList, hasItem(AccessibilityNodeInfo.AccessibilityAction.ACTION_EXPAND))
                assertThat("button is not collapsable", node.actionList, not(hasItem(AccessibilityNodeInfo.AccessibilityAction.ACTION_COLLAPSE)))
            }
        })
    }

    @Test fun testSelectable() {
        var nodeId = View.NO_ID
        loadTestPage("test-selectable")
        waitForInitialFocus(true)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                var node = createNodeInfo(nodeId)
                assertThat("Selectable node is clickable", node.isClickable, equalTo(true))
                assertThat("Selectable node is not selected", node.isSelected, equalTo(false))
                assertThat("Selectable node has correct text", node.text.toString(), equalTo("1"))
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_CLICK, null)
        waitUntilSelect(true)

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_CLICK, null)
        waitUntilSelect(false)

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SELECT, null)
        waitUntilSelect(true)

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SELECT, null)
        waitUntilSelect(false)

        // Ensure that querying an option outside of a selectable container
        // doesn't crash (bug 1801879).
        mainSession.evaluateJS("document.getElementById('outsideSelectable').focus()")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                val nodeChild = createNodeInfo(node.getChildId(0))
                assertThat("Focused outsideSelectable", nodeChild.text.toString(), equalTo("outside selectable "))
            }
        })
    }

    @Test fun testMutation() {
        loadTestPage("test-mutation")
        waitForInitialFocus()

        val rootNode = createNodeInfo(View.NO_ID)
        assertThat("Document has 1 child", rootNode.childCount, equalTo(1))

        assertThat(
            "Section has 1 child",
            createNodeInfo(rootNode.getChildId(0)).childCount,
            equalTo(1),
        )
        mainSession.evaluateJS("document.querySelector('#to_show').style.display = 'none';")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 0)
            override fun onAnnouncement(event: AccessibilityEvent) { }

            @AssertCalled(count = 1)
            override fun onWinContentChanged(event: AccessibilityEvent) { }
        })

        assertThat(
            "Section has no children",
            createNodeInfo(rootNode.getChildId(0)).childCount,
            equalTo(0),
        )
    }

    @Test fun testLiveRegion() {
        loadTestPage("test-live-region")
        waitForInitialFocus()

        val rootNode = createNodeInfo(View.NO_ID)
        assertThat("Document has 1 child", rootNode.childCount, equalTo(1))

        val liveRegion = createNodeInfo(rootNode.getChildId(0))
        assertThat("First node is a label", liveRegion.viewIdResourceName.toString(), equalTo("to_change"))

        mainSession.evaluateJS("document.querySelector('#to_change').textContent = 'Hello';")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAnnouncement(event: AccessibilityEvent) {
                assertThat("Announcement is correct", event.text[0].toString(), equalTo("Hello"))
            }

            @AssertCalled(count = 1)
            override fun onWinContentChanged(event: AccessibilityEvent) { }
        })
    }

    @Test fun testLiveRegionDescendant() {
        loadTestPage("test-live-region-descendant")
        waitForInitialFocus()

        mainSession.evaluateJS("document.querySelector('#to_show').style.display = 'none';")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 0)
            override fun onAnnouncement(event: AccessibilityEvent) { }

            @AssertCalled(count = 1)
            override fun onWinContentChanged(event: AccessibilityEvent) { }
        })

        mainSession.evaluateJS("document.querySelector('#to_show').style.display = 'block';")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAnnouncement(event: AccessibilityEvent) {
                assertThat("Announcement is correct", event.text[0].toString(), equalTo("I will be shown"))
            }

            @AssertCalled(count = 1)
            override fun onWinContentChanged(event: AccessibilityEvent) { }
        })
    }

    @Test fun testLiveRegionAtomic() {
        loadTestPage("test-live-region-atomic")
        waitForInitialFocus()

        mainSession.evaluateJS("document.querySelector('p').textContent = '4pm';")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAnnouncement(event: AccessibilityEvent) {
                assertThat("Announcement is correct", event.text[0].toString(), equalTo("The time is 4pm"))
            }

            @AssertCalled(count = 1)
            override fun onWinContentChanged(event: AccessibilityEvent) { }
        })

        mainSession.evaluateJS(
            "document.querySelector('#container').removeAttribute('aria-atomic');" +
                "document.querySelector('p').textContent = '5pm';",
        )
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAnnouncement(event: AccessibilityEvent) {
                assertThat("Announcement is correct", event.text[0].toString(), equalTo("5pm"))
            }

            @AssertCalled(count = 1)
            override fun onWinContentChanged(event: AccessibilityEvent) { }
        })
    }

    @Test fun testLiveRegionImage() {
        loadTestPage("test-live-region-image")
        waitForInitialFocus()

        mainSession.evaluateJS("document.querySelector('img').alt = 'sad';")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAnnouncement(event: AccessibilityEvent) {
                assertThat("Announcement is correct", event.text[0].toString(), equalTo("This picture is sad"))
            }
        })
    }

    @Test fun testLiveRegionImageLabeledBy() {
        loadTestPage("test-live-region-image-labeled-by")
        waitForInitialFocus()

        mainSession.evaluateJS("document.querySelector('img').setAttribute('aria-labelledby', 'l2');")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAnnouncement(event: AccessibilityEvent) {
                assertThat("Announcement is correct", event.text[0].toString(), equalTo("Goodbye"))
            }
        })
    }

    @Test fun testLiveRegionStatus() {
        loadTestPage("test-live-region-status")
        waitForInitialFocus()

        mainSession.evaluateJS("document.querySelector('#status').textContent = 'hello';")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAnnouncement(event: AccessibilityEvent) {
                assertThat("Announcement is correct", event.text[0].toString(), equalTo("hello"))
            }
        })
    }

    private fun screenContainsNode(nodeId: Int): Boolean {
        var node = createNodeInfo(nodeId)
        var nodeBounds = Rect()
        node.getBoundsInScreen(nodeBounds)
        return screenRect.contains(nodeBounds)
    }

    @Ignore // Bug 1506276 - We need to reliably wait for APZC here, and it's not trivial.
    @Test
    fun testScroll() {
        var nodeId = View.NO_ID
        loadTestPage("test-scroll.html")

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled
            override fun onWinStateChanged(event: AccessibilityEvent) { }

            @AssertCalled(count = 1)
            @Suppress("deprecation")
            override fun onFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                var node = createNodeInfo(nodeId)
                var nodeBounds = Rect()
                node.getBoundsInParent(nodeBounds)
                assertThat("Default root node bounds are correct", nodeBounds, equalTo(screenRect))
            }
        })

        provider.performAction(View.NO_ID, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1, order = [1])
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                assertThat("Focused node is onscreen", screenContainsNode(nodeId), equalTo(true))
            }

            @AssertCalled(count = 1, order = [2])
            override fun onScrolled(event: AccessibilityEvent) {
                assertThat("View is scrolled for focused node to be onscreen", event.scrollY, greaterThan(0))
                assertThat("View is not scrolled to the end", event.scrollY, lessThan(event.maxScrollY))
            }

            @AssertCalled(count = 1, order = [3])
            override fun onWinContentChanged(event: AccessibilityEvent) {
                assertThat("Focused node is onscreen", screenContainsNode(nodeId), equalTo(true))
            }
        })

        SystemClock.sleep(100)
        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SCROLL_FORWARD, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1, order = [1])
            override fun onScrolled(event: AccessibilityEvent) {
                assertThat("View is scrolled to the end", event.scrollY.toDouble(), closeTo(event.maxScrollY.toDouble(), 1.0))
            }

            @AssertCalled(count = 1, order = [2])
            override fun onWinContentChanged(event: AccessibilityEvent) {
                assertThat("Focused node is still onscreen", screenContainsNode(nodeId), equalTo(true))
            }
        })

        SystemClock.sleep(100)
        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1, order = [1])
            override fun onScrolled(event: AccessibilityEvent) {
                assertThat("View is scrolled to the beginning", event.scrollY, equalTo(0))
            }

            @AssertCalled(count = 1, order = [2])
            override fun onWinContentChanged(event: AccessibilityEvent) {
                assertThat("Focused node is offscreen", screenContainsNode(nodeId), equalTo(false))
            }
        })

        SystemClock.sleep(100)
        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1, order = [1])
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                assertThat("Focused node is onscreen", screenContainsNode(nodeId), equalTo(true))
            }

            @AssertCalled(count = 1, order = [2])
            override fun onScrolled(event: AccessibilityEvent) {
                assertThat("View is scrolled to the end", event.scrollY.toDouble(), closeTo(event.maxScrollY.toDouble(), 1.0))
            }

            @AssertCalled(count = 1, order = [3])
            override fun onWinContentChanged(event: AccessibilityEvent) {
                assertThat("Focused node is onscreen", screenContainsNode(nodeId), equalTo(true))
            }
        })
    }

    @Test
    fun autoFill() {
        // Wait for the accessibility nodes to populate.
        mainSession.loadTestPath(FORMS_HTML_PATH)
        waitForInitialFocus()

        val autoFills = mapOf(
            "#user1" to "bar",
            "#pass1" to "baz",
            "#user2" to "bar",
            "#pass2" to "baz",
            "#email1" to "a@b.c",
            "#number1" to "24",
            "#tel1" to "42",
        )

        // Set up promises to monitor the values changing.
        val promises = autoFills.flatMap { entry ->
            // Repeat each test with both the top document and the iframe document.
            arrayOf("document", "document.querySelector('#iframe').contentDocument").map { doc ->
                mainSession.evaluatePromiseJS(
                    """new Promise(resolve =>
                    $doc.querySelector('${entry.key}').addEventListener(
                        'input', event => {
                          let eventInterface =
                            event instanceof $doc.defaultView.InputEvent ? "InputEvent" :
                            event instanceof $doc.defaultView.UIEvent ? "UIEvent" :
                            event instanceof $doc.defaultView.Event ? "Event" : "Unknown";
                          resolve([event.target.value, '${entry.value}', eventInterface]);
                        }, { once: true }))""",
                )
            }
        }

        // Perform auto-fill and return number of auto-fills performed.
        fun autoFillChild(id: Int, child: AccessibilityNodeInfo) {
            // Seal the node info instance so we can perform actions on it.
            if (child.childCount > 0) {
                for (i in 0 until child.childCount) {
                    val childId = child.getChildId(i)
                    autoFillChild(childId, createNodeInfo(childId))
                }
            }

            if (EditText::class.java.name == child.className) {
                assertThat("Input should be enabled", child.isEnabled, equalTo(true))
                assertThat("Input should be focusable", child.isFocusable, equalTo(true))
                assertThat(
                    "Password type should match",
                    child.isPassword,
                    equalTo(
                        child.inputType == InputType.TYPE_CLASS_TEXT or
                            InputType.TYPE_TEXT_VARIATION_WEB_PASSWORD,
                    ),
                )

                val args = Bundle(1)
                val value = if (child.isPassword) {
                    "baz"
                } else {
                    when (child.inputType) {
                        InputType.TYPE_CLASS_TEXT or
                            InputType.TYPE_TEXT_VARIATION_WEB_EMAIL_ADDRESS,
                        -> "a@b.c"
                        InputType.TYPE_CLASS_NUMBER -> "24"
                        InputType.TYPE_CLASS_PHONE -> "42"
                        else -> "bar"
                    }
                }

                val ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE = AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE
                val ACTION_SET_TEXT = AccessibilityNodeInfo.ACTION_SET_TEXT

                args.putCharSequence(ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE, value)
                assertThat(
                    "Can perform auto-fill",
                    provider.performAction(id, ACTION_SET_TEXT, args),
                    equalTo(true),
                )
            }
        }

        autoFillChild(View.NO_ID, createNodeInfo(View.NO_ID))

        // Wait on the promises and check for correct values.
        for ((actual, expected, eventInterface) in promises.map { it.value.asJSList<String>() }) {
            assertThat("Auto-filled value must match", actual, equalTo(expected))
            assertThat("input event should be dispatched with InputEvent interface", eventInterface, equalTo("InputEvent"))
        }
    }

    @Test
    fun autoFill_navigation() {
        fun countAutoFillNodes(
            cond: (AccessibilityNodeInfo) -> Boolean =
                { it.className == "android.widget.EditText" },
            id: Int = View.NO_ID,
        ): Int {
            val info = createNodeInfo(id)
            return (
                if (cond(info) && info.className != "android.webkit.WebView") {
                    1
                } else {
                    0
                }
                ) + (
                if (info.childCount > 0) {
                    (0 until info.childCount).sumOf {
                        countAutoFillNodes(cond, info.getChildId(it))
                    }
                } else {
                    0
                }
                )
        }

        // XXX: Reliably waiting for iframes to load could be flaky, so we wait
        // for our autofill nodes to be the right number.
        fun waitForAutoFillNodes() {
            val checkAutoFillNodes = object : EventDelegate, ShouldContinue {
                var haveAllAutoFills = countAutoFillNodes() == 18

                override fun shouldContinue(): Boolean = !haveAllAutoFills

                override fun onWinContentChanged(event: AccessibilityEvent) {
                    haveAllAutoFills = countAutoFillNodes() == 18
                }
            }
            if (checkAutoFillNodes.shouldContinue()) {
                sessionRule.waitUntilCalled(checkAutoFillNodes)
            }
        }

        // Wait for the accessibility nodes to populate.
        mainSession.loadTestPath(FORMS_HTML_PATH)
        waitForInitialFocus()
        waitForAutoFillNodes()

        assertThat(
            "Initial auto-fill count should match",
            countAutoFillNodes(),
            equalTo(18),
        )
        assertThat(
            "Password auto-fill count should match",
            countAutoFillNodes({ it.isPassword }),
            equalTo(4),
        )

        // Now wait for the nodes to clear.
        mainSession.loadTestPath(HELLO_HTML_PATH)
        waitForInitialFocus()
        assertThat(
            "Should not have auto-fill fields",
            countAutoFillNodes(),
            equalTo(0),
        )

        // Now wait for the nodes to reappear.
        mainSession.goBack(false)
        waitForInitialFocus()
        waitForAutoFillNodes()
        assertThat(
            "Should have auto-fill fields again",
            countAutoFillNodes(),
            equalTo(18),
        )
        assertThat(
            "Should not have focused field",
            countAutoFillNodes({ it.isFocused }),
            equalTo(0),
        )

        mainSession.evaluateJS("document.querySelector('#pass1').focus()")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled
            override fun onFocused(event: AccessibilityEvent) {
            }
        })
        assertThat(
            "Should have one focused field",
            countAutoFillNodes({ it.isFocused }),
            equalTo(1),
        )

        mainSession.evaluateJS("document.querySelector('#pass1').blur()")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled
            override fun onFocused(event: AccessibilityEvent) {
            }
        })
        assertThat(
            "Should not have focused field",
            countAutoFillNodes({ it.isFocused }),
            equalTo(0),
        )
    }

    @Test
    fun testTree() {
        loadTestPage("test-tree")
        waitForInitialFocus()

        val rootNode = createNodeInfo(View.NO_ID)
        assertThat("Document has 3 children", rootNode.childCount, equalTo(3))
        var rootBounds = Rect()
        rootNode.getBoundsInScreen(rootBounds)
        assertThat("Root node bounds are not empty", rootBounds.isEmpty, equalTo(false))
        assertThat("Root node is visible to user", rootNode.isVisibleToUser, equalTo(true))

        var labelBounds = Rect()
        val labelNode = createNodeInfo(rootNode.getChildId(0))
        labelNode.getBoundsInScreen(labelBounds)

        assertThat("Label bounds are in parent", rootBounds.contains(labelBounds), equalTo(true))
        assertThat("First node is a label", labelNode.className.toString(), equalTo("android.view.View"))
        assertThat("Label has text", labelNode.text.toString(), equalTo("Name:"))
        assertThat("Label node is visible to user", labelNode.isVisibleToUser, equalTo(true))

        val entryNode = createNodeInfo(rootNode.getChildId(1))
        assertThat("Second node is an entry", entryNode.className.toString(), equalTo("android.widget.EditText"))
        assertThat("Entry has vieIdwResourceName of 'name'", entryNode.viewIdResourceName, equalTo("name"))
        assertThat("Entry value is text", entryNode.text.toString(), equalTo("Julie"))
        assertThat("Entry node is visible to user", entryNode.isVisibleToUser, equalTo(true))
        assertThat(
            "Entry hint is label",
            entryNode.extras.getString("AccessibilityNodeInfo.hint"),
            equalTo("Name:"),
        )
        assertThat(
            "Entry input type is correct",
            entryNode.inputType,
            equalTo(InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_WEB_EDIT_TEXT),
        )

        val buttonNode = createNodeInfo(rootNode.getChildId(2))
        assertThat("Last node is a button", buttonNode.className.toString(), equalTo("android.widget.Button"))
        // The child text leaf is pruned, so this button is childless.
        assertThat("Button has a single text leaf", buttonNode.childCount, equalTo(0))
        assertThat("Button has correct text", buttonNode.text.toString(), equalTo("Submit"))
        assertThat("Button is visible to user", buttonNode.isVisibleToUser, equalTo(true))
    }

    @Test fun testLoadUnloadIframeDoc() {
        mainSession.loadTestPath(REMOTE_IFRAME)
        waitForInitialFocus()

        loadTestPage("test-tree")
        waitForInitialFocus()

        mainSession.loadTestPath(REMOTE_IFRAME)
        waitForInitialFocus()

        loadTestPage("test-tree")
        waitForInitialFocus()

        mainSession.loadTestPath(REMOTE_IFRAME)
        waitForInitialFocus()

        loadTestPage("test-tree")
        waitForInitialFocus()
    }

    private fun testAccessibilityFocusIframe(page: String) {
        var nodeId = AccessibilityNodeProvider.HOST_VIEW_ID
        mainSession.loadTestPath(page)
        waitForInitialFocus(true)

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat("Label has text", node.text.toString(), equalTo("Some stuff "))
            }
        })

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT,
            null,
        )

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat("heading has correct content", node.text as String, equalTo("Hello, world!"))
            }
        })

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_PREVIOUS_HTML_ELEMENT,
            null,
        )

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat("Label has text", node.text.toString(), equalTo("Some stuff "))
            }
        })
    }

    @Test fun testRemoteAccessibilityFocusIframe() {
        testAccessibilityFocusIframe(REMOTE_IFRAME)
    }

    @Test fun testLocalAccessibilityFocusIframe() {
        testAccessibilityFocusIframe(LOCAL_IFRAME)
    }

    private fun testIframeTree(page: String) {
        mainSession.loadTestPath(page)
        waitForInitialFocus()

        val rootNode = createNodeInfo(View.NO_ID)
        assertThat("Document has 2 children", rootNode.childCount, equalTo(2))
        var rootBounds = Rect()
        rootNode.getBoundsInScreen(rootBounds)
        assertThat("Root bounds are not empty", rootBounds.isEmpty, equalTo(false))

        val labelNode = createNodeInfo(rootNode.getChildId(0))
        assertThat("First node has text", labelNode.text.toString(), equalTo("Some stuff "))

        val iframeNode = createNodeInfo(rootNode.getChildId(1))
        assertThat("iframe has vieIdwResourceName of 'iframe'", iframeNode.viewIdResourceName, equalTo("iframe"))
        assertThat("iframe has 1 child", iframeNode.childCount, equalTo(1))
        var iframeBounds = Rect()
        iframeNode.getBoundsInScreen(iframeBounds)
        assertThat("iframe bounds in root bounds", rootBounds.contains(iframeBounds), equalTo(true))

        val innerDocNode = createNodeInfo(iframeNode.getChildId(0))
        assertThat("Inner doc has one child", innerDocNode.childCount, equalTo(1))
        var innerDocBounds = Rect()
        innerDocNode.getBoundsInScreen(innerDocBounds)
        assertThat("iframe bounds match inner doc bounds", iframeBounds.contains(innerDocBounds), equalTo(true))

        val section = createNodeInfo(innerDocNode.getChildId(0))
        assertThat("section has one child", innerDocNode.childCount, equalTo(1))

        val node = createNodeInfo(section.getChildId(0))
        assertThat("Text node has text", node.text as String, equalTo("Hello, world!"))
        var nodeBounds = Rect()
        node.getBoundsInScreen(nodeBounds)
        assertThat("inner node in inner doc bounds", innerDocBounds.contains(nodeBounds), equalTo(true))
    }

    @Test
    fun testRemoteIframeTree() {
        testIframeTree(REMOTE_IFRAME)
        // disabled for frequent failures - on Bug 1769324
        assumeThat(sessionRule.env.isX86, equalTo(false))
    }

    @Test
    fun testLocalIframeTree() {
        testIframeTree(LOCAL_IFRAME)
    }

    @Test
    fun testCollection() {
        loadTestPage("test-collection")
        waitForInitialFocus()

        val rootNode = createNodeInfo(View.NO_ID)
        assertThat("Document has 2 children", rootNode.childCount, equalTo(2))

        val firstList = createNodeInfo(rootNode.getChildId(0))
        assertThat("First list has 2 children", firstList.childCount, equalTo(2))
        assertThat("List is a ListView", firstList.className.toString(), equalTo("android.widget.ListView"))
        assertThat("First list should have collectionInfo", firstList.collectionInfo, notNullValue())
        assertThat("First list has 2 rowCount", firstList.collectionInfo.rowCount, equalTo(2))
        assertThat("First list should not be hierarchical", firstList.collectionInfo.isHierarchical, equalTo(false))

        val firstListFirstItem = createNodeInfo(firstList.getChildId(0))
        assertThat("Item has collectionItemInfo", firstListFirstItem.collectionItemInfo, notNullValue())
        assertThat("Item has correct rowIndex", firstListFirstItem.collectionItemInfo.rowIndex, equalTo(0))

        val secondList = createNodeInfo(rootNode.getChildId(1))
        assertThat("Second list has 1 child", secondList.childCount, equalTo(1))
        assertThat("Second list should have collectionInfo", secondList.collectionInfo, notNullValue())
        assertThat("Second list has 2 rowCount", secondList.collectionInfo.rowCount, equalTo(1))
        assertThat("Second list should be hierarchical", secondList.collectionInfo.isHierarchical, equalTo(true))
    }

    @Test fun testNavigateListItems() {
        loadTestPage("test-collection")
        waitForInitialFocus()
        var nodeId = View.NO_ID

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT,
            null,
        )
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on text leaf",
                    node.text as String,
                    startsWith("One"),
                )
                assertThat(
                    "first item is a text leaf",
                    node.extras.getCharSequence("AccessibilityNodeInfo.geckoRole")!!.toString(),
                    equalTo("text leaf"),
                )
            }
        })

        provider.performAction(
            nodeId,
            AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT,
            null,
        )
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on link",
                    node.contentDescription as String,
                    startsWith("Two"),
                )
                assertThat(
                    "second item is a link",
                    node.extras.getCharSequence("AccessibilityNodeInfo.geckoRole")!!.toString(),
                    equalTo("link"),
                )
            }
        })
    }

    @Test
    fun testRange() {
        loadTestPage("test-range")
        waitForInitialFocus()

        val rootNode = createNodeInfo(View.NO_ID)
        assertThat("Document has 3 children", rootNode.childCount, equalTo(3))

        val firstRange = createNodeInfo(rootNode.getChildId(0))
        assertThat("Range has right label", firstRange.text.toString(), equalTo("Rating"))
        assertThat("Range is SeekBar", firstRange.className.toString(), equalTo("android.widget.SeekBar"))
        assertThat("'Rating' has rangeInfo", firstRange.rangeInfo, notNullValue())
        assertThat("'Rating' has correct value", firstRange.rangeInfo.current, equalTo(4f))
        assertThat("'Rating' has correct max", firstRange.rangeInfo.max, equalTo(10f))
        assertThat("'Rating' has correct min", firstRange.rangeInfo.min, equalTo(1f))
        assertThat("'Rating' has correct range type", firstRange.rangeInfo.type, equalTo(AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_INT))

        val secondRange = createNodeInfo(rootNode.getChildId(1))
        assertThat("Range has right label", secondRange.text.toString(), equalTo("Stars"))
        assertThat("'Rating' has rangeInfo", secondRange.rangeInfo, notNullValue())
        assertThat("'Rating' has correct value", secondRange.rangeInfo.current, equalTo(4.5f))
        assertThat("'Rating' has correct max", secondRange.rangeInfo.max, equalTo(5f))
        assertThat("'Rating' has correct min", secondRange.rangeInfo.min, equalTo(1f))
        assertThat("'Rating' has correct range type", secondRange.rangeInfo.type, equalTo(AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_FLOAT))

        val thirdRange = createNodeInfo(rootNode.getChildId(2))
        assertThat("Range has right label", thirdRange.text.toString(), equalTo("Percent"))
        assertThat("'Rating' has rangeInfo", thirdRange.rangeInfo, notNullValue())
        assertThat("'Rating' has correct value", thirdRange.rangeInfo.current, equalTo(0.83f))
        assertThat("'Rating' has correct max", thirdRange.rangeInfo.max, equalTo(1f))
        assertThat("'Rating' has correct min", thirdRange.rangeInfo.min, equalTo(0f))
        assertThat("'Rating' has correct range type", thirdRange.rangeInfo.type, equalTo(AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_PERCENT))
    }

    @Test fun testLinksMovingByDefault() {
        loadTestPage("test-links")
        waitForInitialFocus()
        var nodeId = View.NO_ID

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on a with href",
                    node.contentDescription as String,
                    startsWith("a with href"),
                )
                assertThat(
                    "a with href is a link",
                    node.extras.getCharSequence("AccessibilityNodeInfo.roleDescription")!!.toString(),
                    equalTo("link"),
                )
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on a with no attributes",
                    node.text as String,
                    startsWith("a with no attributes"),
                )
                assertThat(
                    "a with no attributes is not a link",
                    node.extras.getCharSequence("AccessibilityNodeInfo.roleDescription")!!.toString(),
                    equalTo(""),
                )
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on a with name",
                    node.text as String,
                    startsWith("a with name"),
                )
                assertThat(
                    "a with name is not a link",
                    node.extras.getCharSequence("AccessibilityNodeInfo.roleDescription")!!.toString(),
                    equalTo(""),
                )
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on a with onclick",
                    node.contentDescription as String,
                    startsWith("a with onclick"),
                )
                assertThat(
                    "a with onclick is a link",
                    node.extras.getCharSequence("AccessibilityNodeInfo.roleDescription")!!.toString(),
                    equalTo("link"),
                )
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on span with role link",
                    node.contentDescription as String,
                    startsWith("span with role link"),
                )
                assertThat(
                    "span with role link is a link",
                    node.extras.getCharSequence("AccessibilityNodeInfo.roleDescription")!!.toString(),
                    equalTo("link"),
                )
            }
        })
    }

    @Test fun testLinksMovingByLink() {
        loadTestPage("test-links")
        waitForInitialFocus()
        var nodeId = View.NO_ID

        val bundle = Bundle()
        bundle.putString(AccessibilityNodeInfo.ACTION_ARGUMENT_HTML_ELEMENT_STRING, "LINK")

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, bundle)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on a with href",
                    node.contentDescription as String,
                    startsWith("a with href"),
                )
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, bundle)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on a with onclick",
                    node.contentDescription as String,
                    startsWith("a with onclick"),
                )
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, bundle)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on span with role link",
                    node.contentDescription as String,
                    startsWith("span with role link"),
                )
            }
        })
    }

    @Test fun testAriaComboBoxesMovingByDefault() {
        loadTestPage("test-aria-comboboxes")
        waitForInitialFocus()
        var nodeId = View.NO_ID

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus is EditBox",
                    node.className.toString(),
                    equalTo("android.widget.EditText"),
                )
                assertThat(
                    "Accessibility focus on ARIA 1.0 combobox",
                    node.extras.getString("AccessibilityNodeInfo.hint"),
                    equalTo("ARIA 1.0 combobox"),
                )
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus is EditBox",
                    node.className.toString(),
                    equalTo("android.widget.EditText"),
                )
                assertThat(
                    "Accessibility focus on ARIA 1.1 combobox",
                    node.extras.getString("AccessibilityNodeInfo.hint"),
                    equalTo("ARIA 1.1 combobox"),
                )
            }
        })
    }

    @Test fun testAriaComboBoxesMovingByControl() {
        loadTestPage("test-aria-comboboxes")
        waitForInitialFocus()
        var nodeId = View.NO_ID

        val bundle = Bundle()
        bundle.putString(AccessibilityNodeInfo.ACTION_ARGUMENT_HTML_ELEMENT_STRING, "CONTROL")

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, bundle)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus is EditBox",
                    node.className.toString(),
                    equalTo("android.widget.EditText"),
                )
                assertThat(
                    "Accessibility focus on ARIA 1.0 combobox",
                    node.extras.getString("AccessibilityNodeInfo.hint"),
                    equalTo("ARIA 1.0 combobox"),
                )
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, bundle)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus is EditBox",
                    node.className.toString(),
                    equalTo("android.widget.EditText"),
                )
                assertThat(
                    "Accessibility focus on ARIA 1.1 combobox",
                    node.extras.getString("AccessibilityNodeInfo.hint"),
                    equalTo("ARIA 1.1 combobox"),
                )
            }
        })
    }

    @Test fun testAccessibilityFocusBoundaries() {
        loadTestPage("test-links")
        waitForInitialFocus()
        var nodeId = View.NO_ID
        var performedAction: Boolean

        performedAction = provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        assertThat("Successfully moved a11y focus to first node", performedAction, equalTo(true))
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on a with href",
                    node.contentDescription as String,
                    startsWith("a with href"),
                )
            }
        })

        performedAction = provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_PREVIOUS_HTML_ELEMENT, null)
        assertThat("Successfully moved a11y focus past first node", performedAction, equalTo(true))
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                assertThat("Accessibility focus on web view", getSourceId(event), equalTo(View.NO_ID))
            }
        })

        performedAction = provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        assertThat("Successfully moved a11y focus to second node", performedAction, equalTo(true))
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on a with no attributes",
                    node.text as String,
                    startsWith("a with no attributes"),
                )
            }
        })

        // hide first and last link
        mainSession.evaluateJS("document.querySelectorAll('body > :first-child, body > :last-child').forEach(e => e.style.display = 'none');")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onWinContentChanged(event: AccessibilityEvent) { }
        })

        performedAction = provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_PREVIOUS_HTML_ELEMENT, null)
        assertThat("Successfully moved a11y focus past first visible node", performedAction, equalTo(true))
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                assertThat("Accessibility focus on web view", getSourceId(event), equalTo(View.NO_ID))
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on a with name",
                    node.text as String,
                    startsWith("a with name"),
                )
                assertThat(
                    "a with name is not a link",
                    node.extras.getCharSequence("AccessibilityNodeInfo.roleDescription")!!.toString(),
                    equalTo(""),
                )
            }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on a with onclick",
                    node.contentDescription as String,
                    startsWith("a with onclick"),
                )
                assertThat(
                    "a with onclick is a link",
                    node.extras.getCharSequence("AccessibilityNodeInfo.roleDescription")!!.toString(),
                    equalTo("link"),
                )
            }
        })

        performedAction = provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        assertThat("Should fail to move a11y focus to last hidden node", performedAction, equalTo(false))

        // show last link
        mainSession.evaluateJS("document.querySelector('body > :last-child').style.display = 'initial';")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onWinContentChanged(event: AccessibilityEvent) { }
        })

        provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {
                nodeId = getSourceId(event)
                val node = createNodeInfo(nodeId)
                assertThat(
                    "Accessibility focus on span with role link",
                    node.contentDescription as String,
                    startsWith("span with role link"),
                )
                assertThat(
                    "span with role link is a link",
                    node.extras.getCharSequence("AccessibilityNodeInfo.roleDescription")!!.toString(),
                    equalTo("link"),
                )
            }
        })

        performedAction = provider.performAction(nodeId, AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT, null)
        assertThat("Should fail to move a11y focus beyond last node", performedAction, equalTo(false))

        performedAction = provider.performAction(View.NO_ID, AccessibilityNodeInfo.ACTION_PREVIOUS_HTML_ELEMENT, null)
        assertThat("Should fail to move a11y focus before web content", performedAction, equalTo(false))
    }

    @Test fun testTextEntry() {
        loadTestPage("test-text-entry-node")
        waitForInitialFocus()

        mainSession.evaluateJS("document.querySelector('input[aria-label=Naame]').focus()")
        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onFocused(event: AccessibilityEvent) {}

            // Focus fires a caret moved event which produces this event.
            @AssertCalled(count = 1)
            override fun onTextSelectionChanged(event: AccessibilityEvent) {}
        })

        mainSession.evaluateJS("document.querySelector('input[aria-label=Naame]').value = 'Tobiasas'")

        sessionRule.waitUntilCalled(object : EventDelegate {
            @AssertCalled(count = 1)
            override fun onTextChanged(event: AccessibilityEvent) {}

            @AssertCalled(count = 1)
            override fun onTextSelectionChanged(event: AccessibilityEvent) {}

            // Don't fire a11y focus for collapsed caret changes.
            // This will interfere with on screen keyboards and throw a11y focus
            // back and fourth.
            @AssertCalled(count = 0)
            override fun onAccessibilityFocused(event: AccessibilityEvent) {}
        })
    }
}
