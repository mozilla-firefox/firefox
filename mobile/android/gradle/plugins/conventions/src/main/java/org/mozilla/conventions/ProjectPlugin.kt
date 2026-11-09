/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.conventions

import com.android.build.api.artifact.SingleArtifact
import com.android.build.api.dsl.ApplicationExtension
import com.android.build.api.dsl.CommonExtension
import com.android.build.api.variant.ApplicationAndroidComponentsExtension
import com.android.build.api.variant.FilterConfiguration
import org.gradle.api.Action
import org.gradle.api.GradleException
import org.gradle.api.JavaVersion
import org.gradle.api.Plugin
import org.gradle.api.Project
import org.gradle.api.artifacts.Configuration
import org.gradle.api.artifacts.DependencySubstitution
import org.gradle.api.artifacts.VersionCatalogsExtension
import org.gradle.api.artifacts.component.ModuleComponentIdentifier
import org.gradle.api.artifacts.component.ModuleComponentSelector
import org.gradle.api.artifacts.component.ProjectComponentIdentifier
import org.gradle.api.logging.Logger
import org.gradle.api.logging.StandardOutputListener
import org.gradle.api.artifacts.ArtifactView
import org.gradle.api.artifacts.component.ComponentIdentifier
import org.gradle.api.artifacts.transform.InputArtifact
import org.gradle.api.artifacts.transform.TransformAction
import org.gradle.api.artifacts.transform.TransformOutputs
import org.gradle.api.artifacts.transform.TransformParameters
import org.gradle.api.artifacts.transform.TransformSpec
import org.gradle.api.artifacts.type.ArtifactTypeDefinition
import org.gradle.api.file.FileSystemLocation
import org.gradle.api.plugins.AppliedPlugin
import org.gradle.api.provider.Property
import org.gradle.api.provider.Provider
import org.gradle.api.specs.Spec
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.JavaExec
import org.gradle.api.tasks.Sync
import org.gradle.api.tasks.compile.JavaCompile
import org.gradle.api.tasks.javadoc.Javadoc
import org.gradle.api.tasks.testing.Test
import org.gradle.api.tasks.testing.TestDescriptor
import org.gradle.api.tasks.testing.TestListener
import org.gradle.api.tasks.testing.TestOutputEvent
import org.gradle.api.tasks.testing.TestOutputListener
import org.gradle.api.tasks.testing.TestResult
import org.gradle.kotlin.dsl.getByType
import org.gradle.kotlin.dsl.register
import org.gradle.kotlin.dsl.withType
import org.gradle.process.CommandLineArgumentProvider
import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import org.jetbrains.kotlin.gradle.tasks.KotlinCompile
import org.mozilla.conventions.ktfmt.configureKtfmt
import java.io.DataInputStream
import java.io.EOFException
import java.io.File
import java.util.concurrent.Callable
import java.util.zip.ZipFile

class ProjectPlugin : Plugin<Project> {
    override fun apply(project: Project) {
        val mozilla = project.extensions.create("mozilla", ProjectExtension::class.java)
        mozilla.androidComponentsProject.convention(false)
        mozilla.ktfmtSourcePaths.convention(emptyList())
        mozilla.detektSourcePaths.convention(emptyList())
        mozilla.detektAutoCorrect.convention(true)
        mozilla.detektReports.convention(emptyMap())

        val extraProperties = project.gradle.extensions.extraProperties
        val mozconfig = extraProperties["mozconfig"] as Map<*, *>
        val topsrcdir = mozconfig["topsrcdir"] as String
        val topobjdir = mozconfig["topobjdir"] as String
        val substs = mozconfig["substs"] as Map<*, *>
        val configureMavenRepositories = extraProperties["configureMavenRepositories"] as groovy.lang.Closure<*>

        project.repositories.apply {
            configureMavenRepositories.call(this)
            maven { setUrl("${topobjdir}/gradle/maven") }
        }

        configureBuildDirectory(project, topsrcdir, topobjdir)
        configureJniKeepDebugSymbols(project)
        configureKotlinCompilerMessageReformatting(project)
        configureKotlinWarningsAsErrors(project)
        configureJvmTarget(project)
        configureJavacLint(project)
        configureAndroidBuildToolsVersion(project, substs)
        configureAppServicesSubstitution(project, extraProperties, substs)
        configureGleanSubstitution(project, extraProperties)
        configureGleanVersionResolution(project)
        configureKtfmt(project, mozilla)
        configureDetekt(project, mozilla)
        configureAndroidComponentsLint(project, mozilla, topsrcdir)
        configureRoomImplementationCheck(project)
        configureUnitTests(project)
        configurePackagingResourcesExcludes(project)
        configureBuildMetadata(project)
        registerPrintVariantsTask(project)
        configureNativeLibsForTests(project, substs)
    }

