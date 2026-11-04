/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.icons.pipeline

import mozilla.components.browser.icons.IconRequest

/**
 * This [Comparator] implementations compares [IconRequest.Resource] objects to determine which icon to try to load
 * first.
 */
internal object IconResourceComparator : Comparator<IconRequest.Resource> {

    /**
     * Compare two icon resources. If [resource] is more important, a negative number is returned. If [other] is more
     * important, a positive number is returned. If the two resources are of equal importance, 0 is returned. Importance
     * represents which icon we should try to load first.
     */
    override fun compare(resource: IconRequest.Resource, other: IconRequest.Resource) =
        when {
            resource.maskable != other.maskable -> -resource.maskable.compareTo(other.maskable)
            resource.type != other.type -> -resource.type.rank().compareTo(other.type.rank())
            resource.maxSize != other.maxSize -> -resource.maxSize.compareTo(other.maxSize)
            else -> {
                // If there's no other way to choose, we prefer container types.
                // They *might* contain an image larger than the size given in the <link> tag.
                val isResourceContainerType = resource.isContainerType
                if (isResourceContainerType != other.isContainerType) {
                    if (isResourceContainerType) -1 else 1
                } else {
                    // There's no way to know which icon might be better. However we need to pick a consistent one
                    // to avoid breaking set implementations (See Fennec Bug 1331808).
                    // Therefore we are  picking one by just comparing the URLs.
                    resource.url.compareTo(other.url)
                }
            }
        }
}

@Suppress("MagicNumber")
private fun IconRequest.Resource.Type.rank(): Int {
    return when (this) {
        // An icon from our Merino Manifest should always be preferred.
        IconRequest.Resource.Type.MERINO_MANIFEST -> 25

        IconRequest.Resource.Type.MANIFEST_ICON -> 20
        // We prefer touch icons because they tend to have a higher resolution than ordinary favicons.
        IconRequest.Resource.Type.APPLE_TOUCH_ICON -> 15
        IconRequest.Resource.Type.FAVICON -> 10

        // Fallback icon types:
        IconRequest.Resource.Type.IMAGE_SRC -> 6
        IconRequest.Resource.Type.FLUID_ICON -> 5
        IconRequest.Resource.Type.OPENGRAPH -> 4
        IconRequest.Resource.Type.TWITTER -> 3
        IconRequest.Resource.Type.MICROSOFT_TILE -> 2
    }
}

private val IconRequest.Resource.maxSize: Int
    get() = sizes.asSequence().map { size -> size.minLength }.maxOrNull() ?: 0

private val IconRequest.Resource.isContainerType: Boolean
    get() = mimeType != null && containerTypes.contains(mimeType)

private val containerTypes =
    listOf(
        "image/vnd.microsoft.icon",
        "image/ico",
        "image/icon",
        "image/x-icon",
        "text/ico",
        "application/ico",
    )
