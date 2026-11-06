/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.ktx.kotlin

import androidx.test.ext.junit.runners.AndroidJUnit4
import java.util.Calendar
import java.util.Calendar.MILLISECOND
import kotlin.test.assertNotNull
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import mozilla.components.support.ktx.helpers.ShadowInetAddresses
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Ignore
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.annotation.Config

const val PUNYCODE = "xn--kpry57d"
const val IDN = "台灣"

@RunWith(AndroidJUnit4::class)
@Config(shadows = [ShadowInetAddresses::class])
class StringTest {

    private val publicSuffixList = PublicSuffixList(testContext)

    @Test
    fun isUrl() {
        assertTrue("mozilla.org".isUrl())
        assertTrue(" mozilla.org ".isUrl())
        assertTrue("http://mozilla.org".isUrl())
        assertTrue("https://mozilla.org".isUrl())
        assertTrue("file://somefile.txt".isUrl())
        assertTrue("http://mozilla".isUrl())
        assertTrue("http://192.168.255.255".isUrl())
        assertTrue("about:crashcontent".isUrl())
        assertTrue(" about:crashcontent ".isUrl())
        assertTrue("sample:about ".isUrl())

        assertFalse("mozilla".isUrl())
        assertFalse("mozilla android".isUrl())
        assertFalse(" mozilla android ".isUrl())
        assertFalse("Tweet:".isUrl())
        assertFalse("inurl:mozilla.org advanced search".isUrl())
        assertFalse("what is about:crashes".isUrl())

        val extraText = "Check out @asa’s Tweet: https://twitter.com/asa/status/123456789?s=09"
        val url = extraText.split(" ").find { it.isUrl() }
        assertNotEquals("Tweet:", url)
    }

    @Test
    fun `GIVEN a content URL WHEN calling isContentUrl THEN true is returned`() {
        val url = "content://contenturl"
        assertTrue(url.isContentUrl())
    }

    @Test
    fun `GIVEN an internet URL WHEN calling isContentUrl THEN false is returned`() {
        val url = "https://mozilla.org"
        assertFalse(url.isContentUrl())
    }

    @Test
    fun `GIVEN an about URL WHEN calling isAboutUrl THEN true is returned`() {
        val url = "about:about"
        assertTrue(url.isAboutUrl())
    }

    @Test
    fun `GIVEN an internet URL WHEN calling isAboutUrl THEN false is returned`() {
        val url = "https://mozilla.org"
        assertFalse(url.isAboutUrl())
    }

    @Test
    fun toNormalizedUrl() {
        val expectedUrl = "http://mozilla.org"
        assertEquals(expectedUrl, "http://mozilla.org".toNormalizedUrl())
        assertEquals(expectedUrl, "  http://mozilla.org  ".toNormalizedUrl())
        assertEquals(expectedUrl, "mozilla.org".toNormalizedUrl())
        assertEquals("https://mozilla.org/a%20b", "https://mozilla.org/a b".toNormalizedUrl())
        assertEquals("http://mozilla.org/a%20b", "mozilla.org/a b".toNormalizedUrl())
    }

    @Test
    fun isPhone() {
        assertTrue("tel:+1234567890".isPhone())
        assertTrue(" tel:+1234567890".isPhone())
        assertTrue("tel:+1234567890 ".isPhone())
        assertTrue("tel:+1234567890 ".isPhone())
        assertTrue("TEL:+1234567890".isPhone())
        assertTrue("Tel:+1234567890".isPhone())

        assertFalse("tel:word".isPhone())
    }

    @Test
    fun isEmail() {
        assertTrue("mailto:asa@mozilla.com".isEmail())
        assertTrue(" mailto:asa@mozilla.com".isEmail())
        assertTrue("mailto:asa@mozilla.com ".isEmail())
        assertTrue("MAILTO:asa@mozilla.com".isEmail())
        assertTrue("Mailto:asa@mozilla.com".isEmail())
    }

    @Test
    fun geoLocation() {
        assertTrue("geo:1,-1".isGeoLocation())
        assertTrue("geo:1,-1;u=1".isGeoLocation())
        assertTrue("geo:1,-1,0.5;u=1".isGeoLocation())
        assertTrue(" geo:1,-1".isGeoLocation())
        assertTrue("geo:1,-1 ".isGeoLocation())
        assertTrue("GEO:1,-1".isGeoLocation())
        assertTrue("Geo:1,-1".isGeoLocation())
    }