    // Initialize the project buildDir to be in ${topobjdir} to follow
    // conventions of mozilla-central build system.
    private fun configureBuildDirectory(project: Project, topsrcdir: String, topobjdir: String) {
        val topSrcPath = File(topsrcdir).toPath()
        val topObjPath = File(topobjdir).toPath()

        val sourcePath = project.buildFile.toPath().parent
        val relativePath = topSrcPath.relativize(sourcePath)

        if (relativePath.startsWith("..")) {
            // The project doesn't appear to be in topsrcdir so leave the
            // buildDir alone.
        } else {
            // Transplant the project path into "${topobjdir}/gradle/build".
            // This is consistent with existing gradle / taskcluster
            // configurations but less consistent with the result of the
            // non-gradle build system.
            project.layout.buildDirectory.set(topObjPath.resolve("gradle/build").resolve(relativePath).toFile())
        }
    }

    // This explicitly disables stripping of native libraries in our projects to match the existing
    // implicit behaviour. Our projects do not specify the `ndkVersion` for our main Android builds
    // and so stripping would otherwise fail with a warning. Note that gecko builds themselves will
    // already strip the *.so files when compiled as release targets.
    private fun configureJniKeepDebugSymbols(project: Project) {
        val action = Action<AppliedPlugin> {
            val android = project.extensions.getByType<CommonExtension>()
            android.packaging.jniLibs.keepDebugSymbols.add("**/*.so")
        }
        project.pluginManager.withPlugin("com.android.library", action)
        project.pluginManager.withPlugin("com.android.application", action)
    }

    // Querying whether :geckoview is part of the build composition; no project-isolation-safe alternative.
    @Suppress("GradleProjectIsolation")
    private fun configureAppServicesSubstitution(
        project: Project,
        extraProperties: org.gradle.api.plugins.ExtraPropertiesExtension,
        substs: Map<*, *>,
    ) {
        // Only substitute when the a-s subprojects are part of this Gradle build:
        // either :geckoview is included (so the a-s subprojects are too), or we're
        // downloading every Gradle dependency. When m/a/fenix builds on its own
        // (e.g., the second pass of a fat-AAR build), :geckoview isn't in settings
        // and fenix consumes a-s as Maven AARs from target.maven.zip instead of
        // maven.mozilla.org.
        if (substs["MOZ_APPSERVICES_IN_TREE"].isTruthy() && (substs["DOWNLOAD_ALL_GRADLE_DEPENDENCIES"].isTruthy() || project.findProject(":geckoview") != null)) {
            // In tree, so we update our legacy "external" dep name to a local project.
            // e.g., "org.mozilla.appservices:syncmanager:X.Y.Z" becomes project(':syncmanager')
            substituteDependencies(project, APP_SERVICES_GROUPS) { group, module, dependency ->
                var name = module
                // full-megazord-libsForTests is a convenience; we don't lose test coverage,
                // just local test convenience.
                if (name == "full-megazord-libsForTests") {
                    name = "full-megazord"
                }
                dependency.useTarget(project.project(":$name"))
            }
        } else if (extraProperties.has("localProperties.autoPublish.application-services.dir")) {
            substituteWithMavenLocal(project, "local-appservices", APP_SERVICES_GROUPS, "org.mozilla.appservices")
        }
    }

    private fun configureGleanSubstitution(
        project: Project,
        extraProperties: org.gradle.api.plugins.ExtraPropertiesExtension
    ) {
        if (extraProperties.has("localProperties.autoPublish.glean.dir")) {
            substituteWithMavenLocal(project, "local-glean", GLEAN_GROUPS)
        }
    }

