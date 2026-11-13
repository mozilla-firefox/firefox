/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import android.content.Context
import android.content.res.Configuration
import android.view.View
import android.view.ViewGroup
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.view.isVisible
import io.mockk.Called
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.spyk
import io.mockk.verify
import junit.framework.TestCase.assertFalse
import junit.framework.TestCase.assertTrue
import kotlin.test.assertNotNull
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.state.createTab
import mozilla.components.concept.engine.EngineView
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.feature.contextmenu.ContextMenuCandidate
import mozilla.components.ui.widgets.VerticalSwipeRefreshLayout
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.components.FindInPageIntegration
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.toolbar.BottomToolbarContainerView
import org.mozilla.fenix.components.toolbar.BrowserNavigationBar
import org.mozilla.fenix.components.toolbar.BrowserToolbarComposable
import org.mozilla.fenix.components.toolbar.ToolbarContainerView
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.microsurvey.MicrosurveyState
import org.mozilla.fenix.microsurvey.ui.ext.MicrosurveyUIData
import org.mozilla.fenix.utils.Settings

class BaseBrowserFragmentTest {
    private lateinit var fragment: TestBaseBrowserFragment
    private lateinit var swipeRefreshLayout: VerticalSwipeRefreshLayout
    private lateinit var engineView: EngineView
    private lateinit var settings: Settings
    private lateinit var testContext: Context
    lateinit var container: ViewGroup

    @Before
    fun setup() {
        fragment = spyk(TestBaseBrowserFragment())
        swipeRefreshLayout = mockk(relaxed = true)
        engineView = mockk(relaxed = true)
        settings =
            mockk(relaxed = true) {
                every { isTabStripEnabled } returns false
            }
        testContext = mockk(relaxed = true)
        container = mockk(relaxed = true)

        every {
            testContext.components.core.geckoRuntime.isInteractiveWidgetDefaultResizesVisual
        } returns false
        every { testContext.components.settings } returns settings
        every { testContext.components.settings } returns settings
        every { fragment.isAdded } returns true
        every { fragment.activity } returns mockk()
        every { fragment.context } returns testContext
        every { fragment.requireContext() } returns testContext
        every { fragment.getEngineView() } returns engineView
        every { fragment.getSwipeRefreshLayout() } returns swipeRefreshLayout
        every { swipeRefreshLayout.layoutParams } returns mockk<CoordinatorLayout.LayoutParams>(relaxed = true)
        every { fragment.binding } returns mockk(relaxed = true)
        every { fragment.viewLifecycleOwner } returns mockk(relaxed = true)
    }

    @Test
    fun `initializeEngineView should setDynamicToolbarMaxHeight to 0 if top toolbar is forced for a11y`() {
        every { settings.shouldUseBottomToolbar } returns false
        every { settings.shouldUseFixedTopToolbar } returns true

        fragment.initializeEngineView(
            topToolbarHeight = 13,
            bottomToolbarHeight = 0,
        )

        verify { engineView.setDynamicToolbarMaxHeight(0) }
    }

    @Test
    fun `initializeEngineView should setDynamicToolbarMaxHeight to 0 if bottom toolbar is forced for a11y`() {
        every { settings.shouldUseBottomToolbar } returns true
        every { settings.shouldUseFixedTopToolbar } returns true

        fragment.initializeEngineView(
            topToolbarHeight = 13,
            bottomToolbarHeight = 13,
        )

        verify { engineView.setDynamicToolbarMaxHeight(0) }
    }

    @Test
    fun `initializeEngineView should setDynamicToolbarMaxHeight to toolbar height if dynamic toolbar is enabled`() {
        every { settings.shouldUseFixedTopToolbar } returns false
        every { settings.isDynamicToolbarEnabled } returns true

        fragment.initializeEngineView(
            topToolbarHeight = 13,
            bottomToolbarHeight = 0,
        )

        verify { engineView.setDynamicToolbarMaxHeight(13) }
    }