    @Test
    fun toDate() {
        val calendar = Calendar.getInstance()
        calendar.set(2019, 10, 29, 0, 0, 0)
        calendar[MILLISECOND] = 0
        assertEquals(calendar.time, "2019-11-29".toDate("yyyy-MM-dd"))
        calendar.set(2019, 10, 28, 0, 0, 0)
        assertEquals(calendar.time, "2019-11-28".toDate("yyyy-MM-dd"))
        assertNotNull("".toDate("yyyy-MM-dd"))
    }

    @Test
    fun `string to date conversion using multiple formats`() {
        assertEquals("2019-08-16T01:02".toDate("yyyy-MM-dd'T'HH:mm"), "2019-08-16T01:02".toDate())

        assertEquals("2019-08-16T01:02:03".toDate("yyyy-MM-dd'T'HH:mm"), "2019-08-16T01:02:03".toDate())

        assertEquals("2019-08-16".toDate("yyyy-MM-dd"), "2019-08-16".toDate())
    }

    @Test
    fun sha1() {
        assertEquals("da39a3ee5e6b4b0d3255bfef95601890afd80709", "".sha1())

        assertEquals("0a4d55a8d778e5022fab701977c5d840bbc486d0", "Hello World".sha1())

        assertEquals("8de545c123907e9f886ba2313560a0abef530594", "ßüöä@!§\$".sha1())
    }

    @Test
    fun `Try Get Host From Url`() {
        val urlTest = "http://www.example.com:1080/docs/resource1.html"
        val new = urlTest.tryGetHostFromUrl()
        assertEquals(new, "www.example.com")
    }

    @Test
    fun `Try Get Host From Malformed Url`() {
        val urlTest = "notarealurl"
        val new = urlTest.tryGetHostFromUrl()
        assertEquals(new, "notarealurl")
    }

    @Test
    fun `Extract host from a complete Url`() {
        val urlTest = "https://www.mozilla.com:1080/docs/resource1.html"
        val new = urlTest.extractHostUrl()
        assertEquals(new, "www.mozilla.com")
    }

    @Test
    fun `Extract host from a Url without www`() {
        val urlTest = "http://mozinlla.org/someoteher/domainthing/with/?=skdfjk"
        val new = urlTest.extractHostUrl()
        assertEquals(new, "mozinlla.org")
    }

    @Test
    fun `Extract host from a Url with new line character`() {
        val urlTest = "\nhttp://mozi\nlla.org:1080/docs/resource1.html\n"
        val new = urlTest.extractHostUrl()
        assertEquals(new, "mozilla.org")
    }

    @Test
    fun `Extract host from a malformed Url returns trimmed input`() {
        val urlTest = "  notarealurl  "
        val new = urlTest.extractHostUrl()
        assertEquals(new, "notarealurl")
    }

    @Test
    fun `Extract host from IPv4 Url`() {
        val urlTest = "http://192.168.0.1/some/path"
        val new = urlTest.extractHostUrl()
        assertEquals(new, "192.168.0.1")
    }

    @Test
    fun isSameOriginAs() {
        // Host mismatch.
        assertFalse("https://foo.bar".isSameOriginAs("https://foo.baz"))
        // Scheme mismatch.
        assertFalse("http://127.0.0.1".isSameOriginAs("https://127.0.0.1"))
        // Port mismatch (implicit + explicit).
        assertFalse("https://foo.bar:444".isSameOriginAs("https://foo.bar"))
        // Port mismatch (explicit).
        assertFalse("https://foo.bar:444".isSameOriginAs("https://foo.bar:555"))
        // Port OK but scheme different.
        assertFalse("https://foo.bar".isSameOriginAs("http://foo.bar:443"))
        // Port OK (explicit) but scheme different.
        assertFalse("https://foo.bar:443".isSameOriginAs("ftp://foo.bar:443"))

        assertTrue("https://foo.bar".isSameOriginAs("https://foo.bar"))
        assertTrue("https://foo.bar/bobo".isSameOriginAs("https://foo.bar/obob"))
        assertTrue("https://foo.bar".isSameOriginAs("https://foo.bar:443"))
        assertTrue("https://foo.bar:333".isSameOriginAs("https://foo.bar:333"))
    }

    @Test
    fun isExtensionUrl() {
        assertTrue("moz-extension://1232-abcd".isExtensionUrl())
        assertFalse("mozilla.org".isExtensionUrl())
        assertFalse("https://mozilla.org".isExtensionUrl())
        assertFalse("http://mozilla.org".isExtensionUrl())
    }

