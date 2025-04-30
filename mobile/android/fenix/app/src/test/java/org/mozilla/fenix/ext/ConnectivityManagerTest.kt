/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ext

import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import io.mockk.every
import io.mockk.mockk
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.helpers.FenixRobolectricTestRunner

@RunWith(FenixRobolectricTestRunner::class)
class ConnectivityManagerTest {

    private lateinit var connectivityManager: ConnectivityManager

    @Before
    fun setup() {
        connectivityManager = mockk(relaxed = true)
    }

    @Test
    fun `connectManager is online works`() {
        val network = mockk<Network>()
        val networkCapabilities = mockk<NetworkCapabilities>()

        every { connectivityManager.getNetworkCapabilities(network) } returns networkCapabilities
        every { networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) } returns true
        every { networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED) } returns true

        assertTrue(connectivityManager.isOnline(network))
    }

    @Test
    fun `connectManager is online with null network works`() {
        val network: Network? = null
        val networkCapabilities = mockk<NetworkCapabilities>()

        every { connectivityManager.getNetworkCapabilities(network) } returns networkCapabilities
        every { networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) } returns true
        every { networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED) } returns true

        assertFalse(connectivityManager.isOnline(network))
    }

    @Test
    fun `connectManager is online with unvalidated connection works`() {
        val network = mockk<Network>()
        val networkCapabilities = mockk<NetworkCapabilities>()

        every { connectivityManager.getNetworkCapabilities(network) } returns networkCapabilities
        every { networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) } returns true
        every { networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED) } returns false

        assertFalse(connectivityManager.isOnline(network))
    }

    @Test
    fun `connectManager is online with no connection works`() {
        val network = mockk<Network>()
        val networkCapabilities = mockk<NetworkCapabilities>()

        every { connectivityManager.getNetworkCapabilities(network) } returns networkCapabilities
        every { networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) } returns false
        every { networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED) } returns true

        assertFalse(connectivityManager.isOnline(network))
    }
}
