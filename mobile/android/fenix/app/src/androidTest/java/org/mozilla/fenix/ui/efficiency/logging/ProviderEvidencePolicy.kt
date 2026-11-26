/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.logging

data class RedactedProviderEvent(
    val fields: Map<String, Any?>,
    val removedFields: Set<String>,
)

object ProviderEvidencePolicy {
    const val VERSION = 3

    private val envelopeFields =
        setOf(
            "eventSchemaVersion",
            "runId",
            "shardId",
            "testId",
            "attemptId",
            "dispatchAttemptId",
            "providerAttemptOrdinal",
            "sequence",
            "wallTimeMs",
            "monotonicTimeNs",
            "eventType",
            "executionPath",
            "isolation",
            "identitySource",
            "processId",
        )
    private val commonFields = setOf("type", "testId", "ts", "eventEnvelope")
    private val stateFields =
        setOf(
            "phase",
            "tabs",
            "tabsPrivate",
            "pendingUndoTabs",
            "recentlyClosedStoreTabs",
            "history",
            "bookmarks",
            "logins",
            "addresses",
            "creditCards",
            "sitePermissions",
            "savedSessions",
            "collections",
            "tabGroups",
            "tabGroupAssignments",
            "recentlyClosedTabs",
            "topSites",
            "downloads",
            "preferenceOverrideCount",
            "preferenceOverrideIds",
            "processId",
            "searchActive",
            "voiceInputRequested",
            "voiceInputResult",
            "launcherIcon",
        )
    private val eventFields =
        mapOf(
            "testStart" to setOf("meta"),
            "testEnd" to setOf("status"),
            "attemptEnd" to
                setOf(
                    "terminalSchemaVersion",
                    "outcome",
                    "bodyOutcome",
                    "cleanupOutcome",
                    "complete",
                    "failureOrigins",
                    "failedCleanupStages",
                    "terminalThrowableType",
                ),
            "stepEnd" to
                setOf(
                    "scopeType",
                    "outcome",
                    "elapsedMs",
                    "stepId",
                    "verb",
                    "selector",
                    "selectorId",
                    "strategy",
                    "backend",
                    "tree",
                    "semanticRoles",
                    "failure",
                    "failureCategory",
                    "failurePhase",
                    "page",
                    "screen",
                    "attempts",
                    "actionAttempts",
                    "waitPolicy",
                    "timeoutMs",
                    "timedOut",
                    "lastObservation",
                    "resolution",
                    "retryable",
                    "navigationEdge",
                    "navigationStepIndex",
                    "navigationFacts",
                    "incomingRoute",
                    "outgoingRoute",
                    "readinessProfile",
                    "isWaypoint",
                    "isDestination",
                    "appliedRules",
                    "skippedRules",
                    "missingSelectors",
                ),
            "dump" to setOf("layer"),
            "dumpNode" to setOf("layer"),
            "state" to stateFields,
            "stateSnapshot" to
                setOf(
                    "schemaVersion",
                    "snapshotId",
                    "chunkIndex",
                    "chunkCount",
                    "phase",
                    "contributors",
                ),
            "cleanup" to setOf("phase", "failed"),
            "runtimeCleanup" to
                setOf(
                    "phase",
                    "searchActiveBefore",
                    "searchActiveAfter",
                    "voiceInputRequestedBefore",
                    "voiceInputRequestedAfter",
                    "voiceInputResultBefore",
                    "voiceInputResultAfter",
                ),
            "inputCleanup" to setOf("phase", "visibleBefore", "dismissals", "visibleAfter"),
            "activityCleanup" to
                setOf(
                    "phase",
                    "taskId",
                    "taskRemovalRequested",
                    "activityDestroyed",
                    "taskPresentAfter",
                    "verified",
                    "elapsedMs",
                ),
            "environmentPreflight" to setOf("requirements", "before", "after"),
            "environmentRestore" to setOf("requirements", "before", "after"),
            "activityEnvironment" to setOf("phase", "requiredOrientation", "beforeOrientation", "afterOrientation"),
            "isolation" to setOf("phase", "verified", "violations"),
            "activityLifecycle" to
                setOf(
                    "sequence",
                    "event",
                    "elapsedRealtimeMs",
                    "processId",
                    "activity",
                    "activityId",
                    "taskId",
                    "isTaskRoot",
                    "intentFlags",
                    "newTaskIntent",
                    "clearTaskIntent",
                    "launchedFromHistory",
                    "hasSavedInstanceState",
                    "savedStateKeyCount",
                    "isFinishing",
                    "isChangingConfigurations",
                    "isDestroyed",
                    "searchActive",
                    "voiceInputRequested",
                ),
        )
    private val launchConfigFields =
        setOf(
            "skipOnboarding",
            "isPageLoadTranslationsPromptEnabled",
            "isPocketEnabled",
            "isRecentlyVisitedFeatureEnabled",
            "shouldUseExpandedToolbar",
            "isTabStripEnabled",
        )
    private val environmentRequirementFields =
        setOf(
            "requiredOrientation",
            "requiredBackGestureNavigation",
            "requiredDataSaver",
            "requiredSystemUiClipboardAccess",
            "requiredPostNotificationPermission",
            "clearNotifications",
            "clearSharedDownloads",
            "mockWebServer",
        )
    private val launchFields = launchConfigFields + environmentRequirementFields
    private val environmentFields =
        setOf(
            "orientation",
            "backGestureLeft",
            "backGestureRight",
            "dataSaver",
            "systemUiClipboardAccess",
            "postNotificationPermission",
            "foregroundWindow",
        )
    private val contributorFields =
        mapOf(
            "browserStore" to setOf("tabs", "tabsPrivate", "downloads", "pendingUndoTabs", "recentlyClosedStoreTabs"),
            "places" to setOf("history", "bookmarks", "topSites"),
            "savedUserData" to setOf("logins", "addresses", "creditCards"),
            "isolationStorage" to setOf("sitePermissions", "savedSessions"),
            "tabOrganization" to setOf("collections", "tabGroups", "tabGroupAssignments", "recentlyClosedTabs"),
            "appRuntime" to setOf("searchActive", "voiceInputRequested", "voiceInputResult"),
            "preferences" to setOf("preferenceOverrideCount", "preferenceOverrideIds"),
            "searchConfiguration" to
                setOf(
                    "complete",
                    "userSelectedEngineOverride",
                    "userSelectedPrivateEngineOverride",
                    "defaultMatchesRegion",
                    "privateMatchesDefault",
                    "customEngineCount",
                    "hiddenEngineCount",
                    "disabledShortcutCount",
                    "disabledShortcutsAtDefault",
                    "additionalEngineCount",
                    "persistedMetadataOverrideCount",
                    "persistedCustomEngineCount",
                ),
            "launcher" to setOf("launcherIcon"),
            "executionIdentity" to setOf("processId"),
        )