    // Extract native libs from libsForTests JARs so transitive .so dependencies
    // (e.g. libmozsqlite3.so needed by libmegazord.so) can be found by the OS
    // dynamic linker during JVM unit tests.
    private fun configureNativeLibsForTests(project: Project, substs: Map<*, *>) {
        if (substs["DOWNLOAD_ALL_GRADLE_DEPENDENCIES"].isTruthy()) {
            return
        }

        val osName = System.getProperty("os.name", "").lowercase()
        val osArch = System.getProperty("os.arch", "").lowercase()
        val osPrefix = when {
            osName.contains("linux") -> "linux"
            osName.contains("mac") || osName.contains("darwin") -> "darwin"
            osName.contains("win") -> "win32"
            else -> return
        }
        val archSuffix = when {
            osArch.contains("aarch64") || osArch.contains("arm64") -> "aarch64"
            osArch.contains("x86_64") || osArch.contains("amd64") -> "x86-64"
            else -> return
        }
        val jnaPlatform = "$osPrefix-$archSuffix"
        val nativeLibsType = "mozilla-native-libs-for-tests"

        // Unpack the libsForTests JAR's native libs through an artifact transform.
        // The transform is cached and only runs for configurations that actually
        // resolve the JAR, so test tasks without the dependency see an empty view.
        project.dependencies.registerTransform(
            ExtractNativeLibsForTests::class.java,
            Action<TransformSpec<ExtractNativeLibsForTests.Parameters>> {
                from.attribute(ArtifactTypeDefinition.ARTIFACT_TYPE_ATTRIBUTE, ArtifactTypeDefinition.JAR_TYPE)
                to.attribute(ArtifactTypeDefinition.ARTIFACT_TYPE_ATTRIBUTE, nativeLibsType)
                parameters.jnaPlatform.set(jnaPlatform)
            }
        )

        val nativeLibs = project.files(Callable {
            project.configurations
                .filter { it.isCanBeResolved && it.name.contains("UnitTestRuntimeClasspath") }
                .map { config ->
                    config.incoming.artifactView(Action<ArtifactView.ViewConfiguration> {
                        attributes.attribute(ArtifactTypeDefinition.ARTIFACT_TYPE_ATTRIBUTE, nativeLibsType)
                        componentFilter(Spec<ComponentIdentifier> { id ->
                            id is ModuleComponentIdentifier && id.module.contains("libsForTests")
                        })
                        lenient(true)
                    }).files
                }
        })

        // Stage the extracted libs into a fixed directory so the test tasks have
        // a stable location to point their library paths at.
        val stageNativeLibs = project.tasks.register(
            "stageNativeLibsForTests",
            Sync::class.java,
            Action<Sync> {
                from(nativeLibs)
                into(project.layout.buildDirectory.dir("nativeLibsForTests"))
            }
        )
        val nativeLibsDir = project.layout.buildDirectory.dir("nativeLibsForTests").get().asFile
        val hostPath = project.providers.environmentVariable("PATH")

        project.tasks.withType(Test::class.java).configureEach(Action<Test> {
            val testTask = this
            testTask.dependsOn(stageNativeLibs)
            testTask.inputs.files(stageNativeLibs).withPropertyName("nativeLibsForTests")
            testTask.jvmArgumentProviders.add(CommandLineArgumentProvider {
                if (nativeLibsDir.list().isNullOrEmpty()) {
                    emptyList()
                } else {
                    listOf(
                        "-Djna.library.path=${nativeLibsDir.absolutePath}",
                        "-Djava.library.path=${nativeLibsDir.absolutePath}",
                    )
                }
            })
            // Windows has no rpath; nss3 loads softokn3/freebl3 through the OS
            // DLL search, so put the staged libs directory on the test JVM PATH.
            if (osPrefix == "win32") {
                testTask.environment(
                    "PATH",
                    "${nativeLibsDir.absolutePath}${File.pathSeparator}${hostPath.getOrElse("")}",
                )
            }
        })
    }

    // Substitutes dependencies to use locally published versions from mavenLocal.
    private fun substituteWithMavenLocal(
        project: Project,
        tag: String,
        groups: Set<String>,
        targetGroup: String? = null
    ) {
        project.logger.lifecycle("[$tag] adjusting $project to use locally published modules ($LOCAL_SNAPSHOT_VERSION)")
        project.repositories.mavenLocal()
        substituteDependencies(project, groups) { group, module, dependency ->
            dependency.useTarget(
                mapOf(
                    "group" to (targetGroup ?: group),
                    "name" to module,
                    "version" to LOCAL_SNAPSHOT_VERSION
                )
            )
        }
    }

    private fun substituteDependencies(
        project: Project,
        groups: Set<String>,
        action: (group: String, module: String, dependency: DependencySubstitution) -> Unit
    ) {
        project.configurations.configureEach(object : Action<Configuration> {
            override fun execute(config: Configuration) {
                if (config.isCanBeResolved) {
                    config.resolutionStrategy.dependencySubstitution.all(object : Action<DependencySubstitution> {
                        override fun execute(dependency: DependencySubstitution) {
                            val requested = dependency.requested
                            if (requested is ModuleComponentSelector && requested.group in groups) {
                                action(requested.group, requested.module, dependency)
                            }
                        }
                    })
                }
            }
        })
    }

