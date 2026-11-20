/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { IPPTabRuleProvider } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPSiteRuleProviders.sys.mjs"
);

const SITE = "https://example.com/";
const OTHER_SITE = "https://example.org/";

const makePrincipal = url =>
  Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(url),
    {}
  );

async function navigate(browser, url) {
  const loaded = BrowserTestUtils.browserLoaded(browser, false, url);
  BrowserTestUtils.startLoadingURIString(browser, url);
  await loaded;
}

/**
 * Tests that a removeOnLeave rule survives navigations within its site and
 * is removed by the first top-level navigation to another site.
 */
add_task(async function test_remove_on_leave_rule_removed_off_site() {
  const provider = new IPPTabRuleProvider();
  provider.init();
  await BrowserTestUtils.withNewTab(SITE, async browser => {
    provider.setTabRule(IPPPrincipalRules.EXCLUDED, browser.browserId, {
      principal: makePrincipal(SITE),
      removeOnLeave: true,
    });

    await navigate(browser, "https://www.example.com/");
    Assert.equal(
      provider.getRule(makePrincipal(SITE), browser.browserId),
      IPPPrincipalRules.EXCLUDED,
      "A navigation within the site should keep the rule"
    );

    let changed = false;
    provider.addEventListener("change", () => (changed = true));
    await navigate(browser, OTHER_SITE);
    Assert.strictEqual(
      provider.getRule(makePrincipal(SITE), browser.browserId),
      null,
      "Leaving the site should remove the rule"
    );
    Assert.ok(changed, "Removing it should notify a change");
  });
  provider.uninit();
});