    @Test
    fun sanitizeURL() {
        val expectedUrl = "http://mozilla.org"
        assertEquals(expectedUrl, "\nhttp://mozi\nlla.org\n".sanitizeURL())
    }

    @Test
    fun isResourceUrl() {
        assertTrue("resource://1232-abcd".isResourceUrl())
        assertFalse("mozilla.org".isResourceUrl())
        assertFalse("https://mozilla.org".isResourceUrl())
        assertFalse("http://mozilla.org".isResourceUrl())
    }

    @Test
    fun sanitizeFileName() {
        val testCases =
            listOf(
                "/../../../../../../../../../../directory/file.......txt" to "file.txt",
                "/root/directory/file.txt" to "file.txt",
                "file" to "file",
                "file.." to "file",
                "file." to "file",
                ".file" to "file",
                "test.2020.12.01.txt" to "test.2020.12.01.txt",
                "\u0000filename" to "_filename",
                "file\u0001name" to "file_name",
                "data\u0002stream" to "data_stream",
                "end\u0003text" to "end_text",
                "trans\u0004mission" to "trans_mission",
                "query\u0005result" to "query_result",
                "acknowledge\u0006signal" to "acknowledge_signal",
                "bell\u0007sound" to "bell_sound",
                "back\u0008space" to "back_space",
                "horizontal\u0009tab" to "horizontal tab",
                "new\u000Aline" to "new line",
                "vertical\u000Btab" to "vertical tab",
                "form\u000Cfeed" to "form feed",
                "return\u000Dcarriage" to "return carriage",
                "shift\u000Eout" to "shift_out",
                "shift\u000Fin" to "shift_in",
                "escape\u0010data" to "escape_data",
                "device\u0011control1" to "device_control1",
                "device\u0012control2" to "device_control2",
                "device\u0013control3" to "device_control3",
                "less<than" to "less_than",
                "greater>than" to "greater_than",
                "asterisk*" to "asterisk_",
                "quotation\"mark" to "quotation_mark",
                "colon:" to "colon_",
                "question?mark" to "question_mark",
                "back\\slash" to "back_slash",
                "vertical|bar" to "vertical_bar",
                "This\u00A0is\u00A0no-break space" to "This is no-break space",
                "This\u1680is\u1680ogham space mark" to "This is ogham space mark",
                "This\u2000is\u2000en quad" to "This is en quad",
                "This\u2001is\u2001em quad" to "This is em quad",
                "This\u2002is\u2002en space" to "This is en space",
                "This\u2003is\u2003em space" to "This is em space",
                "This\u2004is\u2004three-per-em space" to "This is three-per-em space",
                "This\u2005is\u2005four-per-em space" to "This is four-per-em space",
                "This\u2006is\u2006six-per-em space" to "This is six-per-em space",
                "This\u2007is\u2007figure space" to "This is figure space",
                "This\u2008is\u2008punctuation space" to "This is punctuation space",
                "This\u2009is\u2009thin space" to "This is thin space",
                "This\u200Ais\u200Ahair space" to "This is hair space",
                "This\u2028is\u2028line separator" to "This is line separator",
                "This\u2029is\u2029paragraph separator" to "This is paragraph separator",
                "This\u202Fis\u202Fnarrow no-break space" to "This is narrow no-break space",
                "This\u205Fis\u205Fmedium mathematical space" to "This is medium mathematical space",
                "This\u3000is\u3000ideographic space" to "This is ideographic space",
                "This   is    a    text   with multiple   spaces.\nAnd  tabs\tand newlines\n\n" to
                    "This is a text with multiple spaces. And tabs and newlines",
                "This\u00A0\u1680\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200A\u2028\u2029\u202F\u205F\u3000is a mix of different spaces" to
                    "This is a mix of different spaces",
            )

        testCases.forEach { (raw, escaped) ->
            assertEquals(escaped, raw.sanitizeFileName())
        }
    }

    @Test
    fun truncateMiddle() {
        assertEquals("123…890", "1234567890".truncateMiddle(7))
        assertEquals("1234567890", "1234567890".truncateMiddle(10))
        assertEquals("1234567890", "1234567890".truncateMiddle(20))
        assertEquals("1…0", "1234567890".truncateMiddle(3))
        assertEquals("1", "1234567890".truncateMiddle(1))
        assertEquals("123...890", "1234567890".truncateMiddle(9, "..."))
    }

