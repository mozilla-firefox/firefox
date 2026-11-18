"use strict";

const PATH = "/browser/dom/base/test/";
const SAME_SITE_HOP_1 = `https://example.com${PATH}file_speculation_tags_redirect.sjs?hop=1`;
const CROSS_SITE_HOP_2 = `https://example.org${PATH}file_speculation_tags_redirect.sjs?hop=2`;
const SAME_SITE_HOP_3 = `https://example.com${PATH}file_speculation_tags_redirect.sjs?hop=3`;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.speculation_rules.enabled", true],
      ["dom.speculation_rules.same_site_only", false],
      ["dom.security.https_first", false],
    ],
  });
});

add_task(async function test_tags_withheld_from_cross_site_redirect_hop() {
  const hops = [SAME_SITE_HOP_1, CROSS_SITE_HOP_2, SAME_SITE_HOP_3];
  const tagsByHop = new Map();
  const observer = subject => {
    const channel = subject.QueryInterface(Ci.nsIHttpChannel);
    if (!hops.includes(channel.URI.spec)) {
      return;
    }
    let tags = null;
    try {
      tags = channel.getRequestHeader("Sec-Speculation-Tags");
    } catch {}
    tagsByHop.set(channel.URI.spec, tags);
  };
  Services.obs.addObserver(observer, "http-on-modify-request");
  registerCleanupFunction(() =>
    Services.obs.removeObserver(observer, "http-on-modify-request")
  );

  const params = new URLSearchParams({ target: SAME_SITE_HOP_1, tag: "t" });
  const tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: `https://example.com${PATH}file_pageload_prefetch_source.html?${params}`,
    waitForLoad: true,
  });

  await TestUtils.waitForCondition(
    () => tagsByHop.size === hops.length,
    "Every hop of the prefetch redirect chain was requested"
  );

  Assert.equal(
    tagsByHop.get(SAME_SITE_HOP_1),
    '"t"',
    "Tags are sent to the initial same-site URL."
  );
  Assert.equal(
    tagsByHop.get(CROSS_SITE_HOP_2),
    null,
    "Tags are withheld from the cross-site hop."
  );
  Assert.equal(
    tagsByHop.get(SAME_SITE_HOP_3),
    '"t"',
    "Tags are restored on the hop back to the document's site."
  );

  BrowserTestUtils.removeTab(tab);
});

add_task(async function test_tags_withheld_from_cross_scheme_prefetch() {
  const target = `https://example.com${PATH}file_speculation_tags_redirect.sjs?cross-scheme=${Date.now()}`;
  const observed = TestUtils.topicObserved(
    "http-on-modify-request",
    subject => {
      const channel = subject.QueryInterface(Ci.nsIHttpChannel);
      return channel.URI.spec === target;
    }
  );
  const params = new URLSearchParams({ target, tag: "t" });
  await BrowserTestUtils.withNewTab(
    // eslint-disable-next-line sdl/no-insecure-url
    `http://example.com${PATH}file_pageload_prefetch_source.html?${params}`,
    async browser => {
      Assert.equal(browser.currentURI.scheme, "http", "The source uses HTTP.");
      const [subject] = await observed;
      const channel = subject.QueryInterface(Ci.nsIHttpChannel);
      Assert.throws(
        () => channel.getRequestHeader("Sec-Speculation-Tags"),
        /NS_ERROR_NOT_AVAILABLE/,
        "Tags are withheld when the document and target have different schemes."
      );
    }
  );
});
