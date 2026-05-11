/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler.StateInfo
import mozilla.components.feature.ipprotection.Authorized
import mozilla.components.feature.ipprotection.IPProtectionAction
import mozilla.components.feature.ipprotection.IPProtectionState
import mozilla.components.feature.ipprotection.IPProtectionStore
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import org.junit.Assert.assertEquals
import org.junit.Test
import org.mozilla.fenix.components.menu.store.IPProtectionMenuStatus
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.menu.store.MenuState
import org.mozilla.fenix.components.menu.store.MenuStore

class IPProtectionMenuBindingTest {
    private val testDispatcher = StandardTestDispatcher()

    @Test
    fun `GIVEN proxy is uninitialized WHEN binding starts THEN dispatch Disabled status`() = runTest {
        val (captureMiddleware, menuStore) = createMenuStore()
        val ipProtectionStore = IPProtectionStore()

        startBinding(ipProtectionStore, menuStore)

        captureMiddleware.assertLastAction(MenuAction.UpdateIPProtectionMenuState::class) {
            assertEquals(IPProtectionMenuStatus.Disabled, it.state.status)
        }
    }

    @Test
    fun `GIVEN proxy is active WHEN binding starts THEN dispatch Enabled status`() = runTest {
        val (captureMiddleware, menuStore) = createMenuStore()
        val ipProtectionStore = IPProtectionStore(
            initialState = IPProtectionState(proxyStatus = Authorized.Active),
        )

        startBinding(ipProtectionStore, menuStore)

        captureMiddleware.assertLastAction(MenuAction.UpdateIPProtectionMenuState::class) {
            assertEquals(IPProtectionMenuStatus.Enabled, it.state.status)
        }
    }

    @OptIn(ExperimentalAndroidComponentsApi::class)
    @Test
    fun `WHEN proxy status changes THEN dispatch updated menu state`() = runTest {
        val (captureMiddleware, menuStore) = createMenuStore()
        val ipProtectionStore = IPProtectionStore()

        startBinding(ipProtectionStore, menuStore)

        captureMiddleware.assertLastAction(MenuAction.UpdateIPProtectionMenuState::class) {
            assertEquals(IPProtectionMenuStatus.Disabled, it.state.status)
        }

        ipProtectionStore.dispatch(
            IPProtectionAction.EngineStateChanged(
                StateInfo(
                    serviceState = StateInfo.SERVICE_STATE_READY,
                    proxyState = StateInfo.PROXY_STATE_ACTIVE,
                ),
            ),
        )
        testDispatcher.scheduler.advanceUntilIdle()

        captureMiddleware.assertLastAction(MenuAction.UpdateIPProtectionMenuState::class) {
            assertEquals(IPProtectionMenuStatus.Enabled, it.state.status)
        }
    }

    @OptIn(ExperimentalAndroidComponentsApi::class)
    @Test
    fun `WHEN proxy status maps to menu status THEN all statuses are mapped correctly`() = runTest {
        val cases = listOf(
            StateInfo(serviceState = StateInfo.SERVICE_STATE_UNINITIALIZED) to IPProtectionMenuStatus.Disabled,
            StateInfo(
                serviceState = StateInfo.SERVICE_STATE_READY,
                proxyState = StateInfo.PROXY_STATE_READY,
            ) to IPProtectionMenuStatus.Disabled,
            StateInfo(
                serviceState = StateInfo.SERVICE_STATE_READY,
                proxyState = StateInfo.PROXY_STATE_ACTIVATING,
            ) to IPProtectionMenuStatus.Activating,
            StateInfo(
                serviceState = StateInfo.SERVICE_STATE_READY,
                proxyState = StateInfo.PROXY_STATE_ACTIVE,
            ) to IPProtectionMenuStatus.Enabled,
            StateInfo(
                serviceState = StateInfo.SERVICE_STATE_READY,
                proxyState = StateInfo.PROXY_STATE_PAUSED,
            ) to IPProtectionMenuStatus.DataLimitReached,
            StateInfo(
                serviceState = StateInfo.SERVICE_STATE_READY,
                proxyState = StateInfo.PROXY_STATE_ERROR,
            ) to IPProtectionMenuStatus.ConnectionError,
            StateInfo(
                serviceState = StateInfo.SERVICE_STATE_UNAUTHENTICATED,
            ) to IPProtectionMenuStatus.AuthRequired,
        )

        for ((stateInfo, expectedStatus) in cases) {
            val (captureMiddleware, menuStore) = createMenuStore()
            val ipProtectionStore = IPProtectionStore()

            ipProtectionStore.dispatch(IPProtectionAction.EngineStateChanged(stateInfo))
            testDispatcher.scheduler.advanceUntilIdle()

            startBinding(ipProtectionStore, menuStore)

            captureMiddleware.assertLastAction(MenuAction.UpdateIPProtectionMenuState::class) {
                assertEquals(
                    "StateInfo $stateInfo should map to $expectedStatus",
                    expectedStatus,
                    it.state.status,
                )
            }
        }
    }

    @Test
    fun `GIVEN dataMaxBytes is set WHEN binding starts THEN dispatch correct dataLimitGb`() = runTest {
        val (captureMiddleware, menuStore) = createMenuStore()
        val ipProtectionStore = IPProtectionStore(
            initialState = IPProtectionState(
                proxyStatus = Authorized.Active,
                dataMaxBytes = (5 * BYTES_PER_GB).toLong(),
            ),
        )

        startBinding(ipProtectionStore, menuStore)

        captureMiddleware.assertLastAction(MenuAction.UpdateIPProtectionMenuState::class) {
            assertEquals(5, it.state.dataLimitGb)
        }
    }

    @Test
    fun `GIVEN dataMaxBytes is unavailable WHEN binding starts THEN dispatch dataLimitGb as -1`() = runTest {
        val (captureMiddleware, menuStore) = createMenuStore()
        val ipProtectionStore = IPProtectionStore(
            initialState = IPProtectionState(proxyStatus = Authorized.Active, dataMaxBytes = -1L),
        )

        startBinding(ipProtectionStore, menuStore)

        captureMiddleware.assertLastAction(MenuAction.UpdateIPProtectionMenuState::class) {
            assertEquals(-1, it.state.dataLimitGb)
        }
    }

    private fun createMenuStore(): Pair<CaptureActionsMiddleware<MenuState, MenuAction>, MenuStore> {
        val captureMiddleware = CaptureActionsMiddleware<MenuState, MenuAction>()
        val store = MenuStore(
            initialState = MenuState(),
            middleware = listOf(captureMiddleware),
        )
        return captureMiddleware to store
    }

    private fun startBinding(ipProtectionStore: IPProtectionStore, menuStore: MenuStore) {
        val binding = IPProtectionMenuBinding(
            ipProtectionStore = ipProtectionStore,
            menuStore = menuStore,
            mainDispatcher = testDispatcher,
        )
        binding.start()
        testDispatcher.scheduler.advanceUntilIdle()
    }
}