    @Test
    fun `WHEN a string contains utf 8 encoded characters or illegal filename characters decode decodes it`() {
        // List of pairs of encoded strings or illegal filename characters and their expected decoded results
        val testCases =
            listOf(
                "hello%20world" to "hello world",
                "wow%21amazing" to "wow!amazing",
                "quote%22here%22" to "quote\"here\"",
                "hash%23tag" to "hash#tag",
                "save%24money" to "save\$money",
                "100%25complete" to "100%complete",
                "you%26me" to "you&me",
                "it%27s%20easy" to "it's easy",
                "open%28now%29" to "open(now)",
                "star%2Ashine" to "star*shine",
                "add%2Bmore" to "add+more",
                "comma%2Cseparated" to "comma,separated",
                "dash%2Dbetween" to "dash-between",
                "end%2Eperiod" to "end.period",
                "path%2Fto%2Ffile" to "path/to/file",
                "time%3A12%3A00" to "time:12:00",
                "wait%3Bplease" to "wait;please",
                "less%3Cthan" to "less<than",
                "equals%3Dsign" to "equals=sign",
                "greater%3Ethan" to "greater>than",
                "what%3Fwhere" to "what?where",
                "email%40domain.com" to "email@domain.com",
                "bracket%5Bopen%5D" to "bracket[open]",
                "escape%5Cbackslash" to "escape\\backslash",
                "bracket%5Dclose%5D" to "bracket]close]",
                "high%5Efive" to "high^five",
                "accent%60grave" to "accent`grave",
                "brace%7Bopenclose%7D" to "brace{openclose}",
                "pipe%7Csymbol" to "pipe|symbol",
                "tilde%7Ewave" to "tilde~wave",
            )

        testCases.forEach { (encoded, decoded) ->
            assertEquals(decoded, encoded.decode())
        }
    }

    @Test
    fun `getDataUrlImageExtension returns a default extension if one cannot be extracted from the data url`() {
        val base64Image = "data:;base64,testImage"

        val result = base64Image.getDataUrlImageExtension()

        assertEquals("jpg", result)
    }

    @Test
    fun `getDataUrlImageExtension returns an extension based on the media type included in the the data url`() {
        val base64Image = "data:image/gif;base64,testImage"

        val result = base64Image.getDataUrlImageExtension()

        assertEquals("gif", result)
    }

    @Test
    fun `ifNullOrEmpty returns the same if this CharSequence is not null and not empty`() {
        val randomString = "something"

        assertSame(randomString, randomString.ifNullOrEmpty { "something else" })
    }

    @Test
    fun `ifNullOrEmpty returns the invocation of the passed in argument if this CharSequence is null`() {
        val nullString: String? = null
        val validResult = "notNullString"

        assertSame(validResult, nullString.ifNullOrEmpty { validResult })
    }

    @Test
    fun `ifNullOrEmpty returns the invocation of the passed in argument if this CharSequence is empty`() {
        val nullString = ""
        val validResult = "notEmptyString"

        assertSame(validResult, nullString.ifNullOrEmpty { validResult })
    }

    @Test
    fun `getRepresentativeCharacter returns the correct representative character for the given urls`() {
        assertEquals("M", "https://mozilla.org".getRepresentativeCharacter())
        assertEquals("W", "http://wikipedia.org".getRepresentativeCharacter())
        assertEquals("P", "http://plus.google.com".getRepresentativeCharacter())
        assertEquals("E", "https://en.m.wikipedia.org/wiki/Main_Page".getRepresentativeCharacter())

        // Stripping common prefixes
        assertEquals("T", "http://www.theverge.com".getRepresentativeCharacter())
        assertEquals("F", "https://m.facebook.com".getRepresentativeCharacter())
        assertEquals("T", "https://mobile.twitter.com".getRepresentativeCharacter())

        // Special urls
        assertEquals("?", "file:///".getRepresentativeCharacter())
        assertEquals("S", "file:///system/".getRepresentativeCharacter())
        assertEquals("P", "ftp://people.mozilla.org/test".getRepresentativeCharacter())

        // No values
        assertEquals("?", "".getRepresentativeCharacter())

        // Rubbish
        assertEquals("Z", "zZz".getRepresentativeCharacter())
        assertEquals("Ö", "ölkfdpou3rkjaslfdköasdfo8".getRepresentativeCharacter())
        assertEquals("?", "_*+*'##".getRepresentativeCharacter())
        assertEquals("ツ", "¯\\_(ツ)_/¯".getRepresentativeCharacter())
        assertEquals("ಠ", "ಠ_ಠ Look of Disapproval".getRepresentativeCharacter())

        // Non-ASCII
        assertEquals("Ä", "http://www.ätzend.de".getRepresentativeCharacter())
        assertEquals("名", "http://名がドメイン.com".getRepresentativeCharacter())
        assertEquals("C", "http://√.com".getRepresentativeCharacter())
        assertEquals("SS", "http://ß.de".getRepresentativeCharacter())
        assertEquals("Ԛ", "http://ԛәлп.com/".getRepresentativeCharacter()) // cyrillic

        // Punycode
        assertEquals("X", "http://xn--tzend-fra.de".getRepresentativeCharacter()) // ätzend.de
        assertEquals("X", "http://xn--V8jxj3d1dzdz08w.com".getRepresentativeCharacter()) // 名がドメイン.com

        // Numbers
        assertEquals("1", "https://www.1and1.com/".getRepresentativeCharacter())

        // IP
        assertEquals("1", "https://192.168.0.1".getRepresentativeCharacter())
    }

