/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.conventions

import com.charleskorn.kaml.Yaml
import kotlinx.serialization.Serializable
import org.gradle.api.Project
import org.gradle.api.provider.Provider
import java.io.File

@Serializable
data class GleanIndex(
    val metrics_files: List<String>,
    val ping_files: List<String>,
    val tag_files: List<String>,
) {
    companion object {
        @JvmStatic
        fun getGleanYamlFiles(project: Project): Provider<List<String>> {
            val mozconfig = project.gradle.extensions.extraProperties["mozconfig"] as Map<*, *>
            val topsrcdir = mozconfig["topsrcdir"] as String
            val childDirs = project.childProjects.values.map { it.projectDir }
            return project.providers
                .fileContents(project.layout.projectDirectory.file("glean_index.yaml"))
                .asText
                .map { text ->
                    val index = Yaml.default.decodeFromString(serializer(), text)
                    (index.metrics_files + index.ping_files + index.tag_files)
                        .map { File(topsrcdir, it) }
                        .filter { file -> childDirs.none { file.startsWith(it) } }
                        .map { it.path }
                }
        }
    }
}
