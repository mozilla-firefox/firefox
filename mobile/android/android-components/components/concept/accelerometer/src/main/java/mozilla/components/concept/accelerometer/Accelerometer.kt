/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.accelerometer

import kotlinx.coroutines.flow.Flow

/**
 * An accelerometer for measuring changes to a devices motion.
 */
interface Accelerometer {
    /**
     * The flow of data.
     */
    fun samples(): Flow<Accelerometer.Sample>

    /**
     * A simple data type containing acceleration data at a specific time.
     */
    data class Sample(
        val xAccel: Float,
        val yAccel: Float,
        val zAccel: Float,
        val timestampNs: Long,
    )
}
