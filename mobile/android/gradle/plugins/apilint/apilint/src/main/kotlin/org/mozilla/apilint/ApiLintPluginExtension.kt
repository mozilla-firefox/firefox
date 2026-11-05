/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.apilint

import groovy.lang.Closure
import javax.inject.Inject
import org.gradle.api.model.ObjectFactory
import org.gradle.api.provider.ListProperty
import org.gradle.api.provider.Property
import org.gradle.kotlin.dsl.property

abstract class ApiLintPluginExtension @Inject constructor(objects: ObjectFactory) {
    abstract val packageFilter: Property<String>
    abstract val apiOutputFileName: Property<String>
    abstract val currentApiRelativeFilePath: Property<String>
    abstract val jsonResultFileName: Property<String>
    abstract val skipClassesRegex: ListProperty<String>

    abstract val changelogFileName: Property<String>
    abstract val lintFilters: ListProperty<String>
    abstract val allowedPackages: ListProperty<String>
    abstract val deprecationAnnotation: Property<String>
    abstract val libraryVersion: Property<Int>

    // When API differences exist, print this command.  Takes a single
    // `variantName` argument.  Running this command manually should invoke the
    // `apiUpdateFile...` command to update the API file so that the API
    // differences are incorporated into the expected API.
    val helpCommand: Property<(String) -> String> = objects.property<(String) -> String>()

    init {
        packageFilter.convention(".") // By default all packages are part of the api
        apiOutputFileName.convention("api.txt")
        currentApiRelativeFilePath.convention("api.txt")
        jsonResultFileName.convention("apilint-result.json")
        skipClassesRegex.convention(emptyList())
        helpCommand.convention { variantName ->
            "\$ ./mach gradle apiUpdateFile$variantName"
        }
    }

    fun setHelpCommand(closure: Closure<*>) {
        helpCommand.set { variantName ->
            closure.call(variantName).toString()
        }
    }
}
