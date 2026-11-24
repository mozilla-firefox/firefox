/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

plugins {
    `kotlin-dsl`
    alias(libs.plugins.android.lint.plugin)
}

val mozconfig = gradle.extra["mozconfig"] as Map<*, *>
val topobjdir = mozconfig["topobjdir"] as String

layout.buildDirectory.set(file("$topobjdir/gradle/build/mobile/android/fenix/plugins/apksize"))

tasks.withType<org.jetbrains.kotlin.gradle.tasks.KotlinCompile>().configureEach {
    compilerOptions {
        allWarningsAsErrors.set(true)
    }
}

dependencies {
    implementation(libs.android.gradle.plugin)
    implementation(libs.json)

    lintChecks(libs.androidx.lint)

    testImplementation(platform(libs.junit.bom))
    testImplementation(libs.junit.jupiter)
    testRuntimeOnly(libs.junit.platform.launcher)
}

tasks.test {
    useJUnitPlatform()
}

gradlePlugin {
    plugins.register("ApkSizePlugin") {
        id = "ApkSizePlugin"
        implementationClass = "ApkSizePlugin"
    }
}