    private fun configureKotlinCompilerMessageReformatting(project: Project) {
        // Kotlin compiler message formats:
        // - Current: "e: file.kt:10:5 message" (colon-separated, used by fenix/focus/A-C)
        // - Legacy:  "e: file.kt: (10, 5): message" (parenthesized, used by geckoview)
        val messageFormats = listOf(
            Regex("""([ew]): (.+):(\d+):(\d+) (.*)"""),
            Regex("""([ew]): (.+): \((\d+), (\d+)\): (.*)"""),
        )

        project.tasks.withType<KotlinCompile>().configureEach {
            // Translate Kotlin messages like "w: ..." and "e: ..." into
            // "...: warning: ..." and "...: error: ...", to make Treeherder understand.
            val listener = StandardOutputListener { message ->
                if (message.startsWith("e: warnings found")) {
                    return@StandardOutputListener
                }

                if (message.startsWith("w: ") || message.startsWith("e: ")) {
                    val match = messageFormats.firstNotNullOfOrNull { it.find(message) }
                    if (match == null) {
                        logger.quiet("kotlinc message format has changed!")
                        // For warnings, don't continue because we don't want to throw an
                        // exception. For errors, we want the exception so that the new error
                        // message format gets translated properly.
                        if (message.startsWith("w: ")) {
                            return@StandardOutputListener
                        }
                    }
                    match?.let {
                        val (type, file, line, column, msg) = it.destructured
                        val level = if (type == "w") "warning" else "error"
                        // Use logger.lifecycle, which does not go through stderr again.
                        logger.lifecycle("$file:$line:$column: $level: $msg")
                    }
                }
            }

            doFirst {
                logging.addStandardErrorListener(listener)
            }
            doLast {
                logging.removeStandardErrorListener(listener)
            }
        }
    }

    private fun configureKotlinWarningsAsErrors(project: Project) {
        project.tasks.withType<KotlinCompile>().configureEach {
            compilerOptions {
                allWarningsAsErrors.set(true)
            }
        }
    }

    private fun configureJvmTarget(project: Project) {
        val javaVersion = JavaVersion.VERSION_17
        project.pluginManager.withPlugin("com.android.base") {
            project.extensions.getByType<CommonExtension>().compileOptions.apply {
                sourceCompatibility = javaVersion
                targetCompatibility = javaVersion
            }
        }
        project.pluginManager.withPlugin("java") {
            project.tasks.withType<JavaCompile>().configureEach {
                options.release.set(javaVersion.majorVersion.toInt())
            }
            project.tasks.withType<KotlinCompile>().configureEach {
                compilerOptions.freeCompilerArgs.add("-Xjdk-release=${javaVersion.majorVersion}")
            }
        }
        project.tasks.withType<KotlinCompile>().configureEach {
            compilerOptions {
                jvmTarget.set(JvmTarget.fromTarget(javaVersion.majorVersion))
            }
        }
        if (project.providers.environmentVariable("MOZ_AUTOMATION").isPresent) {
            val javaMajor = javaVersion.majorVersion.toInt()
            project.tasks.withType<JavaCompile>().configureEach {
                val classes = destinationDirectory
                doLast { checkClassFileVersions(classes.get().asFile, javaMajor) }
            }
            project.tasks.withType<KotlinCompile>().configureEach {
                val classes = destinationDirectory
                doLast { checkClassFileVersions(classes.get().asFile, javaMajor) }
            }
        }
        project.tasks.withType<Javadoc>().configureEach {
            options.source = javaVersion.majorVersion
        }
    }

    private fun configureJavacLint(project: Project) {
        project.tasks.withType<JavaCompile>().configureEach {
            options.compilerArgs.addAll(
                listOf(
                    // Turn on all warnings, except...
                    "-Xlint:all",
                    // Deprecation, because we do use deprecated API for compatibility.
                    "-Xlint:-deprecation",
                    // Classfile, because it only reports defects in third-party class files, such as
                    // media3 referencing annotations it doesn't ship, which we can't fix.
                    "-Xlint:-classfile",
                    "-Werror",
                ),
            )
        }
        project.pluginManager.withPlugin("com.android.base") {
            project.tasks.withType<JavaCompile>().configureEach {
                // AGP's generated BuildConfig.java has a dangling doc comment.
                // https://issuetracker.google.com/issues/567587949
                options.compilerArgs.add("-Xlint:-dangling-doc-comments")
            }
        }
    }

