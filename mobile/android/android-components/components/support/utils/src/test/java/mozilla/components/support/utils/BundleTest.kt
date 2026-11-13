/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils

import android.os.Bundle
import android.os.Parcel
import android.os.Parcelable
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.support.utils.ext.getParcelableArrayCompat
import mozilla.components.support.utils.ext.safeCastToArrayOfT
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.annotation.Config

@RunWith(AndroidJUnit4::class)
class BundleTest {

    @Test
    fun `safeCastToArrayOfT with expected elements`() {
        val testArray = Array<Parcelable>(4) { Expected() }

        val result = testArray.safeCastToArrayOfT<Expected>()

        assertArrayEquals(testArray, result)
    }

    @Test
    fun `safeCastToArrayOfT with unexpected elements returns empty array and does not throw exception`() {
        val testArray = Array<Parcelable>(4) { Unexpected() }

        val result = testArray.safeCastToArrayOfT<Expected>()

        assertArrayEquals(emptyArray(), result)
    }

    @Test
    fun `safeCastToArrayOfT with expected and unexpected elements returns array with only expected`() {
        val testArray = Array<Parcelable>(4) { Expected() } + Unexpected()

        val result = testArray.safeCastToArrayOfT<Expected>()

        assertEquals(4, result.size)
        assertTrue(result.isArrayOf<Expected>())
    }

    @Test
    fun `getParcelableArrayCompat with expected type`() {
        val bundle = Bundle()

        val testArray = Array(4) { Expected() }

        bundle.putParcelableArray(KEY, testArray)

        val result = bundle.getParcelableArrayCompat(KEY, Expected::class.java)

        assertArrayEquals(testArray, result)
    }

    @Test
    @Config(sdk = [32])
    fun `getParcelableArrayCompat with unexpected type returns empty array and does not throw exception on SDK 32 and below`() {
        val bundle = Bundle()

        val testArray = Array(4) { Unexpected() }

        bundle.putParcelableArray(KEY, testArray)

        val result = bundle.getParcelableArrayCompat(KEY, Expected::class.java)

        assertArrayEquals(emptyArray(), result)
    }

    @Test
    @Config(sdk = [32])
    fun `getParcelableArrayCompat with both expected unexpected type returns array with only expected on SDK 32 and below`() {
        val bundle = Bundle()

        val testArray = Array<Parcelable>(4) { Expected() } + Unexpected()

        bundle.putParcelableArray(KEY, testArray)

        val result = bundle.getParcelableArrayCompat(KEY, Expected::class.java)

        assertEquals(4, result?.size)
        assertTrue(result?.isArrayOf<Expected>() == true)
    }

    companion object {
        private const val KEY = "test key"

        class Expected : Parcelable {
            override fun describeContents(): Int {
                return 0
            }

            override fun writeToParcel(parcel: Parcel, flags: Int) {
                parcel.writeString("good")
            }
        }

        private class Unexpected : Parcelable {
            override fun describeContents(): Int {
                return 0
            }

            override fun writeToParcel(parcel: Parcel, flags: Int) {
                parcel.writeString("wrong")
            }
        }
    }
}
