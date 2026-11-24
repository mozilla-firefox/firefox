/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
@file:Suppress("DEPRECATION")

package org.mozilla.fenix.helpers

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.wifi.WifiManager
import android.os.Build
import android.telephony.TelephonyManager
import android.util.Log
import androidx.test.platform.app.InstrumentationRegistry
import org.mozilla.fenix.ext.isOnline
import org.mozilla.fenix.helpers.Constants.TAG

object NetworkConnectionStatusHelper {
    // Get the context of the app to access system services
    val context = InstrumentationRegistry.getInstrumentation().targetContext

    // Get the ConnectivityManager system service to manage network connections
    val connectivityManager = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

    // All networks currently available. Computed on each access, never cached: the device's
    // connections change at runtime (e.g. when a test toggles Wi-Fi/data), and a value captured once
    // would report stale state forever after.
    val allNetworks: Array<Network>
        get() = connectivityManager.allNetworks

    // The active network the device is currently connected to, or null when offline. Computed on each
    // access for the same reason as allNetworks.
    val activeNetwork: Network?
        get() = connectivityManager.activeNetwork

    // Whether the device currently has a validated internet connection, re-read live on each call.
    // This is the same signal the app itself uses to decide it is offline (INTERNET + VALIDATED
    // capabilities), which makes it the right oracle for "has the network actually come up/down yet".
    fun isDeviceOnline(): Boolean = connectivityManager.isOnline()

    // Whether the ACTIVE network is WiFi with validated internet. On enable, cellular and WiFi come
    // up together and the OS can default to cellular until WiFi validates; this is the signal for
    // "WiFi has taken over as the active connection", preferred because cellular can be unreliable.
    fun isWifiActiveAndValidated(): Boolean {
        val capabilities = connectivityManager.getNetworkCapabilities(activeNetwork) ?: return false
        return capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) &&
            capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
            capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
    }

    // Best-effort diagnostic: logs the active network type, every available network and its signal
    // strength, and whether the device has validated internet. Wrapped so it can never throw -- this
    // runs in the test setup/teardown path, and a logging hiccup (e.g. a signal-strength read failing
    // right after an interface comes up) must not fail or mask a test.
    fun getNetworkDetails() {
        runCatching {
            // Read the live state once per call (activeNetwork/allNetworks are computed, not cached).
            val activeNetworkCapabilities = connectivityManager.getNetworkCapabilities(activeNetwork)
            val activeType =
                when {
                    activeNetworkCapabilities == null -> "No active network"
                    activeNetworkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> "WiFi"
                    activeNetworkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) -> "Cellular"
                    else -> "Unknown"
                }
            Log.i(TAG, "getNetworkDetails: The device's active network is: $activeType")
            Log.i(TAG, "getNetworkDetails: Device has validated internet: ${isDeviceOnline()}")

            for (network in allNetworks) {
                val networkCapabilities = connectivityManager.getNetworkCapabilities(network)
                when {
                    networkCapabilities == null -> Log.i(TAG, "getNetworkDetails: No Network Connection")
                    networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> {
                        val wifiStrength = runCatching { getWifiSignalStrength(context) }.getOrDefault("N/A")
                        Log.i(TAG, "getNetworkDetails: Connected to WiFi. Signal Strength: $wifiStrength")
                    }
                    networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) -> {
                        val signalStrength = runCatching { getCellularSignalStrength(context) }.getOrDefault("N/A")
                        Log.i(TAG, "getNetworkDetails: Connected to Cellular Network. Signal Strength: $signalStrength")
                    }
                    else -> Log.i(TAG, "getNetworkDetails: Connected to Unknown Network")
                }
            }
        }
            .onFailure { Log.i(TAG, "getNetworkDetails: failed to gather network details: ${it.message}") }
    }

    fun checkActiveNetworkState(enabled: Boolean): Boolean {
        // Get the network capabilities of the active network (null when there is no active network)
        val activeNetworkCapabilities = connectivityManager.getNetworkCapabilities(activeNetwork)

        // The active network is considered connected when it carries a WiFi or Cellular transport;
        // no active network (null capabilities) means not connected.
        val isConnected =
            activeNetworkCapabilities?.let {
                it.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) ||
                    it.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)
            } ?: false

        val connectionType =
            when {
                activeNetworkCapabilities == null -> "None"
                activeNetworkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> "WiFi"
                activeNetworkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) -> "Cellular"
                else -> "Unknown"
            }
        Log.i(
            TAG,
            "checkActiveNetworkState: Active network is ${if (isConnected) "connected" else "not connected"} ($connectionType)",
        )
        // Return true when the observed connection state matches the expected one. Unlike before, an
        // offline device (null capabilities) correctly matches enabled = false.
        return enabled == isConnected
    }

    // Function to get WiFi signal strength based on the RSSI (Received Signal Strength Indicator)
    fun getWifiSignalStrength(context: Context): String {
        // Get the WifiManager system service to access WiFi information
        val wifiManager = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        // Get the current RSSI (Received Signal Strength Indicator) value of the connected WiFi network
        val rssi = wifiManager.connectionInfo.rssi
        // Determine the WiFi signal strength based on the RSSI value
        return when {
            // If the RSSI is greater than -50 dBm, the signal is considered "Excellent"
            rssi > -50 -> "Excellent"
            // If the RSSI is between -50 and -60 dBm, the signal is considered "Good"
            rssi > -60 -> "Good"
            // If the RSSI is between -60 and -70 dBm, the signal is considered "Fair"
            rssi > -70 -> "Fair"
            // If the RSSI is less than -70 dBm, the signal is considered "Weak"
            else -> "Weak"
        }
    }

    // Function to get the cellular network signal strength based on the current connection
    fun getCellularSignalStrength(context: Context): String {
        // getSignalStrength() added only in API Level 28
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            // Get the TelephonyManager system service to access cellular network information
            val telephonyManager = context.getSystemService(Context.TELEPHONY_SERVICE) as TelephonyManager
            // Get the current signal strength object which contains signal strength information
            val signalStrength = telephonyManager.signalStrength

            // Return the signal quality based on the signal level
            return if (signalStrength != null) {
                val level = signalStrength.level
                when {
                    // Excellent signal if the level is 4 (strongest signal)
                    level >= 4 -> "Excellent"
                    // Good signal for level 3
                    level == 3 -> "Good"
                    // Fair signal for level 2
                    level == 2 -> "Fair"
                    // Weak signal for level 1 or below
                    else -> "Weak"
                }
            } else {
                "N/A"
            }
        } else {
            return ("API level is lower than 28, can't get the signal strength")
        }
    }
}
