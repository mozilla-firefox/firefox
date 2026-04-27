/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.autofill

import android.content.Context
import android.view.autofill.AutofillManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.doThrow
import org.mockito.Mockito.never
import org.mockito.Mockito.reset
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class AutofillUseCasesTest {
    @Test
    fun testIsSupported() {
        val context: Context = mock()
        val autofillManager: AutofillManager = mock()
        doReturn(autofillManager).`when`(context).getSystemService(AutofillManager::class.java)
        doReturn(true).`when`(autofillManager).isAutofillSupported

        assertTrue(AutofillUseCases().isSupported(context))
    }

    @Test
    fun testIsNotSupported() {
        val context: Context = mock()
        val autofillManager: AutofillManager = mock()
        doReturn(autofillManager).`when`(context).getSystemService(AutofillManager::class.java)
        doReturn(false).`when`(autofillManager).isAutofillSupported

        assertFalse(AutofillUseCases().isSupported(context))
    }

    @Test
    fun testThrows() {
        val context: Context = mock()
        doThrow(RuntimeException()).`when`(context).getSystemService(AutofillManager::class.java)

        assertFalse(AutofillUseCases().isSupported(context))
    }

    @Test
    fun testIsEnabled() {
        val context: Context = mock()
        val autofillManager: AutofillManager = mock()
        doReturn(autofillManager).`when`(context).getSystemService(AutofillManager::class.java)
        doReturn(true).`when`(autofillManager).hasEnabledAutofillServices()

        assertTrue(AutofillUseCases().isEnabled(context))
    }

    @Test
    fun testIsNotEnabled() {
        val context: Context = mock()
        val autofillManager: AutofillManager = mock()
        doReturn(autofillManager).`when`(context).getSystemService(AutofillManager::class.java)
        doReturn(false).`when`(autofillManager).hasEnabledAutofillServices()

        assertFalse(AutofillUseCases().isEnabled(context))
    }

    @Test
    fun testEnable() {
        val context: Context = mock()

        AutofillUseCases().enable(context)
        verify(context).startActivity(any())
        reset(context)
    }

    @Test
    fun testDisable() {
        val context: Context = mock()
        val autofillManager: AutofillManager = mock()
        doReturn(autofillManager).`when`(context).getSystemService(AutofillManager::class.java)

        AutofillUseCases().disable(context)
        verify(autofillManager).disableAutofillServices()
        reset(autofillManager)
    }
}
