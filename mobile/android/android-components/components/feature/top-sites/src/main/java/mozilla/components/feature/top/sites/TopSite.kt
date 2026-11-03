/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.top.sites

/** A top site. */
sealed class TopSite {
    abstract val id: Long?
    abstract val title: String?
    abstract val url: String
    abstract val createdAt: Long?
    abstract val type: String

    /**
     * This top site was added as a default by the application.
     *
     * @property id Unique ID of this top site. This can be `null` when it is not supplied.
     * @property title The title of the top site.
     * @property url The URL of the top site.
     * @property createdAt The optional date the top site was added.
     * @property type The type name of the top site.
     */
    data class Default(
        override val id: Long?,
        override val title: String?,
        override val url: String,
        override val createdAt: Long?,
        override val type: String = "DEFAULT",
    ) : TopSite()

    /**
     * This top site was added as a default by the application and is pinned.
     *
     * @property id Unique ID of this top site.
     * @property title The title of the top site.
     * @property url The URL of the top site.
     * @property createdAt The optional date the top site was added.
     * @property type The type name of the top site.
     */
    data class DefaultPinned(
        override val id: Long?,
        override val title: String?,
        override val url: String,
        override val createdAt: Long?,
        override val type: String = "DEFAULT_PINNED",
    ) : TopSite()

    /**
     * This top site was pinned by an user.
     *
     * @property id Unique ID of this top site.
     * @property title The title of the top site.
     * @property url The URL of the top site.
     * @property createdAt The optional date the top site was added.
     * @property type The type name of the top site.
     */
    data class Pinned(
        override val id: Long?,
        override val title: String?,
        override val url: String,
        override val createdAt: Long?,
        override val type: String = "PINNED",
    ) : TopSite()

    /**
     * This top site is auto-generated from the history storage based on the most frecent site.
     *
     * @property id Unique ID of this top site.
     * @property title The title of the top site.
     * @property url The URL of the top site.
     * @property createdAt The optional date the top site was added.
     * @property type The type name of the top site.
     */
    data class Frecent(
        override val id: Long?,
        override val title: String?,
        override val url: String,
        override val createdAt: Long?,
        override val type: String = "FRECENT",
    ) : TopSite()

    /**
     * This top site is provided by the [TopSitesProvider].
     *
     * @property id Unique ID of this top site.
     * @property title The title of the top site.
     * @property url The URL of the top site.
     * @property clickUrl The click URL of the top site.
     * @property imageUrl The image URL of the top site.
     * @property impressionUrl The URL that needs to be fired when the top site is displayed.
     * @property createdAt The optional date the top site was added.
     * @property type The type name of the top site.
     */
    data class Provided(
        override val id: Long?,
        override val title: String?,
        override val url: String,
        val clickUrl: String,
        val imageUrl: String,
        val impressionUrl: String,
        override val createdAt: Long?,
        override val type: String = "PROVIDED",
    ) : TopSite()
}