    private fun configureAndroidBuildToolsVersion(project: Project, substs: Map<*, *>) {
        val buildToolsVersion = substs["ANDROID_BUILD_TOOLS_VERSION"] as String
        project.pluginManager.withPlugin("com.android.base") {
            project.extensions.getByType<CommonExtension>().buildToolsVersion = buildToolsVersion
        }
    }

    private fun configureGleanVersionResolution(project: Project) {
        // Dependencies can't depend on a different major version of Glean than A-C itself.
        val action = Action<AppliedPlugin> {
            val versionCatalogs = project.extensions.getByType(VersionCatalogsExtension::class.java)
            val libs = versionCatalogs.named("libs")
            val gleanVersion = libs.findVersion("glean").get().requiredVersion

            project.configurations.configureEach {
                resolutionStrategy {
                    eachDependency {
                        if (requested.group == "org.mozilla.telemetry" && requested.name.contains("glean")) {
                            val requestedMajor = requested.version?.split(".")?.firstOrNull()
                            val definedMajor = gleanVersion.split(".").firstOrNull()
                            // Check the major version
                            if (requestedMajor != definedMajor) {
                                throw AssertionError(
                                    "Cannot resolve to a single Glean version. " +
                                        "Requested: ${requested.version}, version catalog defines: $gleanVersion"
                                )
                            } else {
                                // Enforce that all (transitive) dependencies are using the defined Glean version
                                useVersion(gleanVersion)
                            }
                        }
                    }
                    capabilitiesResolution {
                        withCapability("org.mozilla.telemetry:glean-native") {
                            // GeckoView provides Glean either as an in-build project or as a
                            // published module, depending on the build composition.
                            val toBeSelected = candidates.find {
                                when (val id = it.id) {
                                    is ProjectComponentIdentifier -> id.projectName.contains("geckoview")
                                    is ModuleComponentIdentifier -> id.module.contains("geckoview")
                                    else -> false
                                }
                            }
                            if (toBeSelected != null) {
                                select(toBeSelected)
                            }
                            because("use GeckoView Glean instead of standalone Glean")
                        }
                    }
                }
            }
        }
        project.pluginManager.withPlugin("com.android.library", action)
        project.pluginManager.withPlugin("com.android.application", action)
    }

    companion object {
        private const val LOCAL_SNAPSHOT_VERSION = "0.0.1-SNAPSHOT-+"
        private val APP_SERVICES_GROUPS = setOf("org.mozilla.appservices", "org.mozilla.appservices.nightly")
        private val GLEAN_GROUPS = setOf("org.mozilla.telemetry")
    }


    private fun configureDetekt(project: Project, mozilla: ProjectExtension) {
        val sourcePaths = mozilla.detektSourcePaths

        val detektConfig = project.configurations.register("detektCli")
        val detektDep = project.provider {
            val versionCatalogs = project.extensions.getByType(VersionCatalogsExtension::class.java)
            val libs = versionCatalogs.named("libs")
            project.dependencies.create(libs.findLibrary("detekt-cli").get().get())
        }
        detektConfig.configure { dependencies.addLater(detektDep) }
        val detektClasspath = project.files(detektConfig)

        // Subproject's build.gradle can add dependencies like
        // detektPlugins project(":components:tooling-detekt")
        val detektPlugins = project.configurations.register("detektPlugins") {
            isCanBeConsumed = false
            isCanBeResolved = true
        }
        val detektPluginFiles = project.files(detektPlugins)

        val projectDir = project.projectDir

        fun JavaExec.configureCommon() {
            classpath = detektClasspath
            mainClass.set("io.gitlab.arturbosch.detekt.cli.Main")
            // Resolve the include/exclude globs (with leading "!" meaning exclude)
            // into a FileTree rooted at projectDir, so Gradle can use the actual
            // Kotlin source set to compute UP-TO-DATE / build cache keys.
            val detektSourceTree =
                if (sourcePaths.get().none { !it.startsWith("!") }) {
                    project.files()
                } else {
                    project.fileTree(projectDir).matching {
                        sourcePaths.get().forEach { pattern ->
                            if (pattern.startsWith("!")) {
                                exclude(pattern.removePrefix("!"))
                            } else {
                                include(pattern)
                            }
                        }
                    }
                }
            onlyIf { !detektSourceTree.isEmpty }

            mozilla.detektConfig.orNull?.let {
                val file = project.file(it)
                args("--config", file.absolutePath)
                inputs.file(file).withPropertyName("detektConfig")
            }

            inputs.files(detektSourceTree)
                .withPropertyName("detektSources")
                .withPathSensitivity(org.gradle.api.tasks.PathSensitivity.RELATIVE)
                .skipWhenEmpty()
            inputs.files(detektPluginFiles)
                .withPropertyName("detektPlugins")
                .optional(true)

            val includes = sourcePaths.get().filter { !it.startsWith("!") }
            val excludes = sourcePaths.get().filter { it.startsWith("!") }.map {it.removePrefix("!") }
            args("--input", projectDir.absolutePath)
            if (includes.isNotEmpty()) args("--includes", includes.joinToString(","))
            if (excludes.isNotEmpty()) args("--excludes", excludes.joinToString(","))

            argumentProviders.add(CommandLineArgumentProvider {
                val plugins = detektPluginFiles.files.filter { it.exists() }
                if (plugins.isNotEmpty()) {
                    listOf("--plugins", plugins.joinToString(",") { it.absolutePath })
                } else {
                    emptyList()
                }
            })
        }

        project.tasks.register("detekt", JavaExec::class.java) {
            group = "verification"
            description = "Run detekt static analysis."
            configureCommon()

            if (mozilla.detektAutoCorrect.get()) {
                args("--auto-correct")
                jvmArgs("--add-opens", "java.base/java.lang=ALL-UNNAMED")
            }

            mozilla.detektBaseline.orNull?.let {
                val file = project.file(it)
                if (file.exists()) {
                    args("--baseline", file.absolutePath)
                    inputs.file(file).withPropertyName("detektBaseline").optional(true)
                }
            }
            mozilla.detektReports.get().forEach { (id, path) -> args("--report", "$id:$path") }
            mozilla.detektReports.get().values.forEach { outputs.file(project.file(it)) }
            outputs.cacheIf { true }
        }

        project.tasks.register("detektBaseline", JavaExec::class.java) {
            group = "verification"
            description = "Regenerate the detekt baseline."
            configureCommon()
            val baselineFile = mozilla.detektBaseline.orNull?.let { project.file(it) }
            onlyIf { baselineFile != null }
            args("--create-baseline")
            baselineFile?.let { args("--baseline", it.absolutePath) }
        }
    }