    @Test
    fun `initializeEngineView should setDynamicToolbarMaxHeight to 0 if dynamic toolbar is disabled`() {
        every { settings.shouldUseFixedTopToolbar } returns false
        every { settings.isDynamicToolbarEnabled } returns false

        fragment.initializeEngineView(
            topToolbarHeight = 13,
            bottomToolbarHeight = 0,
        )

        verify { engineView.setDynamicToolbarMaxHeight(0) }
    }

    @Test
    fun `initializeEngineView should set EngineViewClippingBehavior when dynamic toolbar is enabled`() {
        every { settings.shouldUseFixedTopToolbar } returns false
        every { settings.isDynamicToolbarEnabled } returns true
        val params: CoordinatorLayout.LayoutParams = mockk(relaxed = true)
        every { params.behavior } returns mockk(relaxed = true)
        every { swipeRefreshLayout.layoutParams } returns params

        fragment.initializeEngineView(
            topToolbarHeight = 13,
            bottomToolbarHeight = 0,
        )
    }

    @Test
    fun `initializeEngineView should set toolbar height as EngineView parent's bottom margin when using bottom toolbar`() {
        every { settings.isDynamicToolbarEnabled } returns false
        every { settings.shouldUseBottomToolbar } returns true

        fragment.initializeEngineView(
            topToolbarHeight = 0,
            bottomToolbarHeight = 13,
        )

        verify { (swipeRefreshLayout.layoutParams as CoordinatorLayout.LayoutParams).bottomMargin = 13 }
    }

    @Test
    fun `initializeEngineView should set toolbar height as EngineView parent's bottom margin if top toolbar is forced for a11y`() {
        every { settings.shouldUseBottomToolbar } returns false
        every { settings.shouldUseFixedTopToolbar } returns true

        fragment.initializeEngineView(
            topToolbarHeight = 13,
            bottomToolbarHeight = 0,
        )

        verify { (swipeRefreshLayout.layoutParams as CoordinatorLayout.LayoutParams).bottomMargin = 13 }
    }

    @Test
    fun `initializeEngineView should set toolbar height as EngineView parent's bottom margin if bottom toolbar is forced for a11y`() {
        every { settings.shouldUseBottomToolbar } returns true
        every { settings.shouldUseFixedTopToolbar } returns true

        fragment.initializeEngineView(
            topToolbarHeight = 0,
            bottomToolbarHeight = 13,
        )

        verify { (swipeRefreshLayout.layoutParams as CoordinatorLayout.LayoutParams).bottomMargin = 13 }
    }

    @Test
    fun `WHEN status is equals to FAILED or COMPLETED and it is the same tab then shouldShowCompletedDownloadDialog will be true`() {
        every { fragment.getCurrentTab() } returns createTab(id = "1", url = "")

        val download =
            DownloadState(
                url = "",
                sessionId = "1",
                directoryPath = "/",
            )

        val status =
            DownloadState.Status.entries.filter {
                it == DownloadState.Status.COMPLETED && it == DownloadState.Status.FAILED
            }

        status.forEach {
            val result = fragment.shouldShowCompletedDownloadDialog(download, it)

            assertTrue(result)
        }
    }

    @Test
    fun `WHEN status is different from FAILED or COMPLETED then shouldShowCompletedDownloadDialog will be false`() {
        every { fragment.getCurrentTab() } returns createTab(id = "1", url = "")

        val download =
            DownloadState(
                url = "",
                sessionId = "1",
                directoryPath = "/",
            )

        val status =
            DownloadState.Status.entries.filter {
                it != DownloadState.Status.COMPLETED && it != DownloadState.Status.FAILED
            }

        status.forEach {
            val result = fragment.shouldShowCompletedDownloadDialog(download, it)

            assertFalse(result)
        }
    }

    @Test
    fun `WHEN the tab is different from the initial one then shouldShowCompletedDownloadDialog will be false`() {
        every { fragment.getCurrentTab() } returns createTab(id = "1", url = "")

        val download =
            DownloadState(
                url = "",
                sessionId = "2",
                directoryPath = "/",
            )

        val status =
            DownloadState.Status.entries.filter {
                it != DownloadState.Status.COMPLETED && it != DownloadState.Status.FAILED
            }

        status.forEach {
            val result = fragment.shouldShowCompletedDownloadDialog(download, it)

            assertFalse(result)
        }
    }

