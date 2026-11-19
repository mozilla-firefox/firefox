/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

plugins {
    `kotlin-dsl`
    alias(libs.plugins.android.lint.plugin)
    // kotlin-dsl builds against Gradle's embedded Kotlin, and warns if another version is applied.
    id("org.jetbrains.kotlin.plugin.serialization") version embeddedKotlinVersion
}

group = "org.mozilla"

val mozconfig = gradle.extra["mozconfig"] as Map<*, *>
val topobjdir = mozconfig["topobjdir"] as String

layout.buildDirectory.set(file("$topobjdir/gradle/build/mobile/android/gradle/plugins/conventions"))

gradlePlugin {
    plugins.register("org.mozilla.conventions.settings") {
        id = "org.mozilla.conventions.settings"
        implementationClass = "org.mozilla.conventions.SettingsPlugin"
    }
    plugins.register("org.mozilla.conventions.project") {
        id = "org.mozilla.conventions.project"
        implementationClass = "org.mozilla.conventions.ProjectPlugin"
    }
    plugins.register("org.mozilla.conventions.mach-tasks") {
        id = "org.mozilla.conventions.mach-tasks"
        implementationClass = "org.mozilla.conventions.MachTasksPlugin"
    }
    plugins.register("org.mozilla.conventions.zip-test-reports") {
        id = "org.mozilla.conventions.zip-test-reports"
        implementationClass = "org.mozilla.conventions.ZipTestReportsPlugin"
    }
}

tasks.withType<org.jetbrains.kotlin.gradle.tasks.KotlinCompile>().configureEach {
    compilerOptions {
        allWarningsAsErrors.set(true)
    }
}

dependencies {
    compileOnly(libs.scip.kotlin.analysis)
    implementation(libs.kaml)
    implementation(libs.android.gradle.plugin)
    implementation(libs.kotlin.gradle.plugin)
    implementation(libs.spotless.plugin)
    implementation(libs.tomlj)
    testImplementation(platform(libs.junit.bom))
    testImplementation(libs.junit.jupiter)
    testImplementation(libs.mockito)
    testRuntimeOnly(libs.junit.platform.launcher)

    lintChecks(libs.androidx.lint)
}

tasks.test {
    useJUnitPlatform()
}