    @Suppress("DEPRECATION") // `sarifReport` and `sarifOutput` are deprecated
    private fun configureAndroidComponentsLint(project: Project, mozilla: ProjectExtension, topsrcdir: String) {
        val action = Action<AppliedPlugin> {
            if (!mozilla.androidComponentsProject.get()) return@Action

            val sarifOutputFile = File(
                topsrcdir,
                "mobile/android/android-components/build/reports/lint/lint-report-${project.name}.sarif.json",
            )
            val android = project.extensions.getByType<CommonExtension>()
            with(android.lint) {
                baseline = project.file("lint-baseline.xml")
                warningsAsErrors = true
                abortOnError = false
                sarifReport = true
                sarifOutput = sarifOutputFile
                disable += listOf(
                    "MissingTranslation",
                    "ExtraTranslation",
                    "MissingDefaultResource",
                    // We do not want to enforce this as a generic rule for all languages (see #6117, #6056, #6118)
                    "TypographyEllipsis",
                    // https://github.com/mozilla-mobile/android-components/issues/10641
                    "UnspecifiedImmutableFlag",
                    // https://bugzilla.mozilla.org/show_bug.cgi?id=1795427
                    "UnusedResources",
                    // "We do not impose rules on locales"
                    // https://github.com/mozilla-mobile/android-components/pull/11069
                    "TypographyDashes",
                )
            }
        }
        project.pluginManager.withPlugin("com.android.library", action)
        project.pluginManager.withPlugin("com.android.application", action)
    }

    private fun configureRoomImplementationCheck(project: Project) {
        project.pluginManager.withPlugin("com.google.devtools.ksp") {
            val schemas = project.file("schemas")
            val kspOutputs = project.layout.buildDirectory.dir("generated/ksp").get().asFile
            val kspTask = Regex("ksp(\\w+)Kotlin")
            project.tasks.configureEach {
                val variant = kspTask.matchEntire(name)?.groupValues?.get(1)?.replaceFirstChar { it.lowercase() }
                if (variant == null || variant.contains("Test")) return@configureEach
                doLast {
                    val missing = missingRoomImplementations(
                        schemas.listFiles().orEmpty().filter { it.isDirectory }.map { it.name },
                        File(kspOutputs, variant).walk().filter { it.isFile }.map { it.name }.toList(),
                    )
                    if (missing.isNotEmpty()) {
                        throw GradleException("$path ran but Room generated no implementation for ${missing.joinToString()}")
                    }
                }
            }
        }
    }