    @Test
    fun `WHEN initializeEngineView is called  THEN setDynamicToolbarMaxHeight sets max height to the engine view as a sum of two toolbars heights`() {
        every { settings.shouldUseFixedTopToolbar } returns false
        every { settings.isDynamicToolbarEnabled } returns true

        fragment.initializeEngineView(
            topToolbarHeight = 13,
            bottomToolbarHeight = 0,
        )
        verify { engineView.setDynamicToolbarMaxHeight(13) }

        fragment.initializeEngineView(
            topToolbarHeight = 0,
            bottomToolbarHeight = 13,
        )
        verify { engineView.setDynamicToolbarMaxHeight(13) }

        fragment.initializeEngineView(
            topToolbarHeight = 13,
            bottomToolbarHeight = 13,
        )
        verify { engineView.setDynamicToolbarMaxHeight(26) }
    }

    @Test
    fun `GIVEN dynamic toolbars and not a PWA WHEN initializeEngineView is called THEN set a custom behavior for the browser`() {
        every { settings.shouldUseFixedTopToolbar } returns false
        every { settings.isDynamicToolbarEnabled } returns true
        fragment.webAppToolbarShouldBeVisible = true

        fragment.initializeEngineView(0, 0)

        val browserViewParams = swipeRefreshLayout.layoutParams as CoordinatorLayout.LayoutParams
        verify { browserViewParams.behavior = any() }
    }

    @Test
    fun `GIVEN dynamic toolbars for a PWA WHEN initializeEngineView is called THEN don't set a custom behavior for the browser`() {
        every { settings.shouldUseFixedTopToolbar } returns false
        every { settings.isDynamicToolbarEnabled } returns true
        fragment.webAppToolbarShouldBeVisible = false

        fragment.initializeEngineView(0, 0)

        val browserViewParams = swipeRefreshLayout.layoutParams as CoordinatorLayout.LayoutParams
        verify { browserViewParams wasNot Called }
    }

    @Test
    fun `WHEN isMicrosurveyEnabled and isExperimentationEnabled are true GIVEN a call to initializeMicrosurveyFeature THEN messagingFeature is initialized and observer is added`() {
        every { testContext.components.settings.isExperimentationEnabled } returns true
        every { testContext.components.settings.microsurveyFeatureEnabled } returns true
        val lifecycle = fragment.viewLifecycleOwner.lifecycle

        assertNull(fragment.messagingFeatureMicrosurvey.get())

        fragment.initializeMicrosurveyFeature(testContext)

        val feature = fragment.messagingFeatureMicrosurvey.get()
        assertNotNull(feature)
        verify(exactly = 1) { lifecycle.addObserver(feature) }
    }

    @Test
    fun `WHEN isMicrosurveyEnabled and isExperimentationEnabled are false GIVEN a call to initializeMicrosurveyFeature THEN messagingFeature is not initialized and observer is not added`() {
        every { testContext.components.settings.isExperimentationEnabled } returns false
        every { testContext.components.settings.microsurveyFeatureEnabled } returns false
        val lifecycle = fragment.viewLifecycleOwner.lifecycle

        assertNull(fragment.messagingFeatureMicrosurvey.get())

        fragment.initializeMicrosurveyFeature(testContext)

        val feature = fragment.messagingFeatureMicrosurvey.get()
        assertNull(feature)
        verify(exactly = 0) { lifecycle.addObserver(any()) }
    }

    @Test
    fun `WHEN isMicrosurveyEnabled is true and isExperimentationEnabled false GIVEN a call to initializeMicrosurveyFeature THEN messagingFeature is not initialized and observer is not added`() {
        every { testContext.components.settings.isExperimentationEnabled } returns false
        every { testContext.components.settings.microsurveyFeatureEnabled } returns true
        val lifecycle = fragment.viewLifecycleOwner.lifecycle

        assertNull(fragment.messagingFeatureMicrosurvey.get())

        fragment.initializeMicrosurveyFeature(testContext)

        val feature = fragment.messagingFeatureMicrosurvey.get()
        assertNull(feature)
        verify(exactly = 0) { lifecycle.addObserver(any()) }
    }

