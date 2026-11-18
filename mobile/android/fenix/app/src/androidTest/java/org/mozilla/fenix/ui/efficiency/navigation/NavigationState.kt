/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.navigation

import org.mozilla.fenix.ui.efficiency.helpers.PageReadinessProfile

data class NavigationFact(val name: String) {
    init {
        require(namePattern.matches(name)) {
            "Navigation fact must match ${namePattern.pattern}: '$name'"
        }
    }

    override fun toString(): String = name

    private companion object {
        val namePattern = Regex("[A-Z][A-Z0-9_]*")
    }
}

object NavigationFacts {
    val BOOKMARKS_HAVE_ITEMS = NavigationFact("BOOKMARKS_HAVE_ITEMS")
    val RETURN_SURFACE_BROWSER = NavigationFact("RETURN_SURFACE_BROWSER")
    val RETURN_SURFACE_HOME = NavigationFact("RETURN_SURFACE_HOME")

    // Tabs settings opened from the tray goes back to the tray, not Settings; gates the back edge.
    val TAB_SETTINGS_FROM_TRAY = NavigationFact("TAB_SETTINGS_FROM_TRAY")

    internal fun normalize(page: String, facts: Set<NavigationFact>): Set<NavigationFact> =
        when (page) {
            "BrowserPage" -> facts - RETURN_SURFACE_HOME + RETURN_SURFACE_BROWSER
            "HomePage" -> facts - RETURN_SURFACE_BROWSER + RETURN_SURFACE_HOME
            else -> facts
        }
}

data class NavigationRouteTrait(val name: String) {
    init {
        require(namePattern.matches(name)) {
            "Navigation route trait must match ${namePattern.pattern}: '$name'"
        }
    }

    override fun toString(): String = name

    private companion object {
        val namePattern = Regex("[A-Z][A-Z0-9_]*")
    }
}

data class NavigationState(
    val node: NavigationNodeId,
    val facts: Set<NavigationFact> = emptySet(),
) {
    val page: String
        get() = node.value

    fun normalized(): NavigationState = copy(facts = NavigationFacts.normalize(page, facts))
}

/** Hard constraints, ordered waypoints, and soft preferences for one navigation request. */
data class NavigationOptions(
    val via: List<String> = emptyList(),
    val excludedPages: Set<String> = emptySet(),
    val excludedRoutes: Set<String> = emptySet(),
    val requiredRoutes: Set<String> = emptySet(),
    val requiredFacts: Set<NavigationFact> = emptySet(),
    val avoidPages: Set<String> = setOf("BrowserPage"),
    val avoidTraits: Set<NavigationRouteTrait> = emptySet(),
    val readinessProfiles: Map<String, PageReadinessProfile> = emptyMap(),
) {
    init {
        require(via.none { it.isBlank() }) { "Navigation waypoints cannot be blank" }
        require(excludedPages.none { it.isBlank() }) { "Excluded navigation pages cannot be blank" }
        require(excludedRoutes.none { it.isBlank() }) { "Excluded navigation routes cannot be blank" }
        require(requiredRoutes.none { it.isBlank() }) { "Required navigation routes cannot be blank" }
        require((excludedRoutes intersect requiredRoutes).isEmpty()) {
            "A navigation route cannot be both required and excluded"
        }
        require((via.toSet() intersect excludedPages).isEmpty()) {
            "A navigation waypoint cannot also be excluded"
        }
        require(readinessProfiles.keys.none { it.isBlank() }) { "Readiness override pages cannot be blank" }
    }

    internal fun validateDestination(destination: String) {
        require(destination !in excludedPages) {
            "Navigation destination '$destination' cannot also be excluded"
        }
    }

    internal fun advanceWaypoint(index: Int, page: String): Int = if (via.getOrNull(index) == page) index + 1 else index

    internal fun readinessProfileFor(page: String, default: PageReadinessProfile): PageReadinessProfile =
        readinessProfiles[page] ?: default

    internal fun goalSatisfied(
        state: NavigationState,
        destination: String,
        waypointIndex: Int,
        traversedRequiredRoutes: Set<String>,
    ): Boolean =
        state.page == destination &&
            waypointIndex == via.size &&
            requiredRoutes.all { it in traversedRequiredRoutes } &&
            requiredFacts.all { it in state.facts }
}

data class NavigationCheckpoint(
    val state: NavigationState,
    val profile: PageReadinessProfile,
    val incomingEdge: NavigationEdge? = null,
    val outgoingEdge: NavigationEdge? = null,
    val isWaypoint: Boolean = false,
    val isDestination: Boolean = false,
)
