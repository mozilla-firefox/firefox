/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins

import androidx.lifecycle.Lifecycle.State
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.Test
import kotlin.test.assertEquals
import mozilla.components.feature.importer.ImporterEvent
import org.junit.runner.RunWith
import org.mozilla.fenix.helpers.lifecycle.TestLifecycleOwner

@RunWith(AndroidJUnit4::class)
class ImportPasswordsDialogFragmentTest {

    private var dismissals = 0

    @Test
    fun `GIVEN the fragment is started WHEN the import finishes THEN the dialog is dismissed`() {
        val owner = TestLifecycleOwner(State.STARTED)

        dismissWhenFinished(ImporterEvent.Success(importCount = 3), owner.lifecycle) { dismissals++ }

        assertEquals(1, dismissals)
    }

    @Test
    fun `GIVEN the fragment has stopped WHEN the import finishes THEN the dialog is not dismissed`() {
        val owner = TestLifecycleOwner(State.CREATED)

        dismissWhenFinished(ImporterEvent.Success(importCount = 3), owner.lifecycle) { dismissals++ }

        assertEquals(0, dismissals)
    }

    @Test
    fun `GIVEN the import finished while stopped WHEN the fragment starts again THEN it is dismissed once`() {
        val owner = TestLifecycleOwner(State.CREATED)

        dismissWhenFinished(ImporterEvent.Success(importCount = 3), owner.lifecycle) { dismissals++ }
        owner.onStart()

        assertEquals(1, dismissals)
    }

    @Test
    fun `GIVEN the import finished while stopped WHEN the fragment is destroyed THEN it is not dismissed`() {
        val owner = TestLifecycleOwner(State.CREATED)

        dismissWhenFinished(ImporterEvent.Success(importCount = 3), owner.lifecycle) { dismissals++ }
        owner.onDestroy()

        assertEquals(0, dismissals)
    }

    @Test
    fun `GIVEN the fragment is started WHEN the import only started THEN the dialog stays open`() {
        val owner = TestLifecycleOwner(State.STARTED)

        dismissWhenFinished(ImporterEvent.Started, owner.lifecycle) { dismissals++ }

        assertEquals(0, dismissals)
    }
}
