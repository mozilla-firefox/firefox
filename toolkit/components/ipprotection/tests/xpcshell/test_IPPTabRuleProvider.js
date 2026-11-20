/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { IPPTabRuleProvider } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPSiteRuleProviders.sys.mjs"
);

const makePrincipal = url =>
  Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(url),
    {}
  );

const TAB = 1;
const OTHER_TAB = 2;

/**
 * Tells the provider that a tab's top browsing context went away.
 *
 * @param {IPPTabRuleProvider} provider
 * @param {number} browserId
 * @param {string} why - "discard" when the tab closes, "replace" otherwise.
 */
function discardTab(provider, browserId, why) {
  provider.observe(
    { parent: null, browserId },
    "browsing-context-discarded",
    why
  );
}

/**
 * Tests that a tab rule covers every site in its tab and nowhere else, and
 * that it is counted.
 */
add_task(function test_tab_rule_covers_only_its_tab() {
  const provider = new IPPTabRuleProvider();
  provider.init();
  provider.setTabRule(IPPPrincipalRules.INCLUDED, TAB);

  Assert.equal(
    provider.getRule(makePrincipal("https://example.com/"), TAB),
    IPPPrincipalRules.INCLUDED,
    "A site in the tab should follow the tab rule"
  );
  Assert.equal(
    provider.getRule(makePrincipal("https://example.org/"), TAB),
    IPPPrincipalRules.INCLUDED,
    "Any other site in the tab should too"
  );
  Assert.strictEqual(
    provider.getRule(makePrincipal("https://example.com/"), OTHER_TAB),
    null,
    "Another tab should be unaffected"
  );
  Assert.equal(provider.count(IPPPrincipalRules.INCLUDED), 1);

  provider.uninit();
});

/**
 * Tests that a rule set with removeOnLeave only covers its site in its tab.
 */
add_task(function test_remove_on_leave_rule_covers_only_its_site() {
  const provider = new IPPTabRuleProvider();
  provider.init();
  provider.setTabRule(IPPPrincipalRules.EXCLUDED, TAB, {
    principal: makePrincipal("https://example.com/page"),
    removeOnLeave: true,
  });

  Assert.equal(
    provider.getRule(makePrincipal("https://www.example.com/"), TAB),
    IPPPrincipalRules.EXCLUDED,
    "The site in the tab should follow the tab rule"
  );
  Assert.strictEqual(
    provider.getRule(makePrincipal("https://example.org/"), TAB),
    null,
    "Another site in the tab should be unaffected"
  );

  provider.uninit();
});

/**
 * Tests that DEFAULT removes a tab rule, and that only an actual change
 * notifies.
 */
add_task(function test_default_removes_the_tab_rule() {
  const provider = new IPPTabRuleProvider();
  provider.init();
  const site = makePrincipal("https://example.com/");
  provider.setTabRule(IPPPrincipalRules.EXCLUDED, TAB);

  let changes = 0;
  provider.addEventListener("change", () => changes++);
  provider.setTabRule(IPPPrincipalRules.DEFAULT, TAB);
  provider.setTabRule(IPPPrincipalRules.DEFAULT, TAB);

  Assert.strictEqual(provider.getRule(site, TAB), null, "The rule is gone");
  Assert.equal(changes, 1, "Only an actual change should notify");

  provider.uninit();
});

/**
 * Tests that closing the tab drops its rule, while a browsing context
 * replacement within the same tab keeps it.
 */
add_task(function test_cleared_when_the_tab_closes() {
  const provider = new IPPTabRuleProvider();
  provider.init();
  const principal = makePrincipal("https://example.com/");
  provider.setTabRule(IPPPrincipalRules.EXCLUDED, TAB);

  discardTab(provider, TAB, "replace");
  Assert.equal(
    provider.getRule(principal, TAB),
    IPPPrincipalRules.EXCLUDED,
    "A replacement should keep the rule"
  );

  let changed = false;
  provider.addEventListener("change", () => (changed = true));
  discardTab(provider, TAB, "discard");
  Assert.strictEqual(
    provider.getRule(principal, TAB),
    null,
    "Closing the tab should drop the rule"
  );
  Assert.ok(changed, "Dropping it should notify a change");

  provider.uninit();
});
