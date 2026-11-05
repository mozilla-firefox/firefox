/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Prevents gradle builds from looking for a root settings.gradle

pluginManagement {
    if (gradle.parent?.extra?.has("mozconfig") == true) {
        gradle.extra["mozconfig"] = gradle.parent!!.extra["mozconfig"]
        gradle.extra["configureMavenRepositories"] = gradle.parent!!.extra["configureMavenRepositories"]
    } else {
        apply(from = file("../../../gradle/mozconfig.gradle"))
    }

    val configureMavenRepositories = gradle.extra["configureMavenRepositories"] as groovy.lang.Closure<*>

    repositories {
        configureMavenRepositories.call(this)
    }

    includeBuild("../dependencies")
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.PREFER_SETTINGS)

    val configureMavenRepositories = gradle.extra["configureMavenRepositories"] as groovy.lang.Closure<*>

    repositories {
        configureMavenRepositories.call(this)
    }
    versionCatalogs {
        create("libs") {
            from(files("../../../../../gradle/libs.versions.toml"))
        }
    }
}
