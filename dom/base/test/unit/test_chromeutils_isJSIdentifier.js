"use strict";

add_task(function test_isJSIdentifier() {
  Assert.equal(ChromeUtils.isJSIdentifier("foo"), true);
  Assert.equal(ChromeUtils.isJSIdentifier("$foo"), true);
  Assert.equal(ChromeUtils.isJSIdentifier("foo1"), true);
  Assert.equal(ChromeUtils.isJSIdentifier("_foo"), true);

  Assert.equal(ChromeUtils.isJSIdentifier("foo-"), false);
  Assert.equal(ChromeUtils.isJSIdentifier("foo~"), false);
  Assert.equal(ChromeUtils.isJSIdentifier("1foo"), false);
  Assert.equal(ChromeUtils.isJSIdentifier("🤣fo"), false);
  Assert.equal(ChromeUtils.isJSIdentifier("fo🤣"), false);

  Assert.equal(ChromeUtils.isJSIdentifier("\u3042"), true);
  Assert.equal(ChromeUtils.isJSIdentifier("A\u3042"), true);
  Assert.equal(ChromeUtils.isJSIdentifier("\u3042\u3042"), true);
  Assert.equal(ChromeUtils.isJSIdentifier("\u{29E49}"), true);
  Assert.equal(ChromeUtils.isJSIdentifier("A\u{29E49}"), true);
  Assert.equal(ChromeUtils.isJSIdentifier("\u{29E49}\u{29E49}"), true);
  Assert.equal(ChromeUtils.isJSIdentifier("\uFF10"), false);
  Assert.equal(ChromeUtils.isJSIdentifier("A\uFF10"), true);
  Assert.equal(ChromeUtils.isJSIdentifier("\uFF10\uFF10"), false);
  Assert.equal(ChromeUtils.isJSIdentifier("\u{11067}"), false);
  Assert.equal(ChromeUtils.isJSIdentifier("A\u{11067}"), true);
  Assert.equal(ChromeUtils.isJSIdentifier("\u{11067}\u{11067}"), false);

  Assert.equal(ChromeUtils.isJSIdentifier("A\0"), false);
  Assert.equal(ChromeUtils.isJSIdentifier("\0A"), false);
  Assert.equal(ChromeUtils.isJSIdentifier("A\0B"), false);

  Assert.equal(ChromeUtils.isJSIdentifier(""), false);
});
