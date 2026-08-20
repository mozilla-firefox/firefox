/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabpage

import kotlin.test.Test
import kotlin.test.assertEquals

class TabCollectionSemanticsTest {

    @Test
    fun `GIVEN a list layout WHEN collection info is read THEN every item is its own row`() {
        val semantics = TabCollectionSemantics(itemCount = 4, columns = 1)

        assertEquals(expected = 4, actual = semantics.collectionInfo.rowCount)
        assertEquals(expected = 1, actual = semantics.collectionInfo.columnCount)
    }

    @Test
    fun `GIVEN a grid layout that fills every row WHEN collection info is read THEN the rows are exact`() {
        val semantics = TabCollectionSemantics(itemCount = 6, columns = 3)

        assertEquals(expected = 2, actual = semantics.collectionInfo.rowCount)
        assertEquals(expected = 3, actual = semantics.collectionInfo.columnCount)
    }

    @Test
    fun `GIVEN a grid layout with a partial last row WHEN collection info is read THEN the partial row is counted`() {
        assertEquals(
            expected = 2,
            actual = TabCollectionSemantics(itemCount = 5, columns = 3).collectionInfo.rowCount,
        )
        assertEquals(
            expected = 3,
            actual = TabCollectionSemantics(itemCount = 7, columns = 3).collectionInfo.rowCount,
        )
    }

    @Test
    fun `GIVEN a single item in a grid WHEN collection info is read THEN there is one row`() {
        val semantics = TabCollectionSemantics(itemCount = 1, columns = 3)

        assertEquals(expected = 1, actual = semantics.collectionInfo.rowCount)
    }

    @Test
    fun `GIVEN an empty collection WHEN collection info is read THEN there are no rows`() {
        val semantics = TabCollectionSemantics(itemCount = 0, columns = 3)

        assertEquals(expected = 0, actual = semantics.collectionInfo.rowCount)
        assertEquals(expected = 3, actual = semantics.collectionInfo.columnCount)
    }

    @Test
    fun `GIVEN a list layout WHEN item info is read THEN the position is the row and the column is always zero`() {
        val semantics = TabCollectionSemantics(itemCount = 4, columns = 1)

        val itemInfo = semantics.itemInfo(position = 3)

        assertEquals(expected = 3, actual = itemInfo.rowIndex)
        assertEquals(expected = 0, actual = itemInfo.columnIndex)
        assertEquals(expected = 1, actual = itemInfo.rowSpan)
        assertEquals(expected = 1, actual = itemInfo.columnSpan)
    }

    @Test
    fun `GIVEN a grid layout WHEN item info is read THEN the position maps onto a row and column`() {
        val semantics = TabCollectionSemantics(itemCount = 6, columns = 3)

        val firstRow = semantics.itemInfo(position = 1)
        assertEquals(expected = 0, actual = firstRow.rowIndex)
        assertEquals(expected = 1, actual = firstRow.columnIndex)

        val secondRow = semantics.itemInfo(position = 4)
        assertEquals(expected = 1, actual = secondRow.rowIndex)
        assertEquals(expected = 1, actual = secondRow.columnIndex)
    }

    @Test
    fun `GIVEN a grid layout WHEN item info is read for a row boundary THEN the column wraps`() {
        val semantics = TabCollectionSemantics(itemCount = 6, columns = 3)

        val lastOfFirstRow = semantics.itemInfo(position = 2)
        assertEquals(expected = 0, actual = lastOfFirstRow.rowIndex)
        assertEquals(expected = 2, actual = lastOfFirstRow.columnIndex)

        val firstOfSecondRow = semantics.itemInfo(position = 3)
        assertEquals(expected = 1, actual = firstOfSecondRow.rowIndex)
        assertEquals(expected = 0, actual = firstOfSecondRow.columnIndex)
    }

    @Test
    fun `GIVEN a full width item in a grid WHEN item info is read THEN it spans every column`() {
        val columns = 3
        val semantics = TabCollectionSemantics(itemCount = 6, columns = columns)

        val itemInfo = semantics.itemInfo(position = 3, columnSpan = columns)

        assertEquals(expected = 1, actual = itemInfo.rowIndex)
        assertEquals(expected = 0, actual = itemInfo.columnIndex)
        assertEquals(expected = 1, actual = itemInfo.rowSpan)
        assertEquals(expected = columns, actual = itemInfo.columnSpan)
    }

    @Test
    fun `GIVEN a grid with reserved slots for a full width item WHEN collection info is read THEN the slots add a row`() {
        val columns = 3
        val tabCount = 6

        val withoutCard = TabCollectionSemantics(itemCount = tabCount, columns = columns)
        val withCard = TabCollectionSemantics(itemCount = tabCount + columns, columns = columns)

        assertEquals(expected = 2, actual = withoutCard.collectionInfo.rowCount)
        assertEquals(expected = 3, actual = withCard.collectionInfo.rowCount)
    }
}
