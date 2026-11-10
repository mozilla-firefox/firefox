/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.importer

import android.net.Uri
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.Test
import kotlin.test.assertEquals
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ImporterReducerTest {

    @Test
    fun `GIVEN a picked file WHEN FileSelected is reduced THEN the state is Loading`() {
        assertEquals(ImporterState.Loading, importerReducer(ImporterAction.FileSelected(uri = Uri.EMPTY)))
    }

    @Test
    fun `GIVEN no picked file WHEN FileSelected is reduced THEN the import finishes as Canceled`() {
        assertEquals(
            ImporterState.Finished(result = ImporterEvent.Canceled),
            importerReducer(ImporterAction.FileSelected(uri = null)),
        )
    }
}
