/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.digitalassetlinks.local

import mozilla.components.service.digitalassetlinks.AssetDescriptor
import mozilla.components.service.digitalassetlinks.Relation
import mozilla.components.service.digitalassetlinks.Statement
import mozilla.components.service.digitalassetlinks.StatementListFetcher
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class StatementRelationCheckerTest {

    @Test
    fun `checks list lazily`() {
        var numYields = 0
        val target = AssetDescriptor.Web("https://mozilla.org")
        val listFetcher =
            object : StatementListFetcher {
                override fun listStatements(source: AssetDescriptor.Web) = sequence {
                    numYields = 1
                    yield(
                        Statement(
                            relation = Relation.USE_AS_ORIGIN,
                            target = target,
                        )
                    )
                    numYields = 2
                    yield(
                        Statement(
                            relation = Relation.USE_AS_ORIGIN,
                            target = target,
                        )
                    )
                }
            }

        val checker = StatementRelationChecker(listFetcher)
        assertEquals(0, numYields)

        assertTrue(checker.checkRelationship(mock(), Relation.USE_AS_ORIGIN, target))
        assertEquals(1, numYields)

        // Sanity check that the mock can yield twice
        numYields = 0
        assertEquals(2, listFetcher.listStatements(mock()).count())
        assertEquals(2, numYields)
    }

    @Test
    fun `fails if relation does not match`() {
        val target = AssetDescriptor.Android("com.test", "AA:BB")
        val listFetcher =
            object : StatementListFetcher {
                override fun listStatements(source: AssetDescriptor.Web) =
                    sequenceOf(
                        Statement(
                            relation = Relation.USE_AS_ORIGIN,
                            target = target,
                        )
                    )
            }

        val checker = StatementRelationChecker(listFetcher)
        assertFalse(checker.checkRelationship(mock(), Relation.HANDLE_ALL_URLS, target))
    }

    @Test
    fun `fails if target does not match`() {
        val target = AssetDescriptor.Web("https://mozilla.org")
        val listFetcher =
            object : StatementListFetcher {
                override fun listStatements(source: AssetDescriptor.Web) =
                    sequenceOf(
                        Statement(
                            relation = Relation.HANDLE_ALL_URLS,
                            target = AssetDescriptor.Web("https://mozilla.com"),
                        ),
                        Statement(
                            relation = Relation.HANDLE_ALL_URLS,
                            target = AssetDescriptor.Web("http://mozilla.org"),
                        ),
                    )
            }

        val checker = StatementRelationChecker(listFetcher)
        assertFalse(checker.checkRelationship(mock(), Relation.HANDLE_ALL_URLS, target))
    }
}
