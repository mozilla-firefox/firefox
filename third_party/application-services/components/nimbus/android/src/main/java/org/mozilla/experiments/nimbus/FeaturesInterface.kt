/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.experiments.nimbus

import android.content.Context
import android.content.SharedPreferences
import org.mozilla.experiments.nimbus.internal.FeatureUpdateDispatcher

/**
 * Small interface to get the feature variables out of the Nimbus SDK.
 *
 * This is intended to be standalone to allow for testing the Nimbus FML.
 */
interface FeaturesInterface {

    val context: Context

    val prefs: SharedPreferences?
        get() = null

    /**
     * Get the variables needed to configure the feature given by `featureId`.
     *
     * @param featureId The string feature id that identifies to the feature under experiment.
     *
     * @param recordExposureEvent Passing `true` to this parameter will record the exposure event
     *      automatically if the client is enrolled in an experiment for the given [featureId].
     *      Passing `false` here indicates that the application will manually record the exposure
     *      event by calling the `recordExposureEvent` function at the time of the exposure to the
     *      feature.
     *
     * See [recordExposureEvent] for more information on manually recording the event.
     *
     * @return a [Variables] object used to configure the feature.
     */
    fun getVariables(featureId: String, recordExposureEvent: Boolean = true): Variables

    /**
     * Records the `exposure` event in telemetry.
     *
     * This is a manual function to accomplish the same purpose as passing `true` as the
     * `recordExposureEvent` property of the [getVariables] function. It is intended to be used
     * when requesting feature variables must occur at a different time than the actual user's
     * exposure to the feature within the app.
     *
     * Examples:
     * * If the [Variables] are needed at a different time than when the exposure to the feature
     *   actually happens, such as constructing a menu happening at a different time than the user
     *   seeing the menu.
     * * If [getVariables] is required to be called multiple times for the same feature and it is
     *   desired to only record the exposure once, such as if [getVariables] were called with every
     *   keystroke.
     *
     * In the case where the use of this function is required, then the [getVariables] function
     * should be called with `false` so that the exposure event is not recorded when the variables
     * are fetched.
     *
     * This function is safe to call even when there is no active experiment for the feature. The SDK
     * will ensure that an event is only recorded for active experiments.
     *
     * @param featureId string representing the id of the feature for which to record the exposure
     *     event.
     */
    fun recordExposureEvent(featureId: String, experimentSlug: String? = null) = Unit

    /**
     * Records the `malformedFeature` event in telemetry.
     *
     * In the event that some or part of a feature's configuration is not semantically valid,
     * this method should be called, to record a telemetry event.
     *
     * Note: the application developers should use [FeatureHolder.recordMalformedConfiguration]
     * method, which calls this one.
     *
     * @param featureId String
     * @param partId The feature specific identifier for which part is malformed (e.g. message or
     *  card).
     */
    fun recordMalformedConfiguration(featureId: String, partId: String) = Unit

    /**
     * Return the feature update dispatcher.
     */
    fun getFeatureUpdateDispatcher(): FeatureUpdateDispatcher? = null
}
