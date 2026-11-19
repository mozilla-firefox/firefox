/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const { SelectionUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/SelectionUtils.sys.mjs"
);

const ELLIPSIS = Services.locale.ellipsis;

add_task(function test_truncateForDisplay_short_text() {
  Assert.equal(
    SelectionUtils.truncateForDisplay("hello", 5),
    "hello",
    "Text at the limit is returned as is"
  );
  Assert.equal(
    SelectionUtils.truncateForDisplay("hi", 5),
    "hi",
    "Text under the limit is returned as is"
  );
});

add_task(function test_truncateForDisplay_collapses_whitespace() {
  Assert.equal(
    SelectionUtils.truncateForDisplay("  a \n\t b   c  ", 10),
    "a b c",
    "Whitespace is trimmed and collapsed"
  );
  Assert.equal(
    SelectionUtils.truncateForDisplay("a  b  c  d", 5),
    "a b c" + ELLIPSIS,
    "The limit applies to the collapsed text"
  );
});

add_task(function test_truncateForDisplay_plain_text() {
  Assert.equal(
    SelectionUtils.truncateForDisplay("abcdefgh", 5),
    "abcde" + ELLIPSIS,
    "Plain text is cut at the limit"
  );
  Assert.equal(
    SelectionUtils.truncateForDisplay("abcdefgh", 5, { addEllipsis: false }),
    "abcde",
    "The ellipsis can be omitted"
  );
});

add_task(function test_truncateForDisplay_surrogate_pair() {
  Assert.equal(
    SelectionUtils.truncateForDisplay("abcd\u{1F600}efgh", 5),
    "abcd\u{1F600}" + ELLIPSIS,
    "A surrogate pair straddling the limit is kept whole"
  );
});

add_task(function test_truncateForDisplay_grapheme_clusters() {
  Assert.equal(
    SelectionUtils.truncateForDisplay("abcde\u{301}fgh", 5),
    "abcde\u{301}" + ELLIPSIS,
    "A combining mark after the limit stays with its base character"
  );
  Assert.equal(
    SelectionUtils.truncateForDisplay("abcde\u{301}fgh", 5, {
      addEllipsis: false,
    }),
    "abcde\u{301}",
    "The cut still moves past the cluster when the ellipsis is omitted"
  );

  const family = "\u{1F468}\u{200D}\u{1F469}\u{200D}\u{1F467}";
  Assert.equal(
    SelectionUtils.truncateForDisplay("ab" + family + "cdef", 4),
    "ab" + family + ELLIPSIS,
    "A ZWJ emoji sequence straddling the limit is kept whole"
  );

  const flag = "\u{1F1FA}\u{1F1F8}";
  Assert.equal(
    SelectionUtils.truncateForDisplay("ab" + flag + "cd", 4),
    "ab" + flag + ELLIPSIS,
    "A flag straddling the limit between its two halves is kept whole"
  );
});

add_task(function test_truncateForDisplay_exceedingly_long_cluster() {
  const marks = "\u{301}".repeat(1000);

  Assert.equal(
    SelectionUtils.truncateForDisplay("abc" + marks + "xyz", 4),
    "abc\u{301}" + ELLIPSIS,
    "A cluster far longer than the limit is split at the limit"
  );
  Assert.equal(
    SelectionUtils.truncateForDisplay("ab\u{1F600}" + marks, 3),
    "ab\u{1F600}" + ELLIPSIS,
    "An exceedingly long cluster is still not split inside a surrogate pair"
  );
  Assert.equal(
    SelectionUtils.truncateForDisplay("abc" + "\u{301}".repeat(30), 4),
    "abc" + "\u{301}".repeat(30),
    "A cluster within the overshoot allowance is kept whole"
  );
});

add_task(function test_truncateForDisplay_no_ellipsis_when_nothing_removed() {
  Assert.equal(
    SelectionUtils.truncateForDisplay("abcd\u{1F600}", 5),
    "abcd\u{1F600}",
    "Extending past the cut to the end of the text adds no ellipsis"
  );
  Assert.equal(
    SelectionUtils.truncateForDisplay("abcde\u{301}", 5),
    "abcde\u{301}",
    "A trailing combining mark adds no ellipsis"
  );
});
