/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Regression test ensuring that that a STORED entry with differing compressed
// and uncompressed sizes is considered to be corrupt.
//
// This fixture's End Of Central Directory record points past the end of the
// file, so opening it is now rejected outright rather than succeeding and
// failing later during test().

add_task(async function test1801102() {
  let file = do_get_file("data/test_1801102.jar");

  let zipreader = Cc["@mozilla.org/libjar/zip-reader;1"].createInstance(
    Ci.nsIZipReader
  );
  Assert.throws(() => zipreader.open(file), /NS_ERROR_FAILURE/, "must throw");
});
