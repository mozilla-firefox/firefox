/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.engine

/** Represents all the different supported types of data that can be found from long clicking an element. */
@Suppress("ClassNaming", "ClassName")
sealed class HitResult(open val src: String) {

    /** Get the URL of the hit result depending on the type. */
    fun getUrl(): String =
        when (this) {
            is UNKNOWN -> src
            is IMAGE_SRC -> uri
            is IMAGE -> src
            is VIDEO -> src
            is AUDIO -> src
            else -> "about:blank"
        }

    /**
     * Default type if we're unable to match the type to anything. It may or may not have a src.
     *
     * @param src The src of the element.
     * @param linkText The (optional) link text of the element.
     */
    data class UNKNOWN(override val src: String, val linkText: String? = null) : HitResult(src)

    /** If the HTML element was of type 'HTMLImageElement'. */
    data class IMAGE(override val src: String, val title: String? = null) : HitResult(src)

    /** If the HTML element was of type 'HTMLVideoElement'. */
    data class VIDEO(override val src: String, val title: String? = null) : HitResult(src)

    /** If the HTML element was of type 'HTMLAudioElement'. */
    data class AUDIO(override val src: String, val title: String? = null) : HitResult(src)

    /** If the HTML element was of type 'HTMLImageElement' and contained a URI. */
    data class IMAGE_SRC(override val src: String, val uri: String, val title: String? = null) : HitResult(src)

    /** The type used if the URI is prepended with 'tel:'. */
    data class PHONE(override val src: String) : HitResult(src)

    /** The type used if the URI is prepended with 'mailto:'. */
    data class EMAIL(override val src: String) : HitResult(src)

    /** The type used if the URI is prepended with 'geo:'. */
    data class GEO(override val src: String) : HitResult(src)
}
