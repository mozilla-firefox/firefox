/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const USER_CONTEXT_ID = 1;

/**
 * Never-translate-site is a property of the site, not of the container the user
 * happened to be in when they set it, so the permission is stored and read with
 * the userContextId stripped, even when permissions are isolated by container.
 */
add_task(async function test_never_translate_site_ignores_user_context() {
  const { cleanup } = await loadTestPage({
    page: SPANISH_PAGE_URL,
    languagePairs: LANGUAGE_PAIRS,
    prefs: [["permissions.isolateBy.userContext", true]],
  });

  const origin = new URL(SPANISH_PAGE_URL).origin;
  const defaultActor = getTranslationsParent();

  info("Opening the same page in a container tab");
  const containerTab = BrowserTestUtils.addTab(gBrowser, SPANISH_PAGE_URL, {
    userContextId: USER_CONTEXT_ID,
  });
  await BrowserTestUtils.browserLoaded(containerTab.linkedBrowser);
  const containerActor = TranslationsParent.getTranslationsActor(
    containerTab.linkedBrowser
  );

  Assert.ok(
    containerActor.setNeverTranslateSitePermissions(true),
    "Never-translate-site is enabled from within the container"
  );

  Assert.deepEqual(
    TranslationsParent.listNeverTranslateSites(),
    [origin],
    "The exception is stored for the site alone, without the container"
  );
  Assert.ok(
    containerActor.shouldNeverTranslateSite(),
    "The site is never translated within the container"
  );
  Assert.ok(
    defaultActor.shouldNeverTranslateSite(),
    "The site is never translated outside of the container"
  );

  info("Removing the exception from outside of the container");
  TranslationsParent.setNeverTranslateSiteByOrigin(false, origin);

  Assert.deepEqual(
    TranslationsParent.listNeverTranslateSites(),
    [],
    "The exception is gone"
  );
  Assert.ok(
    !containerActor.shouldNeverTranslateSite(),
    "The site is translatable within the container again"
  );
  Assert.ok(
    !defaultActor.shouldNeverTranslateSite(),
    "The site is translatable outside of the container again"
  );

  BrowserTestUtils.removeTab(containerTab);
  await cleanup();
});
