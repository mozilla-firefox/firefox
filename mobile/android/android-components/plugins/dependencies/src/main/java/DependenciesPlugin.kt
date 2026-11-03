/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import groovy.lang.GroovyObject
import org.gradle.api.Plugin
import org.gradle.api.flow.FlowAction
import org.gradle.api.flow.FlowParameters
import org.gradle.api.flow.FlowProviders
import org.gradle.api.flow.FlowScope
import org.gradle.api.initialization.Settings
import org.gradle.api.provider.Property
import org.gradle.api.tasks.Input
import org.gradle.kotlin.dsl.always
import java.util.Optional
import javax.inject.Inject

// If you ever need to force a toolchain rebuild (taskcluster) then edit the following comment.
// FORCE REBUILD 2024-05-02

/**
 * Print Gradle errors in such a way that Treeherder includes them in its "Failure Summary".  This
 * approach is technically complicated but is compatible with the Gradle configuration cache.
 *
 * The unusual output is recognized by Treeherder even when Gradle is directly invoked without
 * `mach` or `mozharness` logging.
 */
abstract class LogGradleErrorForTreeHerder : FlowAction<LogGradleErrorForTreeHerder.Parameters> {
    interface Parameters : FlowParameters {
        @get:Input
        val failure: Property<Optional<Throwable>>
    }

    companion object {
        private fun appendIndentedMessage(
            sb: StringBuilder,
            t: Throwable,
            indentStr: String,
        ) {
            val message: String = (t.message ?: "").replace("(?m)^".toRegex(), indentStr)
            if (message.isNotEmpty()) {
                // We don't want the first line indented.
                sb.append(message.substring(indentStr.length))
            }
            sb.append("\n")
            if (t.cause != null && t.cause != t) {
                sb.append(indentStr)
                sb.append("> ")
                appendIndentedMessage(sb, t.cause!!, indentStr + "  ")
            }
        }

        fun getIndentedMessage(t: Throwable): String {
            val sb = StringBuilder()
            appendIndentedMessage(sb, t, "")
            return sb.toString()
        }
    }

    /**
     * Print non-null `Throwable` with each line prefixed by "[gradle:error]: >".  Each `cause` is
     * printed indented, roughly matching Gradle's output.
     *
     * Treeherder recognizes such lines as errors and surfaces them in its "Failure Summary"; see
     * https://github.com/mozilla/treeherder/blob/b04b64185e189a2d9e4c088b4be98d898c658e00/treeherder/log_parser/parsers.py#L75.
     * Treeherder trims leading spaces; the extra ">" preserves indentation.
     */
    override fun execute(parameters: Parameters) {
        parameters.failure.get().map { t ->
            getIndentedMessage(t).split("\n").forEach {
                println("[gradle:error]: > $it")
            }
        }
    }
}

abstract class DependenciesPlugin : Plugin<Settings> {
    @get:Inject
    protected abstract val flowScope: FlowScope

    @get:Inject
    protected abstract val flowProviders: FlowProviders

    override fun apply(settings: Settings) {
        val mozconfig = settings.gradle.extensions.extraProperties["mozconfig"] as Map<*, *>
        val substs = mozconfig["substs"] as Map<*, *>
        val appservicesInTree = (substs["MOZ_APPSERVICES_IN_TREE"] as? String ?: "0") == "1"
        val onTry = settings.providers.environmentVariable("MOZ_SOURCE_REPO")
            .orNull == "https://hg.mozilla.org/try"

        // `mobile/android/shared-settings.gradle` sets `config` in `projectsLoaded`, which is
        // after this plugin is applied, so resolve the version lazily.
        @Suppress("GradleProjectIsolation")
        val componentsVersion = lazy {
            val config = settings.gradle.rootProject.extensions.extraProperties["config"] as GroovyObject
            config.getProperty("componentsVersion") as String
        }

        ComponentsDependencies.initialize(appservicesInTree, onTry, componentsVersion)

        flowScope.always(LogGradleErrorForTreeHerder::class) {
            parameters.failure.set(flowProviders.buildWorkResult.map { result -> result.failure })
        }
    }
}

// Synchronized dependencies used by (some) modules
@Suppress("Unused", "MaxLineLength")
object ComponentsDependencies {
    private var appservicesInTree: Boolean? = null
    private var onTry: Boolean = false
    private var componentsVersion: Lazy<String>? = null

    internal fun initialize(inTree: Boolean, onTry: Boolean, componentsVersion: Lazy<String>) {
        appservicesInTree = inTree
        this.onTry = onTry
        this.componentsVersion = componentsVersion
    }

    internal fun getGroupId(): String {
        if (appservicesInTree ?: false) {
            return "org.mozilla.appservices"
        }
        return ApplicationServicesConfig.groupId
    }

    internal fun getVersionNumber(): String {
        if (appservicesInTree ?: false) {
            // On try, relax version pin to allow for --use-existing-task.
            if (onTry) {
                return "+"
            }
            // In-tree app-services publishes with the android-components version, like `159.0a1`;
            // see `configurePublish` in `third_party/application-services/publish.gradle`.
            return componentsVersion!!.value
        }
        return ApplicationServicesConfig.version
    }

    @JvmStatic
    val mozilla_appservices_ads_client get() = "${getGroupId()}:ads-client:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_fxaclient get() = "${getGroupId()}:fxaclient:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_nimbus get() = "${getGroupId()}:nimbus:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_autofill get() = "${getGroupId()}:autofill:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_db_crypto get() = "${getGroupId()}:db-crypto:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_logins get() = "${getGroupId()}:logins:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_merino get() = "${getGroupId()}:merino:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_places get() = "${getGroupId()}:places:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_syncmanager get() = "${getGroupId()}:syncmanager:${getVersionNumber()}"
    @JvmStatic
    val mozilla_remote_settings get() = "${getGroupId()}:remotesettings:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_push get() = "${getGroupId()}:push:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_search get() = "${getGroupId()}:search:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_tabs get() = "${getGroupId()}:tabs:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_suggest get() = "${getGroupId()}:suggest:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_viaduct get() = "${getGroupId()}:viaduct:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_init_rust_components get() = "${getGroupId()}:init_rust_components:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_full_megazord get() = "${getGroupId()}:full-megazord:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_full_megazord_libsForTests get() = "${getGroupId()}:full-megazord-libsForTests:${getVersionNumber()}"

    @JvmStatic
    val mozilla_appservices_errorsupport get() = "${getGroupId()}:errorsupport:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_rust_log_forwarder get() = "${getGroupId()}:rust-log-forwarder:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_sync15 get() = "${getGroupId()}:sync15:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_tracing get() = "${getGroupId()}:tracing:${getVersionNumber()}"
    @JvmStatic
    val mozilla_appservices_fxrelay get() = "${getGroupId()}:relay:${getVersionNumber()}"
}
