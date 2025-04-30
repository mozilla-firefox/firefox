/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.distributions

import io.mockk.unmockkObject
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.Config
import org.mozilla.fenix.helpers.FenixRobolectricTestRunner
import org.robolectric.shadows.ShadowBuild

@RunWith(FenixRobolectricTestRunner::class)
class DistributionIdManagerTest {

    private var providerValue: String? = null
    private var storedId: String? = null

    private val testDistributionProviderChecker = object : DistributionProviderChecker {
        override fun queryProvider(): String? = providerValue
    }

    private val testBrowserStoreProvider = object : DistributionBrowserStoreProvider {
        override fun getDistributionId(): String? = storedId

        override fun updateDistributionId(id: String) {
            storedId = id
        }
    }

    @After
    fun tearDown() {
        providerValue = null
        storedId = null
        unmockkObject(Config)
        ShadowBuild.reset()
    }

    @Test
    fun `WHEN a device is made by vivo AND the vivo distribution file is found THEN the proper id is returned`() {
        val subject = DistributionIdManager(
            testContext,
            testBrowserStoreProvider,
            testDistributionProviderChecker,
            appPreinstalledOnVivoDevice = { true },
        )

        // Mock Build.MANUFACTURER to simulate a Vivo device
        ShadowBuild.setManufacturer("vivo")

        val distributionId = subject.getDistributionId()

        assertEquals("vivo-001", distributionId)
    }

    @Test
    fun `WHEN a device is not made by vivo AND the vivo distribution file is found THEN the proper id is returned`() {
        val subject = DistributionIdManager(
            testContext,
            testBrowserStoreProvider,
            testDistributionProviderChecker,
            appPreinstalledOnVivoDevice = { true },
        )

        val distributionId = subject.getDistributionId()

        assertEquals("Mozilla", distributionId)
    }

    @Test
    fun `WHEN a device is made by vivo AND the vivo distribution file is not found THEN the proper id is returned`() {
        val subject = DistributionIdManager(
            testContext,
            testBrowserStoreProvider,
            testDistributionProviderChecker,
            appPreinstalledOnVivoDevice = { false },
        )

        // Mock Build.MANUFACTURER to simulate a Vivo device
        ShadowBuild.setManufacturer("vivo")

        val distributionId = subject.getDistributionId()

        assertEquals("Mozilla", distributionId)
    }

    @Test
    fun `WHEN the device is not vivo AND the channel is not mozilla online THEN the proper id is returned`() {
        val subject = DistributionIdManager(
            testContext,
            testBrowserStoreProvider,
            testDistributionProviderChecker,
        )

        val distributionId = subject.getDistributionId()

        assertEquals("Mozilla", distributionId)
    }

    @Test
    fun `WHEN the browser stores state already has a distribution Id assigned THEN that ID gets returned`() {
        val subject = DistributionIdManager(
            testContext,
            testBrowserStoreProvider,
            testDistributionProviderChecker,
        )

        storedId = "testId"

        val distributionId = subject.getDistributionId()

        assertEquals("testId", distributionId)
    }

    @Test
    fun `WHEN the provider is digital_tubrine AND the DT app is installed THEN the proper ID is returned`() {
        val subject = DistributionIdManager(
            testContext,
            testBrowserStoreProvider,
            testDistributionProviderChecker,
            isDtTelefonicaInstalled = { true },
        )

        providerValue = "digital_turbine"
        val distributionId = subject.getDistributionId()

        assertEquals("dt-001", distributionId)
    }

    @Test
    fun `WHEN the provider is not digital_tubrine AND the DT app is installed THEN the proper ID is returned`() {
        val subject = DistributionIdManager(
            testContext,
            testBrowserStoreProvider,
            testDistributionProviderChecker,
            isDtTelefonicaInstalled = { true },
        )

        providerValue = "some_provider"
        val distributionId = subject.getDistributionId()

        assertEquals("Mozilla", distributionId)
    }

    @Test
    fun `WHEN the provider is digital_tubrine AND the DT app is not installed THEN the proper ID is returned`() {
        val subject = DistributionIdManager(
            testContext,
            testBrowserStoreProvider,
            testDistributionProviderChecker,
            isDtTelefonicaInstalled = { false },
        )

        providerValue = "digital_turbine"
        val distributionId = subject.getDistributionId()

        assertEquals("Mozilla", distributionId)
    }

    @Test
    fun `WHEN the provider is not digital_tubrine AND the DT app is not installed THEN the proper ID is returned`() {
        val subject = DistributionIdManager(
            testContext,
            testBrowserStoreProvider,
            testDistributionProviderChecker,
            isDtTelefonicaInstalled = { false },
        )

        providerValue = "some_provider"
        val distributionId = subject.getDistributionId()

        assertEquals("Mozilla", distributionId)
    }

    @Test
    fun `WHEN the provider is null AND the DT app is installed THEN the proper ID is returned`() {
        val subject = DistributionIdManager(
            testContext,
            testBrowserStoreProvider,
            testDistributionProviderChecker,
            isDtTelefonicaInstalled = { true },
        )

        providerValue = null
        val distributionId = subject.getDistributionId()

        assertEquals("Mozilla", distributionId)
    }

    @Test
    fun `WHEN the provider is null AND the DT app is not installed THEN the proper ID is returned`() {
        val subject = DistributionIdManager(
            testContext,
            testBrowserStoreProvider,
            testDistributionProviderChecker,
            isDtTelefonicaInstalled = { false },
        )

        providerValue = null
        val distributionId = subject.getDistributionId()

        assertEquals("Mozilla", distributionId)
    }

    @Test
    fun `WHEN the distribution is not default or mozilla online THEN the distribution is from a deal`() {
        val subject = DistributionIdManager(
            testContext,
            testBrowserStoreProvider,
            testDistributionProviderChecker,
        )

        testBrowserStoreProvider.updateDistributionId(DistributionIdManager.Distribution.VIVO_001.id)
        assertEquals(true, subject.isPartnershipDistribution())

        testBrowserStoreProvider.updateDistributionId(DistributionIdManager.Distribution.DT_001.id)
        assertEquals(true, subject.isPartnershipDistribution())
    }

    @Test
    fun `WHEN the provider is aura THEN the proper distribution ID is returned`() {
        val subject = DistributionIdManager(
            testContext,
            testBrowserStoreProvider,
            testDistributionProviderChecker,
        )

        providerValue = "aura"
        val distributionId = subject.getDistributionId()

        assertEquals("aura-001", distributionId)
    }
}
