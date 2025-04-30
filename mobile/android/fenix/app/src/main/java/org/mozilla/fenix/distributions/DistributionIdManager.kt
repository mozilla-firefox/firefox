/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.distributions

import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.annotation.VisibleForTesting
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.GleanMetrics.Partnerships
import java.io.File
import java.util.Locale

/**
 * This file will be present on vivo devices that have Firefox preinstalled.
 * Note: The file is added by the manufacturer.
 */
private const val VIVO_PREINSTALLED_FIREFOX_FILE_PATH = "/data/yzfswj/another/vivo_firefox.txt"
private const val VIVO_MANUFACTURER = "vivo"

private const val DT_PROVIDER = "digital_turbine"
private const val DT_TELEFONICA_PACKAGE = "com.dti.telefonica"

private const val AURA_PROVIDER = "aura"

private val logger = Logger(DistributionIdManager::class.simpleName)

/**
 * Class used to manage distribution Ids for distribution deals with third parties
 *
 * @param context application context
 * @param browserStoreProvider used to update and fetch the stored distribution Id
 * @param distributionProviderChecker used for checking content providers for a distribution provider
 * @param appPreinstalledOnVivoDevice checks if the vivo preinstalled file exists.
 * @param isDtTelefonicaInstalled checks if the DT telefonica app is installed on the device
 */
class DistributionIdManager(
    private val context: Context,
    private val browserStoreProvider: DistributionBrowserStoreProvider,
    private val distributionProviderChecker: DistributionProviderChecker,
    private val appPreinstalledOnVivoDevice: () -> Boolean = { wasAppPreinstalledOnVivoDevice() },
    private val isDtTelefonicaInstalled: () -> Boolean = { isDtTelefonicaInstalled(context) },
) {

    /**
     * Gets the distribution Id that is used to specify which distribution deal this install
     * is associated with.
     *
     * @return the distribution ID if one exists.
     */
    fun getDistributionId(): String {
        browserStoreProvider.getDistributionId()?.let { return it }

        val provider = distributionProviderChecker.queryProvider()

        val distributionId = when {
            isProviderDigitalTurbine(provider) && isDtTelefonicaInstalled() -> Distribution.DT_001.id
            isProviderAura(provider) -> Distribution.AURA_001.id
            isDeviceVivo() && appPreinstalledOnVivoDevice() -> Distribution.VIVO_001.id
            else -> Distribution.DEFAULT.id
        }

        browserStoreProvider.updateDistributionId(distributionId)

        return distributionId
    }

    /**
     * Check if the distribution is part of a distribution deal
     *
     * @return true if the distribution is part of a distribution deal
     */
    fun isPartnershipDistribution(): Boolean {
        val id = Distribution.fromId(getDistributionId())

        return when (id) {
            Distribution.DEFAULT -> false
            Distribution.VIVO_001 -> true
            Distribution.DT_001 -> true
            Distribution.AURA_001 -> true
        }
    }

    private fun isDeviceVivo(): Boolean {
        return Build.MANUFACTURER?.lowercase(Locale.getDefault())?.contains(VIVO_MANUFACTURER) ?: false
    }

    private fun isProviderDigitalTurbine(provider: String?): Boolean = provider == DT_PROVIDER

    private fun isProviderAura(provider: String?): Boolean = provider == AURA_PROVIDER

    /**
     * This enum represents distribution IDs that are used in glean metrics.
     */
    @VisibleForTesting
    internal enum class Distribution(val id: String) {
        DEFAULT(id = "Mozilla"),
        VIVO_001(id = "vivo-001"),
        DT_001(id = "dt-001"),
        AURA_001(id = "aura-001"),
        ;

        companion object {
            fun fromId(id: String): Distribution {
                return entries.find { it.id == id } ?: DEFAULT
            }
        }
    }
}

/**
 * Checks for a file in the device that indicates if the app was preinstalled on a vivo device
 */
private fun wasAppPreinstalledOnVivoDevice(): Boolean {
    return try {
        File(VIVO_PREINSTALLED_FIREFOX_FILE_PATH).exists()
    } catch (e: SecurityException) {
        logger.error("File access denied", e)
        Partnerships.vivoFileCheckError.record()
        false
    }
}

/**
 * Checks if the Digital Turbine Telefonica app exists on the device
 */
private fun isDtTelefonicaInstalled(context: Context): Boolean {
    val packages = context.packageManager.getInstalledPackages(PackageManager.GET_META_DATA)
    return packages.any { it.packageName == DT_TELEFONICA_PACKAGE }
}
