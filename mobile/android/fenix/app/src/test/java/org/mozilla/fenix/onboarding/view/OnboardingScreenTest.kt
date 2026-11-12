/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class OnboardingScreenTest {

    @Test
    fun `GIVEN an index within the pages range WHEN resumedPageIndex is called THEN it returns the saved index`() {
        assertEquals(2, resumedPageIndex(pageIndex = 2, pageCount = 4))
    }

    @Test
    fun `GIVEN an index on the first page WHEN resumedPageIndex is called THEN it returns the saved index`() {
        assertEquals(0, resumedPageIndex(pageIndex = 0, pageCount = 4))
    }

    @Test
    fun `GIVEN an index on the last page WHEN resumedPageIndex is called THEN it returns the saved index`() {
        assertEquals(3, resumedPageIndex(pageIndex = 3, pageCount = 4))
    }

    @Test
    fun `GIVEN the number of pages reduced past the index WHEN resumedPageIndex is called THEN it returns the last available page index`() {
        assertEquals(2, resumedPageIndex(pageIndex = 5, pageCount = 3))
    }

    @Test
    fun `GIVEN an index one past the last page WHEN resumedPageIndex is called THEN it returns the last available page index`() {
        assertEquals(3, resumedPageIndex(pageIndex = 4, pageCount = 4))
    }

    @Test
    fun `GIVEN a negative index WHEN resumedPageIndex is called THEN it returns the first page index`() {
        assertEquals(0, resumedPageIndex(pageIndex = -1, pageCount = 4))
    }

    @Test
    fun `GIVEN no pages to display WHEN resumedPageIndex is called THEN it returns the first page index`() {
        assertEquals(0, resumedPageIndex(pageIndex = 3, pageCount = 0))
    }

    @Test
    fun `GIVEN signed in on the sign in sync page WHEN scrollToNextCardFromSignIn is called THEN it returns true`() {
        val pages = listOf(termsOfServicePageUiData, syncPageUiData)

        assertTrue(scrollToNextCardFromSignIn(pages, currentPageIndex = 1, isSignedIn = true))
    }

    @Test
    fun `GIVEN signed in on the pairing sync page WHEN scrollToNextCardFromSignIn is called THEN it returns true`() {
        val pages = listOf(termsOfServicePageUiData, syncPairingPageUiData)

        assertTrue(scrollToNextCardFromSignIn(pages, currentPageIndex = 1, isSignedIn = true))
    }

    @Test
    fun `GIVEN not signed in on the sign in sync page WHEN scrollToNextCardFromSignIn is called THEN it returns false`() {
        val pages = listOf(termsOfServicePageUiData, syncPageUiData)

        assertFalse(scrollToNextCardFromSignIn(pages, currentPageIndex = 1, isSignedIn = false))
    }

    @Test
    fun `GIVEN not signed in on the pairing sync page WHEN scrollToNextCardFromSignIn is called THEN it returns false`() {
        val pages = listOf(termsOfServicePageUiData, syncPairingPageUiData)

        assertFalse(scrollToNextCardFromSignIn(pages, currentPageIndex = 1, isSignedIn = false))
    }

    @Test
    fun `GIVEN signed in on a page that is not a sync page WHEN scrollToNextCardFromSignIn is called THEN it returns false`() {
        val pages = listOf(termsOfServicePageUiData, syncPairingPageUiData)

        assertFalse(scrollToNextCardFromSignIn(pages, currentPageIndex = 0, isSignedIn = true))
    }

    @Test
    fun `GIVEN signed in on the second sync page WHEN scrollToNextCardFromSignIn is called THEN it returns true`() {
        val pages = listOf(syncPageUiData, syncPairingPageUiData)

        assertTrue(scrollToNextCardFromSignIn(pages, currentPageIndex = 1, isSignedIn = true))
    }
}
