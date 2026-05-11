/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { E10SUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/E10SUtils.sys.mjs"
);

/**
 * Tests for E10SUtils.getRemoteTypeForURIObject method, which is
 * used to compute preferred remote process types for content given
 * certain conditions.
 */

/**
 * Test that getRemoteTypeForURIObject returns the preferred remote type
 * when given a URI with an invalid site origin.
 *
 * This is a regression test for bug 1760417.
 */
add_task(async function test_invalid_site_origin() {
  const INVALID_SITE_ORIGIN_URI = Services.io.newURI(
    "https://.mozilla.org/this/is/a/test.html"
  );
  const EXPECTED_REMOTE_TYPE = `${E10SUtils.FISSION_WEB_REMOTE_TYPE}=https://.mozilla.org`;
  let result = E10SUtils.getRemoteTypeForURIObject(INVALID_SITE_ORIGIN_URI, {
    remoteSubFrames: true,
    multiProcess: true,
    preferredRemoteType: E10SUtils.DEFAULT_REMOTE_TYPE,
  });
  Assert.equal(
    result,
    EXPECTED_REMOTE_TYPE,
    "Got the expected default remote type."
  );
});
