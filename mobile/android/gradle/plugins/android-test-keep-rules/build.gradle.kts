/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

plugins {
    `kotlin-dsl`
}

val mozconfig = gradle.extra["mozconfig"] as Map<*, *>
val topobjdir = mozconfig["topobjdir"] as String

layout.buildDirectory.set(
    file("$topobjdir/gradle/build/mobile/android/gradle/plugins/android-test-keep-rules"),
)

gradlePlugin {
    plugins.register("org.mozilla.android-test-keep-rules") {
        id = "org.mozilla.android-test-keep-rules"
        implementationClass = "org.mozilla.androidtestkeeprules.AndroidTestKeepRulesPlugin"
    }
}

tasks.withType<org.jetbrains.kotlin.gradle.tasks.KotlinCompile>().configureEach {
    compilerOptions {
        allWarningsAsErrors.set(true)
    }
}

dependencies {
    compileOnly(libs.android.gradle.plugin)
    testImplementation(libs.android.gradle.plugin)
    testImplementation(platform(libs.junit.bom))
    testImplementation(libs.junit.jupiter)
    testRuntimeOnly(libs.junit.platform.launcher)
}

tasks.test {
    useJUnitPlatform()
}
