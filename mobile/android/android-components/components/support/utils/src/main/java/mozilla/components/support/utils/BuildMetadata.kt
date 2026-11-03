/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils

import android.content.res.AssetManager
import java.io.IOException
import java.util.Properties

/**
 * Metadata about the app's build that is only known once the build has run, read from the `build-metadata.properties`
 * asset written by the `WriteBuildMetadata` Gradle task. Builds without the asset (e.g. non-minified builds) have no
 * metadata.
 *
 * @property proguardUuid UUID of the build's R8/ProGuard mapping file, as referenced by crash reports and profiles.
 * @property r8MappingHash SHA-256 of the build's R8 mapping file, from its `pg_map_hash` header.
 */
data class BuildMetadata(
    val proguardUuid: String? = null,
    val r8MappingHash: String? = null,
) {
    companion object {
        internal const val FILENAME = "build-metadata.properties"
        internal const val PROGUARD_UUID_PROPERTY = "proguard.uuid"
        internal const val R8_MAPPING_HASH_PROPERTY = "r8.pg_map_hash"

        /** Reads the build metadata from the app's assets. */
        fun fromAssets(assets: AssetManager): BuildMetadata =
            try {
                assets.open(FILENAME).use { stream ->
                    val properties = Properties().apply { load(stream) }
                    BuildMetadata(
                        proguardUuid = properties.nonBlank(PROGUARD_UUID_PROPERTY),
                        r8MappingHash = properties.nonBlank(R8_MAPPING_HASH_PROPERTY),
                    )
                }
            } catch (_: IOException) {
                BuildMetadata()
            }

        private fun Properties.nonBlank(key: String) = getProperty(key)?.takeIf { it.isNotBlank() }
    }
}