    @Test
    fun `last4Digits returns a string with only last 4 digits `() {
        assertEquals("8431", "371449635398431".last4Digits())
        assertEquals("2345", "12345".last4Digits())
        assertEquals("1234", "1234".last4Digits())
        assertEquals("123", "123".last4Digits())
        assertEquals("1", "1".last4Digits())
        assertEquals("", "".last4Digits())
    }

    @Test
    fun `when the full hostname cannot be displayed, elide labels starting from the front`() {
        // See https://url.spec.whatwg.org/#url-rendering-elision
        // See
        // https://chromium.googlesource.com/chromium/src/+/master/docs/security/url_display_guidelines/url_display_guidelines.md#eliding-urls

        val display = "http://1.2.3.4.5.6.7.8.9.10.11.12.13.14.15.16.17.18.19.20.21.22.23.24.25.com".shortened()

        val split = display.split(".")

        // If the list ends with 25.com...
        assertEquals("25", split.dropLast(1).last())
        // ...and each value is 1 larger than the last...
        split
            .dropLast(1)
            .map { it.toInt() }
            .windowed(2, 1)
            .forEach { (prev, next) ->
                assertEquals(next, prev + 1)
            }
        // ...that means that all removed values came from the front of the list
    }

    @Test
    fun `the registrable domain is always displayed`() {
        // https://url.spec.whatwg.org/#url-rendering-elision
        // See
        // https://chromium.googlesource.com/chromium/src/+/master/docs/security/url_display_guidelines/url_display_guidelines.md#eliding-urls

        val bigRegistrableDomain = "evil-but-also-shockingly-long-registrable-domain.com"
        assertTrue(
            "https://login.your-bank.com.$bigRegistrableDomain/enter/your/password"
                .shortened()
                .contains(bigRegistrableDomain)
        )
    }

    @Test
    fun `url username and password fields should not be displayed`() {
        // See https://url.spec.whatwg.org/#url-rendering-simplification
        // See
        // https://chromium.googlesource.com/chromium/src/+/master/docs/security/url_display_guidelines/url_display_guidelines.md#simplify

        assertFalse("https://examplecorp.com@attacker.example/".shortened().contains("examplecorp"))
        assertFalse("https://examplecorp.com@attacker.example/".shortened().contains("com"))
        assertFalse("https://user:password@example.com/".shortened().contains("user"))
        assertFalse("https://user:password@example.com/".shortened().contains("password"))
    }

    @Test
    fun `eTLDs should not be dropped`() {
        // See https://bugzilla.mozilla.org/show_bug.cgi?id=1554984#c11
        "http://mozfreddyb.github.io/" shortenedShouldBecome "mozfreddyb.github.io"
        "http://web.security.plumbing/" shortenedShouldBecome "web.security.plumbing"
    }

    @Test
    fun `ipv4 addresses should be returned as is`() {
        // See https://bugzilla.mozilla.org/show_bug.cgi?id=1554984#c11
        "192.168.85.1" shortenedShouldBecome "192.168.85.1"
    }

    @Test
    fun `about buildconfig should not be modified`() {
        // See https://bugzilla.mozilla.org/show_bug.cgi?id=1554984#c11
        "about:buildconfig" shortenedShouldBecome "about:buildconfig"
    }