    private fun configureUnitTests(project: Project) {
        val automation = project.providers.environmentVariable("MOZ_AUTOMATION").isPresent
        val fetches = project.providers.environmentVariable("MOZ_FETCHES_DIR")
        project.pluginManager.withPlugin("com.android.base") {
            project.tasks.withType(Test::class.java).configureEach {
                systemProperty("robolectric.logging", "stdout")
                systemProperty("logging.test-mode", "true")
                systemProperty("javax.net.ssl.trustStoreType", "JKS")
                jvmArgs("--enable-native-access=ALL-UNNAMED")
                jvmArgs("--add-exports=java.base/jdk.internal.access=ALL-UNNAMED")
                if (automation) {
                    systemProperty(
                        "robolectric.dependency.dir",
                        File(fetches.get(), "robolectric-android-all").path,
                    )
                }

                testLogging.events = emptySet()

                // Translates JUnit test events into Mozilla's TBPL-like textual format that
                // Taskcluster log parsing expects.
                // See also: testing/mozbase/mozlog/mozlog/formatters/tbplformatter.py
                val listener = MozillaTestOutputListener(logger)
                addTestListener(listener)
                addTestOutputListener(listener)
            }

            val libs = project.extensions.getByType(VersionCatalogsExtension::class.java).named("libs")
            val espresso = libs.findLibrary("androidx-test-espresso-core").get().get()
            val espressoNotation = "${espresso.module}:${espresso.versionConstraint.requiredVersion}"
            project.configurations.matching { it.name == "testImplementation" }.configureEach {
                project.dependencies.constraints.add(name, espressoNotation)
            }
        }
    }

    /**
     * Applies the packaging resources exclusions for Android library and application projects
     */
    private fun configurePackagingResourcesExcludes(project: Project) {
        val action = Action<AppliedPlugin> {
            val android = project.extensions.getByType<CommonExtension>()
            android.packaging.resources.excludes.addAll(listOf("META-INF/LICENSE.md", "META-INF/LICENSE-notice.md"))
        }
        project.pluginManager.withPlugin("com.android.library", action)
        project.pluginManager.withPlugin("com.android.application", action)
    }

    /**
     * Adds the `build-metadata.properties` asset to minified application variants.
     */
    private fun configureBuildMetadata(project: Project) {
        project.pluginManager.withPlugin("com.android.application") {
            val androidComponents = project.extensions.getByType<ApplicationAndroidComponentsExtension>()
            androidComponents.onVariants(androidComponents.selector().all()) { variant ->
                if (!variant.isMinifyEnabled) return@onVariants
                val task = project.tasks.register<WriteBuildMetadata>(
                    "writeBuildMetadata${variant.name.replaceFirstChar { it.uppercase() }}",
                ) {
                    mappingFile.set(variant.artifacts.get(SingleArtifact.OBFUSCATION_MAPPING_FILE))
                }
                variant.sources.assets?.addGeneratedSourceDirectory(task, WriteBuildMetadata::outputDir)
            }
        }
    }

    private fun registerPrintVariantsTask(project: Project) {
        project.pluginManager.withPlugin("com.android.application") {
            val outputFile = project.file("build/printVariants.json")
            val variants = mutableListOf<Map<String, Any?>>()

            // onVariants is a configuration-time callback, so only collect variant metadata when
            // printVariants is actually being run.
            val wantsPrintVariants = project.gradle.startParameter.taskNames.any {
                it.substringAfterLast(':') == "printVariants"
            }
            if (wantsPrintVariants) {
                val buildTypes = project.extensions.getByType<ApplicationExtension>().buildTypes
                val androidComponents = project.extensions.getByType<ApplicationAndroidComponentsExtension>()

                // The APK file name is reconstructed from the flavor/ABI/build type since VariantOutput
                // does not expose the output file.
                androidComponents.onVariants(androidComponents.selector().all()) { variant ->
                    val buildType = variant.buildType
                    val flavorPrefix = if (!variant.flavorName.isNullOrEmpty()) "${variant.flavorName}-" else ""
                    // AGP appends "-unsigned" to APKs whose build type has no signing config.
                    val signed = buildTypes.getByName(requireNotNull(buildType)).signingConfig != null
                    val signedSuffix = if (signed) "" else "-unsigned"

                    val apks = variant.outputs.map { output ->
                        val abi = output.filters
                            .firstOrNull { it.filterType == FilterConfiguration.FilterType.ABI }
                            ?.identifier
                            ?: "universal"
                        mapOf("abi" to abi, "fileName" to "app-$flavorPrefix$abi-$buildType$signedSuffix.apk")
                    }.sortedBy { it["abi"] }
                    variants.add(mapOf("apks" to apks, "build_type" to buildType, "name" to variant.name))
                }
            }

            project.tasks.register("printVariants") {
                outputs.file(outputFile)

                doLast {
                    val variantsList = variants.toMutableList()
                    variantsList.add(
                        mapOf(
                            "apks" to listOf(
                                mapOf(
                                    "abi" to "noarch",
                                    "fileName" to "app-debug-androidTest.apk"
                                )
                            ),
                            "build_type" to "androidTest",
                            "name" to "androidTest"
                        )
                    )
                    outputFile.parentFile.mkdirs()
                    outputFile.writeText(groovy.json.JsonOutput.toJson(variantsList))
                    logger.debug("Wrote variant info to $outputFile")
                }
            }
        }
    }
}

