/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.continuous

import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.temporal.ChronoUnit
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.DateTimeProvider
import mozilla.components.support.utils.DefaultDateTimeProvider
import org.mozilla.fenix.utils.Settings

// Remove this file when ContinuousOnboardingFeature is retired, see Continuous-onboarding-via-messaging.md.
/** Determines the [ContinuousOnboardingStage] for the current user onboarding journey. */
interface ContinuousOnboardingStageProvider {
    /** Returns the [ContinuousOnboardingStage] for the current user onboarding journey. */
    fun getContinuousOnboardingStage(): ContinuousOnboardingStage
}

private const val ONE_DAY = 1L
private const val TWO_DAYS = 2L

/**
 * Default implementation of [ContinuousOnboardingStageProvider].
 *
 * @param settings The [Settings] used to read continuous onboarding state.
 * @param dateTimeProvider The [DateTimeProvider] used to get the current time.
 * @param zoneId The [ZoneId] used to resolve timestamps to local dates.
 */
class ContinuousOnboardingStageProviderDefault(
    private val settings: Settings,
    private val dateTimeProvider: DateTimeProvider = DefaultDateTimeProvider(),
    private val zoneId: ZoneId = ZoneId.systemDefault(),
) : ContinuousOnboardingStageProvider {
    private val logger = Logger("ContinuousOnboardingStageProviderDefault")

    override fun getContinuousOnboardingStage(): ContinuousOnboardingStage =
        with(settings) {
            if (onboardingCompletedTimestamp == -1L) {
                logger.info("Initial onboarding has not been completed.")
                return ContinuousOnboardingStage.NONE
            }

            val today = dateTimeProvider.currentTimeMillis().toLocalDate(zoneId)

            return when {
                shouldShowDay2(today, zoneId) -> ContinuousOnboardingStage.DAY_2
                shouldShowDay3(today, zoneId) -> ContinuousOnboardingStage.DAY_3
                shouldShowDay5(today, zoneId) -> ContinuousOnboardingStage.DAY_5
                shouldShowDay7(today, zoneId) -> ContinuousOnboardingStage.DAY_7
                else -> ContinuousOnboardingStage.NONE
            }
        }

    private fun Settings.shouldShowDay2(today: LocalDate, zoneId: ZoneId): Boolean {
        val result =
            secondDayOnboardingCompletedTimestamp == -1L &&
                onboardingCompletedTimestamp.daysElapsedTo(today, zoneId) >= ONE_DAY
        logger.info("shouldShowDay2: $result")
        return result
    }

    private fun Settings.shouldShowDay3(today: LocalDate, zoneId: ZoneId): Boolean {
        val result =
            thirdDayOnboardingCompletedTimestamp == -1L &&
                secondDayOnboardingCompletedTimestamp != -1L &&
                secondDayOnboardingCompletedTimestamp.daysElapsedTo(today, zoneId) >= ONE_DAY
        logger.info("shouldShowDay3: $result")
        return result
    }

    private fun Settings.shouldShowDay5(today: LocalDate, zoneId: ZoneId): Boolean {
        val result =
            fifthDayOnboardingCompletedTimestamp == -1L &&
                thirdDayOnboardingCompletedTimestamp != -1L &&
                thirdDayOnboardingCompletedTimestamp.daysElapsedTo(today, zoneId) >= TWO_DAYS
        logger.info("shouldShowDay5: $result")
        return result
    }

    private fun Settings.shouldShowDay7(today: LocalDate, zoneId: ZoneId): Boolean {
        val result =
            seventhDayOnboardingCompletedTimestamp == -1L &&
                fifthDayOnboardingCompletedTimestamp != -1L &&
                fifthDayOnboardingCompletedTimestamp.daysElapsedTo(today, zoneId) >= TWO_DAYS
        logger.info("shouldShowDay7: $result")
        return result
    }

    private fun Long.daysElapsedTo(today: LocalDate, zoneId: ZoneId) =
        ChronoUnit.DAYS.between(toLocalDate(zoneId), today)

    private fun Long.toLocalDate(zoneId: ZoneId): LocalDate = Instant.ofEpochMilli(this).atZone(zoneId).toLocalDate()
}