    @Test
    fun `WHEN isMicrosurveyEnabled is false and isExperimentationEnabled true GIVEN a call to initializeMicrosurveyFeature THEN messagingFeature is not initialized and observer is not added`() {
        every { testContext.components.settings.isExperimentationEnabled } returns true
        every { testContext.components.settings.microsurveyFeatureEnabled } returns false
        val lifecycle = fragment.viewLifecycleOwner.lifecycle

        assertNull(fragment.messagingFeatureMicrosurvey.get())

        fragment.initializeMicrosurveyFeature(testContext)

        val feature = fragment.messagingFeatureMicrosurvey.get()
        assertNull(feature)
        verify(exactly = 0) { lifecycle.addObserver(any()) }
    }

    @Test
    fun `GIVEN fixed toolbar WHEN setting engine view insets THEN use bottom toolbar's height as bottom margin`() {
        val currentTab = createTab("https://example.com")
        every { testContext.components.core.store.state } returns
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            )
        every { fragment.view } returns
            mockk {
                every { context } returns testContext
            }
        every { settings.getBrowserToolbarHeight(testContext) } returns 11
        every { settings.toolbarPosition } returns ToolbarPosition.BOTTOM
        every { settings.isDynamicToolbarEnabled } returns false

        fragment.configureEngineViewWithDynamicToolbarsMaxHeight()