    @Test
    fun `encoded userinfo should still be considered userinfo`() {
        "https://user:password%40really.evil.domain%2F@mail.google.com" shortenedShouldBecome "mail.google.com"
    }

    @Test
    @Ignore("This would be more correct, but does not appear to be an attack vector")
    fun `should decode DWORD IP addresses`() {
        "https://16843009" shortenedShouldBecome "1.1.1.1"
    }

    @Test
    @Ignore("This would be more correct, but does not appear to be an attack vector")
    fun `should decode octal IP addresses`() {
        "https://000010.000010.000010.000010" shortenedShouldBecome "8.8.8.8"
    }

    @Test
    @Ignore("This would be more correct, but does not appear to be an attack vector")
    fun `should decode hex IP addresses`() {
        "http://0x01010101" shortenedShouldBecome "1.1.1.1"
    }

    // BEGIN test cases borrowed from desktop (shortUrl is used for Top Sites on new tab)
    // Test cases are modified, as we show the eTLD
    // (https://searchfox.org/firefox-main/source/toolkit/modules/tests/xpcshell/test_NewTabUtils.js)
    @Test
    fun `should return a blank string if url is blank`() {
        "" shortenedShouldBecome ""
    }

    @Test
    fun `should return the 'url' if not a valid url`() {
        "something" shortenedShouldBecome "something"
        "http:" shortenedShouldBecome "http:"
        "http::double-colon" shortenedShouldBecome "http::double-colon"
        // The largest allowed port is 65,535
        "http://badport:65536/" shortenedShouldBecome "http://badport:65536/"
    }

    @Test
    fun `should losslessly decode the path of a url it cannot shorten`() {
        "http://badport:65536/%D1%82%D0%B5%D1%81%D1%82%20file" shortenedShouldBecome "http://badport:65536/тест%20file"
        "http://$PUNYCODE.com/%D1%82%D0%B5%D1%81%D1%82" shortenedShouldBecome "$IDN.com"
    }

    @Test
    fun `should convert host to idn when calling shortURL`() {
        "http://$PUNYCODE.blah.com" shortenedShouldBecome "$IDN.blah.com"
    }

    @Test
    fun `should get the hostname from url`() {
        "http://bar.com" shortenedShouldBecome "bar.com"
    }

    @Test
    @Config(sdk = [28])
    fun `should not strip out www if not first subdomain`() {
        "http://foo.www.com" shortenedShouldBecome "foo.www.com"
        "http://www.foo.www.com" shortenedShouldBecome "foo.www.com"
    }

    @Test
    @Config(sdk = [28])
    fun `should convert to lowercase`() {
        "HTTP://FOO.COM" shortenedShouldBecome "foo.com"
    }

    @Test
    fun `should not include the port`() {
        "http://foo.com:8888" shortenedShouldBecome "foo.com"
    }

    @Test
    fun `should return hostname for localhost`() {
        "http://localhost:8000/" shortenedShouldBecome "localhost"
    }

    @Test
    fun `should return hostname for ip address`() {
        "http://127.0.0.1/foo" shortenedShouldBecome "127.0.0.1"
    }

    @Test
    @Config(sdk = [28])
    fun `should return etld for www gov uk (www-only non-etld)`() {
        "https://www.gov.uk/countersigning" shortenedShouldBecome "gov.uk"
    }

    @Test
    fun `should return idn etld for www-only non-etld`() {
        "https://www.$PUNYCODE/foo" shortenedShouldBecome IDN
    }

    @Test
    fun `file uri should return input`() {
        "file:///foo/bar.txt" shortenedShouldBecome "file:///foo/bar.txt"
    }

    @Test
    @Ignore("This behavior conflicts with https://bugzilla.mozilla.org/show_bug.cgi?id=1554984#c11")
    fun `should return not the protocol for about`() {
        "about:newtab" shortenedShouldBecome "newtab"
    }

    @Test
    fun `should fall back to full url as a last resort`() {
        "about:" shortenedShouldBecome "about:"
    }

    // END test cases borrowed from desktop

    // BEGIN test cases borrowed from FFTV
    // (https://searchfox.org/mozilla-mobile/source/firefox-echo-show/app/src/test/java/org/mozilla/focus/utils/TestFormattedDomain.java#228)
    @Test
    fun testIsIPv4RealAddress() {
        assertTrue("192.168.1.1".isIpv4())
        assertTrue("8.8.8.8".isIpv4())
        assertTrue("63.245.215.20".isIpv4())
    }