private class MozillaTestOutputListener(
    private val taskLogger: Logger,
) : TestListener, TestOutputListener {
    override fun beforeSuite(suite: TestDescriptor) {
        if (suite.className != null) {
            println("\nSUITE: ${suite.className}")
        }
    }

    override fun afterSuite(suite: TestDescriptor, result: TestResult) {}

    override fun beforeTest(testDescriptor: TestDescriptor) {
        println("  TEST: ${testDescriptor.name}")
    }

    override fun afterTest(testDescriptor: TestDescriptor, result: TestResult) {
        when (result.resultType) {
            TestResult.ResultType.SUCCESS -> println("  SUCCESS")
            TestResult.ResultType.FAILURE -> {
                val testId = "${testDescriptor.className}.${testDescriptor.name}"
                println("  TEST-UNEXPECTED-FAIL | $testId | ${result.exception}")
            }
            TestResult.ResultType.SKIPPED -> println("  SKIPPED")
        }
        taskLogger.lifecycle("")
    }

    override fun onOutput(testDescriptor: TestDescriptor, outputEvent: TestOutputEvent) {
        taskLogger.lifecycle("    ${outputEvent.message.trim()}")
    }
}

// Unpacks the host platform native libs (the `<jna platform>/` entries) out of a
// libsForTests JAR into a flat directory, so JVM unit tests can load them.
abstract class ExtractNativeLibsForTests : TransformAction<ExtractNativeLibsForTests.Parameters> {
    interface Parameters : TransformParameters {
        @get:Input
        val jnaPlatform: Property<String>
    }

    @get:InputArtifact
    abstract val inputArtifact: Provider<FileSystemLocation>

    override fun transform(outputs: TransformOutputs) {
        val jar = inputArtifact.get().asFile
        val platform = parameters.jnaPlatform.get()
        val outputDir = outputs.dir("native-libs")
        ZipFile(jar).use { zip ->
            zip.entries().asSequence()
                .filter { it.name.startsWith("$platform/") && !it.isDirectory }
                .forEach { entry ->
                    val outFile = File(outputDir, entry.name.substringAfterLast("/"))
                    zip.getInputStream(entry).use { input ->
                        outFile.outputStream().use { output ->
                            input.copyTo(output)
                        }
                    }
                }
        }
    }
}

/**
 * Returns the Room databases, named by their exported schema directories (fully qualified class names), that have
 * no `_Impl` class among [generatedFiles], the file names KSP generated for one variant.
 */
internal fun missingRoomImplementations(databases: List<String>, generatedFiles: List<String>): List<String> {
    val generated = generatedFiles.map { it.substringBeforeLast('.') }
    return databases.filterNot { database ->
        val impl = "${database.substringAfterLast('.')}_Impl"
        generated.any { it == impl || it.endsWith("_$impl") }
    }
}

internal fun checkClassFileVersions(dir: File, javaMajor: Int) {
    // Class file major version 61 is Java 17: https://docs.oracle.com/javase/specs/jvms/se17/html/jvms-4.html#jvms-4.1
    val max = javaMajor + 44
    val tooNew = dir.walk().filter { it.isFile && it.extension == "class" }.mapNotNull { file ->
        val version = DataInputStream(file.inputStream()).use {
            try {
                if (it.readInt() == 0xCAFEBABE.toInt()) { it.readUnsignedShort(); it.readUnsignedShort() } else null
            } catch (e: EOFException) {
                null
            }
        } ?: throw GradleException("$file is not a class file")
        if (version > max) "$file: class file version $version" else null
    }.toList()
    if (tooNew.isNotEmpty()) {
        throw GradleException("Class files newer than version $max (Java $javaMajor):\n" + tooNew.joinToString("\n"))
    }
}
