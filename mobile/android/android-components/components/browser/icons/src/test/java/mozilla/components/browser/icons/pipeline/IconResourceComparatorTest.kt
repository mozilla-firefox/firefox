/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.icons.pipeline

import kotlin.random.Random
import mozilla.components.browser.icons.IconRequest
import mozilla.components.concept.engine.manifest.Size
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class IconResourceComparatorTest {
    @Test
    fun `compare mozilla-org icons`() {
        val resources =
            listOf(
                IconRequest.Resource(
                    url = "https://www.mozilla.org/media/img/favicon/favicon-196x196.c80e6abe0767.png",
                    type = IconRequest.Resource.Type.FAVICON,
                    sizes = listOf(Size(196, 196)),
                ),
                IconRequest.Resource(
                    url = "https://www.mozilla.org/media/img/favicon.d4f1f46b91f4.ico",
                    type = IconRequest.Resource.Type.FAVICON,
                ),
                IconRequest.Resource(
                    url = "https://www.mozilla.org/media/img/favicon/apple-touch-icon-180x180.8772ec154918.png",
                    type = IconRequest.Resource.Type.APPLE_TOUCH_ICON,
                    sizes = listOf(Size(180, 180)),
                ),
                IconRequest.Resource(
                    url = "https://www.mozilla.org/media/img/mozorg/mozilla-256.4720741d4108.jpg",
                    type = IconRequest.Resource.Type.OPENGRAPH,
                ),
            )

        val urls = resources.sortedWith(IconResourceComparator).map { it.url }

        assertEquals(
            listOf(
                "https://www.mozilla.org/media/img/favicon/apple-touch-icon-180x180.8772ec154918.png",
                "https://www.mozilla.org/media/img/favicon/favicon-196x196.c80e6abe0767.png",
                "https://www.mozilla.org/media/img/favicon.d4f1f46b91f4.ico",
                "https://www.mozilla.org/media/img/mozorg/mozilla-256.4720741d4108.jpg",
            ),
            urls,
        )
    }

    @Test
    fun `compare m-youtube-com icons`() {
        val resources =
            listOf(
                IconRequest.Resource(
                    url = "https://s.ytimg.com/yts/img/favicon-vfl8qSV2F.ico",
                    type = IconRequest.Resource.Type.FAVICON,
                    mimeType = "image/x-icon",
                ),
                IconRequest.Resource(
                    url = "https://s.ytimg.com/yts/img/favicon-vfl8qSV2F.ico",
                    type = IconRequest.Resource.Type.FAVICON,
                    mimeType = "image/x-icon",
                ),
            )

        val urls = resources.sortedWith(IconResourceComparator).map { it.url }

        assertEquals(
            listOf(
                "https://s.ytimg.com/yts/img/favicon-vfl8qSV2F.ico",
                "https://s.ytimg.com/yts/img/favicon-vfl8qSV2F.ico",
            ),
            urls,
        )
    }

    @Test
    fun `compare m-facebook-com icons`() {
        val resources =
            listOf(
                IconRequest.Resource(
                    url = "https://static.xx.fbcdn.net/rsrc.php/v3/ya/r/O2aKM2iSbOw.png",
                    type = IconRequest.Resource.Type.FAVICON,
                    sizes = listOf(Size(196, 196)),
                )
            )

        val urls = resources.sortedWith(IconResourceComparator).map { it.url }

        assertEquals(
            listOf("https://static.xx.fbcdn.net/rsrc.php/v3/ya/r/O2aKM2iSbOw.png"),
            urls,
        )
    }

    @Test
    fun `compare baidu-com icons`() {
        val resources =
            listOf(
                IconRequest.Resource(
                    url = "http://sm.bdimg.com/static/wiseindex/img/favicon64.ico",
                    type = IconRequest.Resource.Type.FAVICON,
                ),
                IconRequest.Resource(
                    url = "http://sm.bdimg.com/static/wiseindex/img/screen_icon_new.png",
                    type = IconRequest.Resource.Type.APPLE_TOUCH_ICON,
                ),
            )

        val urls = resources.sortedWith(IconResourceComparator).map { it.url }

        assertEquals(
            listOf(
                "http://sm.bdimg.com/static/wiseindex/img/screen_icon_new.png",
                "http://sm.bdimg.com/static/wiseindex/img/favicon64.ico",
            ),
            urls,
        )
    }

    @Test
    fun `compare wikipedia-org icons`() {
        val resources =
            listOf(
                IconRequest.Resource(
                    url = "https://www.wikipedia.org/static/favicon/wikipedia.ico",
                    type = IconRequest.Resource.Type.FAVICON,
                ),
                IconRequest.Resource(
                    url = "https://www.wikipedia.org/static/apple-touch/wikipedia.png",
                    type = IconRequest.Resource.Type.APPLE_TOUCH_ICON,
                ),
            )

        val urls = resources.sortedWith(IconResourceComparator).map { it.url }

        assertEquals(
            listOf(
                "https://www.wikipedia.org/static/apple-touch/wikipedia.png",
                "https://www.wikipedia.org/static/favicon/wikipedia.ico",
            ),
            urls,
        )
    }

    @Test
    fun `compare amazon-com icons`() {
        val resources =
            listOf(
                IconRequest.Resource(
                    url =
                        "https://images-na.ssl-images-amazon.com/images/G/01/anywhere/a_smile_57x57._CB368212015_.png",
                    type = IconRequest.Resource.Type.APPLE_TOUCH_ICON,
                    sizes = listOf(Size(57, 57)),
                ),
                IconRequest.Resource(
                    url =
                        "https://images-na.ssl-images-amazon.com/images/G/01/anywhere/a_smile_72x72._CB368212002_.png",
                    type = IconRequest.Resource.Type.APPLE_TOUCH_ICON,
                    sizes = listOf(Size(72, 72)),
                ),
                IconRequest.Resource(
                    url =
                        "https://images-na.ssl-images-amazon.com/images/G/01/anywhere/a_smile_114x114._CB368212020_.png",
                    type = IconRequest.Resource.Type.APPLE_TOUCH_ICON,
                    sizes = listOf(Size(114, 114)),
                ),
                IconRequest.Resource(
                    url =
                        "https://images-na.ssl-images-amazon.com/images/G/01/anywhere/a_smile_120x120._CB368246573_.png",
                    type = IconRequest.Resource.Type.APPLE_TOUCH_ICON,
                    sizes = listOf(Size(120, 120)),
                ),
                IconRequest.Resource(
                    url =
                        "https://images-na.ssl-images-amazon.com/images/G/01/anywhere/a_smile_144x144._CB368211973_.png",
                    type = IconRequest.Resource.Type.APPLE_TOUCH_ICON,
                    sizes = listOf(Size(144, 144)),
                ),
                IconRequest.Resource(
                    url =
                        "https://images-na.ssl-images-amazon.com/images/G/01/anywhere/a_smile_152x152._CB368246573_.png",
                    type = IconRequest.Resource.Type.APPLE_TOUCH_ICON,
                    sizes = listOf(Size(152, 152)),
                ),
                IconRequest.Resource(
                    url =
                        "https://images-na.ssl-images-amazon.com/images/G/01/anywhere/a_smile_196x196._CB368246573_.png",
                    type = IconRequest.Resource.Type.APPLE_TOUCH_ICON,
                    sizes = listOf(Size(196, 196)),
                ),
            )

        val urls = resources.sortedWith(IconResourceComparator).map { it.url }

        assertEquals(
            listOf(
                "https://images-na.ssl-images-amazon.com/images/G/01/anywhere/a_smile_196x196._CB368246573_.png",
                "https://images-na.ssl-images-amazon.com/images/G/01/anywhere/a_smile_152x152._CB368246573_.png",
                "https://images-na.ssl-images-amazon.com/images/G/01/anywhere/a_smile_144x144._CB368211973_.png",
                "https://images-na.ssl-images-amazon.com/images/G/01/anywhere/a_smile_120x120._CB368246573_.png",
                "https://images-na.ssl-images-amazon.com/images/G/01/anywhere/a_smile_114x114._CB368212020_.png",
                "https://images-na.ssl-images-amazon.com/images/G/01/anywhere/a_smile_72x72._CB368212002_.png",
                "https://images-na.ssl-images-amazon.com/images/G/01/anywhere/a_smile_57x57._CB368212015_.png",
            ),
            urls,
        )
    }

    @Test
    fun `compare twitter-com icons`() {
        val resources =
            listOf(
                IconRequest.Resource(
                    url = "https://abs.twimg.com/favicons/favicon.ico",
                    type = IconRequest.Resource.Type.FAVICON,
                ),
                IconRequest.Resource(
                    url = "https://abs.twimg.com/responsive-web/web/icon-ios.8ea219d08eafdfa41.png",
                    type = IconRequest.Resource.Type.APPLE_TOUCH_ICON,
                ),
            )

        val urls = resources.sortedWith(IconResourceComparator).map { it.url }

        assertEquals(
            listOf(
                "https://abs.twimg.com/responsive-web/web/icon-ios.8ea219d08eafdfa41.png",
                "https://abs.twimg.com/favicons/favicon.ico",
            ),
            urls,
        )
    }

    @Test
    fun `compare github-com icons`() {
        val resources =
            listOf(
                IconRequest.Resource(
                    url = "https://github.githubassets.com/favicon.ico",
                    type = IconRequest.Resource.Type.FAVICON,
                ),
                IconRequest.Resource(
                    url = "https://github.com/fluidicon.png",
                    type = IconRequest.Resource.Type.FLUID_ICON,
                ),
                IconRequest.Resource(
                    url = "https://github.githubassets.com/images/modules/open_graph/github-logo.png",
                    type = IconRequest.Resource.Type.OPENGRAPH,
                ),
                IconRequest.Resource(
                    url = "https://github.githubassets.com/images/modules/open_graph/github-mark.png",
                    type = IconRequest.Resource.Type.OPENGRAPH,
                ),
                IconRequest.Resource(
                    url = "https://github.githubassets.com/images/modules/open_graph/github-octocat.png",
                    type = IconRequest.Resource.Type.OPENGRAPH,
                ),
            )

        val urls = resources.sortedWith(IconResourceComparator).map { it.url }

        assertEquals(
            listOf(
                "https://github.githubassets.com/favicon.ico",
                "https://github.com/fluidicon.png",
                "https://github.githubassets.com/images/modules/open_graph/github-logo.png",
                "https://github.githubassets.com/images/modules/open_graph/github-mark.png",
                "https://github.githubassets.com/images/modules/open_graph/github-octocat.png",
            ),
            urls,
        )
    }

    @Test
    fun `compare theverge-com icons`() {
        val resources =
            listOf(
                IconRequest.Resource(
                    url = "https://cdn.vox-cdn.com/uploads/chorus_asset/file/7395367/favicon-16x16.0.png",
                    type = IconRequest.Resource.Type.FAVICON,
                    sizes = listOf(Size(16, 16)),
                ),
                IconRequest.Resource(
                    url = "https://cdn.vox-cdn.com/uploads/chorus_asset/file/7395363/favicon-32x32.0.png",
                    type = IconRequest.Resource.Type.FAVICON,
                    sizes = listOf(Size(32, 32)),
                ),
                IconRequest.Resource(
                    url = "https://cdn.vox-cdn.com/uploads/chorus_asset/file/7395365/favicon-96x96.0.png",
                    type = IconRequest.Resource.Type.FAVICON,
                    sizes = listOf(Size(96, 96)),
                ),
                IconRequest.Resource(
                    url = "https://cdn.vox-cdn.com/uploads/chorus_asset/file/7395351/android-chrome-192x192.0.png",
                    type = IconRequest.Resource.Type.FAVICON,
                    sizes = listOf(Size(192, 192)),
                ),
                IconRequest.Resource(
                    url = "https://cdn.vox-cdn.com/uploads/chorus_asset/file/7395361/favicon-64x64.0.ico",
                    type = IconRequest.Resource.Type.FAVICON,
                ),
                IconRequest.Resource(
                    url = "https://cdn.vox-cdn.com/uploads/chorus_asset/file/7395359/ios-icon.0.png",
                    type = IconRequest.Resource.Type.APPLE_TOUCH_ICON,
                    sizes = listOf(Size(180, 180)),
                ),
                IconRequest.Resource(
                    url = "https://cdn.vox-cdn.com/uploads/chorus_asset/file/9672633/VergeOG.0_1200x627.0.png",
                    type = IconRequest.Resource.Type.OPENGRAPH,
                ),
                IconRequest.Resource(
                    url = "https://cdn.vox-cdn.com/community_logos/52803/VER_Logomark_175x92..png",
                    type = IconRequest.Resource.Type.TWITTER,
                ),
                IconRequest.Resource(
                    url =
                        "https://cdn.vox-cdn.com/uploads/chorus_asset/file/7396113/221a67c8-a10f-11e6-8fae-983107008690.0.png",
                    type = IconRequest.Resource.Type.MICROSOFT_TILE,
                ),
            )

        val urls = resources.sortedWith(IconResourceComparator).map { it.url }

        assertEquals(
            listOf(
                "https://cdn.vox-cdn.com/uploads/chorus_asset/file/7395359/ios-icon.0.png",
                "https://cdn.vox-cdn.com/uploads/chorus_asset/file/7395351/android-chrome-192x192.0.png",
                "https://cdn.vox-cdn.com/uploads/chorus_asset/file/7395365/favicon-96x96.0.png",
                "https://cdn.vox-cdn.com/uploads/chorus_asset/file/7395363/favicon-32x32.0.png",
                "https://cdn.vox-cdn.com/uploads/chorus_asset/file/7395367/favicon-16x16.0.png",
                "https://cdn.vox-cdn.com/uploads/chorus_asset/file/7395361/favicon-64x64.0.ico",
                "https://cdn.vox-cdn.com/uploads/chorus_asset/file/9672633/VergeOG.0_1200x627.0.png",
                "https://cdn.vox-cdn.com/community_logos/52803/VER_Logomark_175x92..png",
                "https://cdn.vox-cdn.com/uploads/chorus_asset/file/7396113/221a67c8-a10f-11e6-8fae-983107008690.0.png",
            ),
            urls,
        )
    }

    @Test
    fun `compare proxx-app icons`() {
        val resources =
            listOf(
                IconRequest.Resource(
                    url = "https://proxx.app/assets/icon-05a70868.png",
                    type = IconRequest.Resource.Type.MANIFEST_ICON,
                    sizes = listOf(Size(1024, 1024)),
                    mimeType = "image/png",
                ),
                IconRequest.Resource(
                    url = "https://proxx.app/assets/icon-maskable-7a2eb399.png",
                    type = IconRequest.Resource.Type.MANIFEST_ICON,
                    sizes = listOf(Size(1024, 1024)),
                    mimeType = "image/png",
                    maskable = true,
                ),
            )

        val urls = resources.sortedWith(IconResourceComparator).map { it.url }

        assertEquals(
            listOf(
                "https://proxx.app/assets/icon-maskable-7a2eb399.png",
                "https://proxx.app/assets/icon-05a70868.png",
            ),
            urls,
        )
    }

    @Test
    fun `comparator is transitive when the same URL is listed with different attributes`() {
        val a = IconRequest.Resource(url = "https://example.org/a.png", type = IconRequest.Resource.Type.FAVICON)
        val b =
            IconRequest.Resource(url = "https://example.org/b.png", type = IconRequest.Resource.Type.APPLE_TOUCH_ICON)
        val c = IconRequest.Resource(url = "https://example.org/a.png", type = IconRequest.Resource.Type.MANIFEST_ICON)

        // c is better than b, and b is better than a, so c must be better than a.
        assertTrue(IconResourceComparator.compare(c, b) < 0)
        assertTrue(IconResourceComparator.compare(b, a) < 0)
        assertTrue(IconResourceComparator.compare(c, a) < 0)
    }

    @Test
    fun `sorting many resources with repeated URLs does not violate the comparator contract`() {
        val types = IconRequest.Resource.Type.values()
        val random = Random(42)
        repeat(50) {
            val resources =
                List(200) {
                    IconRequest.Resource(
                        url = "https://example.org/icon-${random.nextInt(10)}.png",
                        type = types[random.nextInt(types.size)],
                        sizes = listOf(Size(random.nextInt(4) * 16, random.nextInt(4) * 16)),
                        mimeType = if (random.nextBoolean()) "image/x-icon" else "image/png",
                        maskable = random.nextBoolean(),
                    )
                }

            val sorted = resources.sortedWith(IconResourceComparator)

            for (i in 0 until sorted.size - 1) {
                assertTrue(IconResourceComparator.compare(sorted[i], sorted[i + 1]) <= 0)
            }
        }
    }
}