    @Test
    fun testIsIPv4WithProtocol() {
        assertFalse("http://8.8.8.8".isIpv4())
        assertFalse("https://8.8.8.8".isIpv4())
    }

    @Test
    fun testIsIPv4WithPort() {
        assertFalse("8.8.8.8:400".isIpv4())
        assertFalse("8.8.8.8:1337".isIpv4())
    }

    @Test
    fun testIsIPv4WithPath() {
        assertFalse("8.8.8.8/index.html".isIpv4())
        assertFalse("8.8.8.8/".isIpv4())
    }

    @Test
    fun testIsIPv4WithIPv6() {
        assertFalse("2001:db8::1 ".isIpv4())
        assertFalse("2001:db8:0:1:1:1:1:1".isIpv4())
        assertFalse("[2001:db8:a0b:12f0::1]".isIpv4())
        assertFalse("2001:db8: 3333:4444:5555:6666:1.2.3.4".isIpv4())
    }

    @Test
    fun testIsIPv6WithIPv6() {
        assertTrue("2001:db8::1".isIpv6())
        assertTrue("2001:db8:0:1:1:1:1:1".isIpv6())
    }

    @Test
    fun testIsIPv6WithIPv4() {
        assertFalse("192.168.1.1".isIpv6())
        assertFalse("8.8.8.8".isIpv6())
        assertFalse("63.245.215.20".isIpv6())
    }

    // END test cases borrowed from FFTV

    @Test
    fun testStripCommonSubdomains() {
        assertEquals("mozilla.org", ("mozilla.org").stripCommonSubdomains())
        assertEquals("mozilla.org", ("www.mozilla.org").stripCommonSubdomains())
        assertEquals("mozilla.org", ("m.mozilla.org").stripCommonSubdomains())
        assertEquals("mozilla.org", ("mobile.mozilla.org").stripCommonSubdomains())
        assertEquals("random.mozilla.org", ("random.mozilla.org").stripCommonSubdomains())
    }

    @Test
    fun `GIVEN an invalid base64 image string WHEN converting it into bitmap THEN the result is null`() {
        val invalidBase64BitmapString = "aa"
        assertNull(invalidBase64BitmapString.base64ToBitmap(ICON_PX, ICON_PX))
    }

    @Test
    fun `GIVEN a valid base64 png string WHEN converting it into bitmap THEN the result is not null and no exception is thrown`() {
        val validBase64BitmapString = "data:image/png;base64,R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs="
        assertNotNull(validBase64BitmapString.base64ToBitmap(ICON_PX, ICON_PX))
    }

    @Test
    fun `GIVEN a valid base64 image string WHEN converting it into bitmap THEN the result is not null and no exception is thrown`() {
        val validBase64JpegString = "data:image/jpeg;base64,R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs="
        val validBase64JpgString = "data:image/jpg;base64,R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs="
        val validBase64AnythingString = "data:image/anything;base64,R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs="
        assertNotNull(validBase64JpegString.base64ToBitmap(ICON_PX, ICON_PX))
        assertNotNull(validBase64JpgString.base64ToBitmap(ICON_PX, ICON_PX))
        assertNotNull(validBase64AnythingString.base64ToBitmap(ICON_PX, ICON_PX))
    }

    @Test
    fun `GIVEN invalid base64 image strings WHEN converting them into bitmap THEN the result is null`() {
        val invalidBase64String = "R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs="
        val invalidBase64String2 = "data:image/jpg;base64;R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs="
        val invalidBase64String3 = "image/jpg;base64,R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs="
        assertNull(invalidBase64String.base64ToBitmap(ICON_PX, ICON_PX))
        assertNull(invalidBase64String2.base64ToBitmap(ICON_PX, ICON_PX))
        assertNull(invalidBase64String3.base64ToBitmap(ICON_PX, ICON_PX))
    }

    @Test
    fun `GIVEN a base64 image larger than the target WHEN converting it into a bitmap THEN it is subsampled`() {
        val bitmap = FOUR_BY_FOUR_PNG.base64ToBitmap(targetWidth = 1, targetHeight = 1)

        assertNotNull(bitmap)
        assertEquals(1, bitmap.width)
        assertEquals(1, bitmap.height)
    }

    @Test
    fun `GIVEN a data URI whose payload is not base64 WHEN converting it into a bitmap THEN null is returned`() {
        // The other invalid cases here are rejected by the data URI pattern, so they never reach the decode.
        assertNull("data:image/png;base64,!!!!not-base64!!!!".base64ToBitmap(ICON_PX, ICON_PX))
    }

