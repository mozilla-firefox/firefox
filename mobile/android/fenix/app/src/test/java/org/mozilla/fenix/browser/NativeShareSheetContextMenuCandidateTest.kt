/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import io.mockk.mockk
import mozilla.components.feature.contextmenu.ContextMenuCandidate
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class NativeShareSheetContextMenuCandidateTest {

    @Test
    fun `WHEN additional tab candidates are provided THEN they are added after the open in tab candidates`() {
        val additionalCandidate =
            ContextMenuCandidate(
                id = "test.candidate",
                label = "Test candidate",
                showFor = { _, _ -> true },
                action = { _, _ -> },
            )

        val candidates =
            NativeShareSheetContextMenuCandidate.defaultCandidates(
                context = testContext,
                tabsUseCases = mockk(relaxed = true),
                contextMenuUseCases = mockk(relaxed = true),
                shareUseCases = mockk(relaxed = true),
                getShareItems = { emptyList() },
                snackBarParentView = mockk(relaxed = true),
                snackbarDelegate = mockk(relaxed = true),
                downloadsLocation = { "downloads" },
                navigateToShareFragment = { _, _ -> },
                additionalTabCandidates = listOf(additionalCandidate),
            )

        assertEquals(
            listOf(
                "mozac.feature.contextmenu.open_in_new_tab",
                "mozac.feature.contextmenu.open_in_private_tab",
                "test.candidate",
                "mozac.feature.contextmenu.copy_link",
            ),
            candidates.take(4).map { it.id },
        )
    }
}
