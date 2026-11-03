/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils

import android.content.res.AssetManager
import java.io.ByteArrayInputStream
import java.io.FileNotFoundException
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Test
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.doThrow

class BuildMetadataTest {

    private fun assetsWith(contents: String): AssetManager {
        val assets: AssetManager = mock()
        doReturn(ByteArrayInputStream(contents.toByteArray())).`when`(assets).open(BuildMetadata.FILENAME)
        return assets
    }

    @Test
    fun `WHEN the asset has both values THEN they are read`() {
        val metadata =
            BuildMetadata.fromAssets(
                assetsWith(
                    "proguard.uuid=9bc515f8-e1af-3eeb-8d1f-293d25734486\n" +
                        "r8.pg_map_hash=7574790ae7934a00b7257c067f2218cd59730d705346757ba754271c688777e7\n"
                )
            )

        assertEquals(
            BuildMetadata(
                proguardUuid = "9bc515f8-e1af-3eeb-8d1f-293d25734486",
                r8MappingHash = "7574790ae7934a00b7257c067f2218cd59730d705346757ba754271c688777e7",
            ),
            metadata,
        )
    }

    @Test
    fun `WHEN a value is missing or blank THEN it is null`() {
        val metadata = BuildMetadata.fromAssets(assetsWith("proguard.uuid=\n"))

        assertEquals(BuildMetadata(), metadata)
    }

    @Test
    fun `WHEN the asset is missing THEN there is no metadata`() {
        val assets: AssetManager = mock()
        doThrow(FileNotFoundException()).`when`(assets).open(BuildMetadata.FILENAME)

        assertEquals(BuildMetadata(), BuildMetadata.fromAssets(assets))
    }
}
