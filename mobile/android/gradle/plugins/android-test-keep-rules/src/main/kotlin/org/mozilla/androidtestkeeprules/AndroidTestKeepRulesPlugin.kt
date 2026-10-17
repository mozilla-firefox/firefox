/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.androidtestkeeprules

import com.android.build.api.artifact.ScopedArtifact
import com.android.build.api.variant.ApplicationAndroidComponentsExtension
import com.android.build.api.variant.DeviceTestBuilder
import com.android.build.api.variant.ScopedArtifacts
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.kotlin.dsl.register

/**
 * Keeps the app code that instrumented tests use when they run against a minified build.
 *
 * R8 only sees the app when it minifies it, so it removes or renames code that only the androidTest APK calls, and the
 * tests then crash at runtime. For the build type passed as `-PtestBuildType`, this plugin traces what the androidTest
 * classes reference in the app and adds keep rules for it to that variant's R8 configuration. Builds that don't pass
 * `-PtestBuildType` are left untouched, so shipping builds are unaffected.
 */
class AndroidTestKeepRulesPlugin : Plugin<Project> {
    override fun apply(project: Project) {
        val testBuildType = project.providers.gradleProperty("testBuildType").orNull ?: return

        project.pluginManager.withPlugin("com.android.application") {
            val androidComponents = project.extensions.getByType(ApplicationAndroidComponentsExtension::class.java)
            androidComponents.onVariants(androidComponents.selector().withBuildType(testBuildType)) { variant ->
                val androidTest = variant.deviceTests[DeviceTestBuilder.ANDROID_TEST_TYPE]
                if (!variant.isMinifyEnabled || androidTest == null) {
                    return@onVariants
                }

                val taskName = "generate${variant.name.replaceFirstChar { it.uppercase() }}AndroidTestKeepRules"
                val task = project.tasks.register<GenerateAndroidTestKeepRules>(taskName) {
                    bootClasspath.from(androidComponents.sdkComponents.bootClasspath)
                    outputFile.set(
                        project.layout.buildDirectory.file(
                            "intermediates/android_test_keep_rules/${variant.name}/android-test-keep-rules.pro",
                        ),
                    )
                }

                variant.artifacts
                    .forScope(ScopedArtifacts.Scope.ALL)
                    .use(task)
                    .toGet(
                        ScopedArtifact.CLASSES,
                        GenerateAndroidTestKeepRules::appJars,
                        GenerateAndroidTestKeepRules::appDirs,
                    )
                androidTest.artifacts
                    .forScope(ScopedArtifacts.Scope.ALL)
                    .use(task)
                    .toGet(
                        ScopedArtifact.CLASSES,
                        GenerateAndroidTestKeepRules::testJars,
                        GenerateAndroidTestKeepRules::testDirs,
                    )

                variant.proguardFiles.add(task.flatMap { it.outputFile })
            }
        }
    }
}
