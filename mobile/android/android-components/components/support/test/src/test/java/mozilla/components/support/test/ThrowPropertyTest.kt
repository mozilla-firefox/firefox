/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.test.robolectric.mozilla.components.support.test

import mozilla.components.support.test.ThrowProperty
import org.junit.Assert.fail
import org.junit.Test

class ThrowPropertyTest {
    private val testProperty = "test"

    @Test(expected = UnsupportedOperationException::class)
    fun `exception thrown when get value is called`() {
        val throwProperty = ThrowProperty<String>()
        fail("getValue returned ${throwProperty.getValue(testProperty, ::testProperty)}")
    }

    @Test(expected = UnsupportedOperationException::class)
    fun `exception thrown when set value is called`() {
        val throwProperty = ThrowProperty<String>()
        throwProperty.setValue(testProperty, ::testProperty, "test1")
    }
}