    fun redact(event: Map<String, Any?>): RedactedProviderEvent {
        val removed = linkedSetOf<String>()
        val type = event["type"]?.toString().orEmpty()
        val allowed = commonFields + eventFields.getOrElse(type) { emptySet() }
        val fields =
            event
                .filterKeys { key ->
                    (key in allowed).also { keep -> if (!keep) removed += key }
                }
                .mapValues { (key, value) ->
                    when (key) {
                        "eventEnvelope" -> filterMap(value, envelopeFields, "eventEnvelope", removed)
                        "meta" -> filterMap(value, launchFields, "meta", removed)
                        "requirements" -> filterMap(value, environmentRequirementFields, "requirements", removed)
                        "before",
                        "after" -> filterMap(value, environmentFields, key, removed)
                        "contributors" -> redactContributors(value, removed)
                        else -> value
                    }
                }
                .toMutableMap()
        fields["providerEvidence"] =
            mapOf(
                "redactionPolicyVersion" to VERSION,
                "removedFieldCount" to removed.size,
                "removedFields" to removed.sorted(),
            )
        return RedactedProviderEvent(fields, removed)
    }

    private fun filterMap(
        value: Any?,
        allowed: Set<String>,
        path: String,
        removed: MutableSet<String>,
    ): Map<String, Any?> {
        val source = value as? Map<*, *> ?: return emptyMap()
        return source.entries
            .mapNotNull { (key, item) ->
                val name = key as? String ?: return@mapNotNull null
                if (name !in allowed) {
                    removed += "$path.$name"
                    null
                } else {
                    name to item
                }
            }
            .toMap()
    }

    private fun redactContributors(value: Any?, removed: MutableSet<String>): List<Map<String, Any?>> {
        val contributors = value as? List<*> ?: return emptyList()
        return contributors.mapNotNull { item ->
            val source = item as? Map<*, *> ?: return@mapNotNull null
            val name = source["name"] as? String ?: return@mapNotNull null
            val allowedValues = contributorFields[name]
            if (allowedValues == null) {
                removed += "contributors.$name"
                return@mapNotNull null
            }
            val allowedMetadata =
                setOf(
                    "name",
                    "schemaVersion",
                    "captureCost",
                    "sensitivity",
                    "includeInCompatibilityState",
                    "boundaryPolicy",
                    "resourcePolicy",
                    "boundaryBaseline",
                    "controlDrivers",
                    "complete",
                    "values",
                )
            val result = filterMap(source, allowedMetadata, "contributors.$name", removed).toMutableMap()
            result["boundaryBaseline"] =
                filterMap(source["boundaryBaseline"], allowedValues, "contributors.$name.boundaryBaseline", removed)
            result["values"] = filterMap(source["values"], allowedValues, "contributors.$name.values", removed)
            result
        }
    }
}