        verify { (swipeRefreshLayout.layoutParams as CoordinatorLayout.LayoutParams).bottomMargin = 11 }
    }

    @Test
    fun `GIVEN only a top toolbar WHEN setting engine view insets THEN use top toolbar's height`() {
        val currentTab = createTab("https://example.com")
        every { testContext.components.core.store.state } returns
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            )
        every { fragment.view } returns
            mockk {
                every { context } returns testContext
            }
        every { settings.getBrowserToolbarHeight(testContext) } returns 11
        every { settings.toolbarPosition } returns ToolbarPosition.TOP
        every { settings.isDynamicToolbarEnabled } returns true
        every { settings.shouldUseFixedTopToolbar } returns false

        fragment.configureEngineViewWithDynamicToolbarsMaxHeight()

        verify { engineView.setDynamicToolbarMaxHeight(11) }
    }

    @Test
    fun `GIVEN default engine view resize behavior and only a top toolbar WHEN setting engine view insets THEN don't update current values`() {
        val currentTab = createTab("https://example.com")
        every { testContext.components.core.store.state } returns
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            )
        every {
            testContext.components.core.geckoRuntime.isInteractiveWidgetDefaultResizesVisual
        } returns true
        every { fragment.view } returns
            mockk {
                every { context } returns testContext
            }
        every { settings.getBrowserToolbarHeight(testContext) } returns 11
        every { settings.toolbarPosition } returns ToolbarPosition.TOP
        every { settings.isDynamicToolbarEnabled } returns true
        every { settings.shouldUseFixedTopToolbar } returns false

        fragment.configureEngineViewWithDynamicToolbarsMaxHeight()

        verify(exactly = 0) { engineView.setDynamicToolbarMaxHeight(any()) }
    }

    @Test
    fun `GIVEN a pdf shown with a dynamic top toolbar WHEN setting engine view insets THEN set none`() {
        var currentTab = createTab("https://example.com")
        currentTab = currentTab.copy(content = currentTab.content.copy(isPdf = true))
        every { testContext.components.core.store.state } returns
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            )
        every { fragment.view } returns
            mockk {
                every { context } returns testContext
            }
        every { settings.getBrowserToolbarHeight(testContext) } returns 11
        every { settings.toolbarPosition } returns ToolbarPosition.TOP
        every { settings.isDynamicToolbarEnabled } returns true
        every { settings.shouldUseFixedTopToolbar } returns false

        fragment.configureEngineViewWithDynamicToolbarsMaxHeight()

        verify(exactly = 0) { engineView.setDynamicToolbarMaxHeight(any()) }
    }

    @Test
    fun `GIVEN find in page active with a dynamic top toolbar WHEN setting engine view insets THEN set none`() {
        val currentTab = createTab("https://example.com")
        every { testContext.components.core.store.state } returns
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            )
        val findInPageIntegration: FindInPageIntegration = mockk {
            every { isFeatureActive } returns true
        }
        fragment.findInPageIntegration.set(
            feature = findInPageIntegration,
            owner = mockk(relaxed = true),
            view = mockk(relaxed = true),
        )
        every { fragment.view } returns
            mockk {
                every { context } returns testContext
            }
        every { settings.getBrowserToolbarHeight(testContext) } returns 11
        every { settings.toolbarPosition } returns ToolbarPosition.TOP
        every { settings.isDynamicToolbarEnabled } returns true
        every { settings.shouldUseFixedTopToolbar } returns false

        fragment.configureEngineViewWithDynamicToolbarsMaxHeight()

        verify(exactly = 0) { engineView.setDynamicToolbarMaxHeight(any()) }
    }

    @Test
    fun `GIVEN only a bottom toolbar WHEN setting engine view insets THEN use bottom toolbar's height`() {
        val currentTab = createTab("https://example.com")
        every { testContext.components.core.store.state } returns
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            )
        every { fragment.view } returns
            mockk {
                every { context } returns testContext
            }
        every { settings.getBrowserToolbarHeight(testContext) } returns 22
        every { settings.toolbarPosition } returns ToolbarPosition.TOP
        every { settings.isDynamicToolbarEnabled } returns true
        every { settings.shouldUseFixedTopToolbar } returns false

        fragment.configureEngineViewWithDynamicToolbarsMaxHeight()

        verify { engineView.setDynamicToolbarMaxHeight(22) }
    }

    @Test
    fun `GIVEN default engine view resize behavior and only a bottom toolbar WHEN setting engine view insets THEN don't update current values`() {
        val currentTab = createTab("https://example.com")
        every { testContext.components.core.store.state } returns
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            )
        every {
            testContext.components.core.geckoRuntime.isInteractiveWidgetDefaultResizesVisual
        } returns true
        every { fragment.view } returns
            mockk {
                every { context } returns testContext
            }
        every { settings.getBrowserToolbarHeight(testContext) } returns 11
        every { settings.toolbarPosition } returns ToolbarPosition.BOTTOM
        every { settings.isDynamicToolbarEnabled } returns true
        every { settings.shouldUseFixedTopToolbar } returns false

        fragment.configureEngineViewWithDynamicToolbarsMaxHeight()

        verify(exactly = 0) { engineView.setDynamicToolbarMaxHeight(any()) }
    }

    @Test
    fun `GIVEN addressbar and micro survey shown WHEN setting engine view insets THEN use both toolbars' heights`() {
        val currentTab = createTab("https://example.com")
        every { testContext.components.core.store.state } returns
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            )
        every { fragment.view } returns
            mockk {
                every { context } returns testContext
            }
        every { settings.getBrowserToolbarHeight(testContext) } returns 11
        every { settings.toolbarPosition } returns ToolbarPosition.TOP
        every { settings.microsurveyFeatureEnabled } returns true
        every { testContext.components.appStore.state } returns
            AppState(
                microsurvey =
                    MicrosurveyState(
                        current =
                            MicrosurveyUIData(
                                id = "microsurvey-id",
                                promptTitle = "Prompt title",
                                icon = 0,
                                question = "Question?",
                                answers = listOf("Yes", "No"),
                                maxNumberLines = 3,
                            )
                    )
            )
        every { settings.isDynamicToolbarEnabled } returns true
        every { settings.shouldUseFixedTopToolbar } returns false
        every { testContext.resources.getDimensionPixelSize(R.dimen.browser_microsurvey_height) } returns 131

        fragment.configureEngineViewWithDynamicToolbarsMaxHeight()

        verify { engineView.setDynamicToolbarMaxHeight(142) }
    }

    @Test
    fun `GIVEN default engine view resize behavior and addressbar and navbar shown WHEN setting engine view insets THEN use don't update current values`() {
        val currentTab = createTab("https://example.com")
        every { testContext.components.core.store.state } returns
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            )
        every {
            testContext.components.core.geckoRuntime.isInteractiveWidgetDefaultResizesVisual
        } returns true
        every { fragment.view } returns
            mockk {
                every { context } returns testContext
            }
        every { settings.getBrowserToolbarHeight(testContext) } returns 11
        every { settings.toolbarPosition } returns ToolbarPosition.TOP
        every { settings.isDynamicToolbarEnabled } returns true
        every { settings.shouldUseFixedTopToolbar } returns false
        every { testContext.resources.getDimensionPixelSize(R.dimen.browser_navbar_height) } returns 10

        fragment.configureEngineViewWithDynamicToolbarsMaxHeight()

        verify(exactly = 0) { engineView.setDynamicToolbarMaxHeight(any()) }
    }

    @Test
    fun `GIVEN keyboard shown WHEN setting engine view insets THEN use both toolbars' heights`() {
        val currentTab = createTab("https://example.com")
        val configuration =
            Configuration().apply {
                screenHeightDp = 700
                screenWidthDp = 400
            }
        every { testContext.components.core.store.state } returns
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            )
        every { fragment.view } returns
            mockk {
                every { context } returns testContext
            }
        every { settings.getBrowserToolbarHeight(testContext) } returns 11
        every { settings.toolbarPosition } returns ToolbarPosition.TOP
        every { settings.isDynamicToolbarEnabled } returns true
        every { settings.shouldUseFixedTopToolbar } returns false
        every { settings.shouldUseExpandedToolbar } returns true
        every { testContext.resources.getDimensionPixelSize(R.dimen.browser_navbar_height) } returns 22
        every { testContext.resources.configuration } returns configuration

        fragment.configureEngineViewWithDynamicToolbarsMaxHeight()

        verify { engineView.setDynamicToolbarMaxHeight(33) }
    }

    @Test
    fun `GIVEN default engine view resize behavior and keyboard shown WHEN setting engine view insets THEN don't update current values`() {
        val currentTab = createTab("https://example.com")
        every { testContext.components.core.store.state } returns
            BrowserState(
                tabs = listOf(currentTab),
                selectedTabId = currentTab.id,
            )
        every {
            testContext.components.core.geckoRuntime.isInteractiveWidgetDefaultResizesVisual
        } returns true
        every { fragment.view } returns
            mockk {
                every { context } returns testContext
            }
        every { settings.getBrowserToolbarHeight(testContext) } returns 11
        every { settings.toolbarPosition } returns ToolbarPosition.TOP
        every { settings.isDynamicToolbarEnabled } returns true
        every { settings.shouldUseFixedTopToolbar } returns false
        every { testContext.resources.getDimensionPixelSize(R.dimen.browser_navbar_height) } returns 10

        fragment.configureEngineViewWithDynamicToolbarsMaxHeight()

        verify(exactly = 0) { engineView.setDynamicToolbarMaxHeight(any()) }
    }

    @Test
    fun `WHEN asked to expand the browser view THEN hide all toolbars and show only the browser view`() {
        val browserToolbar = mockk<BrowserToolbarComposable>(relaxed = true)
        val toolbarContainerView = mockk<ToolbarContainerView>(relaxed = true)
        val bottomToolbarContainerView = mockk<BottomToolbarContainerView>()
        val browserNavigationBar = mockk<BrowserNavigationBar>(relaxed = true)
        every { bottomToolbarContainerView.toolbarContainerView } returns toolbarContainerView
        fragment._browserToolbar = browserToolbar
        fragment._bottomToolbarContainerView = bottomToolbarContainerView
        fragment.browserNavigationBar = browserNavigationBar

        fragment.expandBrowserView()

        verify { browserToolbar.gone() }
        verify { browserNavigationBar.gone() }
        verify { toolbarContainerView.collapse() }
        verify { toolbarContainerView.isVisible = false }
        val browserViewParams = swipeRefreshLayout.layoutParams as CoordinatorLayout.LayoutParams
        verify { browserViewParams.behavior = null }
        verify { (swipeRefreshLayout.layoutParams as CoordinatorLayout.LayoutParams).bottomMargin = 0 }
        verify { (swipeRefreshLayout.layoutParams as CoordinatorLayout.LayoutParams).topMargin = 0 }
        verify { swipeRefreshLayout.translationY = 0f }
        verify { engineView.setDynamicToolbarMaxHeight(0) }
        verify { engineView.setVerticalClipping(0) }
    }

    @Test
    fun `GIVEN toolbars should be visible WHEN asked to collapse the browser view THEN reinitialize the browser view and show the toolbars`() {
        fragment.webAppToolbarShouldBeVisible = true
        every { fragment.reinitializeEngineView() } just Runs
        val browserToolbar = mockk<BrowserToolbarComposable>(relaxed = true)
        val toolbarContainerView = mockk<ToolbarContainerView>(relaxed = true)
        val bottomToolbarContainerView = mockk<BottomToolbarContainerView>()
        val browserNavigationBar = mockk<BrowserNavigationBar>(relaxed = true)
        every { bottomToolbarContainerView.toolbarContainerView } returns toolbarContainerView
        fragment._browserToolbar = browserToolbar
        fragment._bottomToolbarContainerView = bottomToolbarContainerView
        fragment.browserNavigationBar = browserNavigationBar

        fragment.collapseBrowserView()

        verify { fragment.reinitializeEngineView() }
        verify { browserToolbar.visible() }
        verify { toolbarContainerView.isVisible = true }
        verify { browserToolbar.expand() }
        verify { toolbarContainerView.expand() }
    }

    @Test
    fun `GIVEN toolbars should not be visible WHEN asked to collapse the browser view THEN don't do anything`() {
        fragment.webAppToolbarShouldBeVisible = false
        every { fragment.reinitializeEngineView() } just Runs
        val browserToolbar = mockk<BrowserToolbarComposable>(relaxed = true)
        val toolbarContainerView = mockk<ToolbarContainerView>(relaxed = true)
        val bottomToolbarContainerView = mockk<BottomToolbarContainerView>()
        every { bottomToolbarContainerView.toolbarContainerView } returns toolbarContainerView
        fragment._browserToolbar = browserToolbar
        fragment._bottomToolbarContainerView = bottomToolbarContainerView

        fragment.collapseBrowserView()

        verify(exactly = 0) { fragment.reinitializeEngineView() }
        verify { browserToolbar wasNot Called }
        verify { toolbarContainerView wasNot Called }
    }

    @Test
    fun `GIVEN normal browsing WHEN reinitializing the engine view THEN use the toolbar heights`() {
        fragment.webAppToolbarShouldBeVisible = true
        val configuration =
            Configuration().apply {
                screenHeightDp = 700
                screenWidthDp = 400
            }
        every { fragment.view } returns
            mockk {
                every { context } returns testContext
            }
        every { settings.getBrowserToolbarHeight(testContext) } returns 11
        every { settings.toolbarPosition } returns ToolbarPosition.TOP
        every { settings.isDynamicToolbarEnabled } returns true
        every { settings.shouldUseExpandedToolbar } returns true
        every { testContext.resources.getDimensionPixelSize(R.dimen.browser_navbar_height) } returns 22
        every { testContext.resources.configuration } returns configuration

        fragment.reinitializeEngineView()

        verify { fragment.initializeEngineView(11, 22) }
    }

    @Test
    fun `GIVEN a PWA WHEN reinitializing the engine view THEN ignore toolbar heights`() {
        fragment.webAppToolbarShouldBeVisible = false
        val configuration =
            Configuration().apply {
                screenHeightDp = 700
                screenWidthDp = 400
            }
        every { fragment.view } returns
            mockk {
                every { context } returns testContext
            }
        every { settings.getBrowserToolbarHeight(testContext) } returns 11
        every { settings.toolbarPosition } returns ToolbarPosition.TOP
        every { settings.isDynamicToolbarEnabled } returns true
        every { settings.shouldUseExpandedToolbar } returns true
        every { testContext.resources.getDimensionPixelSize(R.dimen.browser_navbar_height) } returns 22
        every { testContext.resources.configuration } returns configuration

        fragment.reinitializeEngineView()

        verify { fragment.initializeEngineView(0, 0) }
    }

    @Test
    fun `shouldAddBlackScreen returns true when all conditions are met`() {
        every { testContext.components.settings.privateBrowsingModeLocked } returns true
        every { testContext.components.appStore.state.mode.isPrivate } returns true
        every { testContext.components.core.store.state } returns BrowserState(systemPermissionRequestInProgress = true)
        fragment.blackScreenOverlay = null

        assertTrue(fragment.shouldAddBlackScreen())
    }

    @Test
    fun `shouldAddBlackScreen returns false when private mode lock feature is off`() {
        every { testContext.components.settings.privateBrowsingModeLocked } returns false
        every { testContext.components.appStore.state.mode.isPrivate } returns true
        every { testContext.components.core.store.state } returns BrowserState(systemPermissionRequestInProgress = true)
        fragment.blackScreenOverlay = null

        assertFalse(fragment.shouldAddBlackScreen())
    }

    @Test
    fun `shouldAddBlackScreen returns false when not in private mode`() {
        every { testContext.components.settings.privateBrowsingModeLocked } returns true
        every { testContext.components.appStore.state.mode.isPrivate } returns false
        every { testContext.components.core.store.state } returns BrowserState(systemPermissionRequestInProgress = true)
        fragment.blackScreenOverlay = null

        assertFalse(fragment.shouldAddBlackScreen())
    }

    @Test
    fun `shouldAddBlackScreen returns false when permission request is not in progress`() {
        every { testContext.components.settings.privateBrowsingModeLocked } returns true
        every { testContext.components.appStore.state.mode.isPrivate } returns true
        every { testContext.components.core.store.state } returns
            BrowserState(systemPermissionRequestInProgress = false)
        fragment.blackScreenOverlay = null

        assertFalse(fragment.shouldAddBlackScreen())
    }

    @Test
    fun `shouldAddBlackScreen returns false when black screen overlay already exists`() {
        every { testContext.components.settings.privateBrowsingModeLocked } returns true
        every { testContext.components.appStore.state.mode.isPrivate } returns true
        every { testContext.components.core.store.state } returns BrowserState(systemPermissionRequestInProgress = true)
        fragment.blackScreenOverlay = mockk()

        assertFalse(fragment.shouldAddBlackScreen())
    }

    @Test
    fun `addBlackScreen adds black screen overlay and sets it`() {
        assertNull(fragment.blackScreenOverlay)

        fragment.addBlackScreen(container)

        verify { container.addView(any()) }
        assertNotNull(fragment.blackScreenOverlay)
    }

    @Test
    fun `removeBlackScreen removes black screen overlay and nullifies it`() {
        fragment.addBlackScreen(container)
        assertNotNull(fragment.blackScreenOverlay)

        fragment.removeBlackScreen(container)

        verify { container.removeView(any()) }
        assertNull(fragment.blackScreenOverlay)
    }

    @Test
    fun `setAlwaysOpenInAppPreferences sets both app links and YouTube preferences to always`() {
        every { testContext.getString(R.string.pref_key_open_links_in_apps_always) } returns "apps_always"
        every { testContext.getString(R.string.pref_key_open_links_in_youtube_always) } returns "youtube_always"

        fragment.setAlwaysOpenInAppPreferences()

        verify { settings.openLinksInExternalApp = "apps_always" }
        verify { settings.openLinksInYoutube = "youtube_always" }
    }
}

private class TestBaseBrowserFragment : BaseBrowserFragment() {
    override fun getContextMenuCandidates(
        context: Context,
        view: View,
    ): List<ContextMenuCandidate> {
        // no-op
        return emptyList()
    }

    override fun navToQuickSettingsSheet(tab: SessionState, sitePermissions: SitePermissions?) {
        // no-op
    }
}