    @Test
    fun `GIVEN a base64 image smaller than the target WHEN converting it into a bitmap THEN it is decoded whole`() {
        val bitmap = FOUR_BY_FOUR_PNG.base64ToBitmap(targetWidth = 64, targetHeight = 64)

        assertNotNull(bitmap)
        assertEquals(4, bitmap.width)
        assertEquals(4, bitmap.height)
    }

    @Test
    fun `GIVEN a valid or invalid base64 image string WHEN extracting its raw content string THEN the result is correct`() {
        val validBase64JpegString = "data:image/jpeg;base64,R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs="
        val validBase64JpgString = "data:image/jpeg;base64,R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs="
        val validBase64PngString = "data:image/jpeg;base64,R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs="
        val invalidBase64String = "R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs="
        val invalidBase64String2 = "data:image/jpeg;base64;R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs="
        assertEquals("R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs=", validBase64JpegString.extractBase6RawString())
        assertEquals("R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs=", validBase64JpgString.extractBase6RawString())
        assertEquals("R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs=", validBase64PngString.extractBase6RawString())
        assertNull(invalidBase64String.extractBase6RawString())
        assertNull(invalidBase64String2.extractBase6RawString())
    }

    @Test
    fun `GIVEN a URL with matching parameters WHEN testing if a URL contains query parameters THEN the result is true`() {
        assertTrue("http://example.com?a".urlContainsQueryParameters("a"))
        assertTrue("http://example.com?a&b&c".urlContainsQueryParameters("b"))
        assertTrue("http://example.com?a=b".urlContainsQueryParameters("a=b"))
        assertTrue("http://example.com?a=b&c=d&e=f".urlContainsQueryParameters("c=d"))
        assertTrue("http://example.com?a=b&c=d&e=f#g=h".urlContainsQueryParameters("e=f"))
    }

    @Test
    fun `GIVEN a URL without matching parameters WHEN testing if a URL contains query parameters THEN the result is false`() {
        assertFalse("".urlContainsQueryParameters("a"))
        assertFalse("!@#$%^&*()-+".urlContainsQueryParameters("a"))
        assertFalse("http://example.com".urlContainsQueryParameters("a"))
        assertFalse("http://example.com?a&b".urlContainsQueryParameters("c"))
        assertFalse("http://example.com?a=b".urlContainsQueryParameters("a"))
        assertFalse("http://example.com?a=b&c=d&e=f#g=h".urlContainsQueryParameters("g=h"))
    }

    @Test
    fun `GIVEN a camel case string WHEN converting it to snake case THEN its words are lowercased and joined by underscores`() {
        assertEquals("enrolled_and_entitled", "EnrolledAndEntitled".toSnakeCase())
        assertEquals("user_action", "userAction".toSnakeCase())
        assertEquals("ready", "Ready".toSnakeCase())
        assertEquals("a_b_test", "ABTest".toSnakeCase())
        assertEquals("", "".toSnakeCase())
    }

    @Test
    fun `toWellFormed only replaces unpaired surrogates`() {
        val valid = "abc \uD83D\uDE00 def"
        assertSame(valid, valid.toWellFormed())
        assertEquals("", "".toWellFormed())
        assertEquals("\uFFFD", "\uD83D".toWellFormed())
        assertEquals("\uFFFD", "\uDE00".toWellFormed())
        assertEquals("a\uFFFDb", "a\uD83Db".toWellFormed())
        assertEquals("\uFFFD\uFFFD", "\uDE00\uD83D".toWellFormed())
        assertEquals("\uFFFD\uD83D\uDE00", "\uD83D\uD83D\uDE00".toWellFormed())
        assertEquals("\uD83D\uDE00\uFFFD", "\uD83D\uDE00\uDE00".toWellFormed())
    }

    private infix fun String.shortenedShouldBecome(expect: String) {
        assertEquals(expect, this.shortened())
    }

    private fun String.shortened() = this.toShortUrl(publicSuffixList)
}

private const val ICON_PX = 32

// A 4x4 PNG, small enough to inline but large enough to subsample.
private const val FOUR_BY_FOUR_PNG =
    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAIAAAAmkwkpAAAAEElEQVR42mMQVDKGIwbiOACj1AZhWTrROwAAAABJRU5ErkJggg=="
