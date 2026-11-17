/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/** @type {import("../MLTestUtils.sys.mjs")} */
const { MLTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/MLTestUtils.sys.mjs"
);

const { Progress } = ChromeUtils.importESModule(
  "chrome://global/content/ml/Utils.sys.mjs"
);

const OWNER_ICON_ROOT = "modelOwners";

/**
 * getOwnerIcon only knows how to fetch an icon for a handful of hub
 * hostnames (see ModelOwner.getIcon). "mochitests" is special-cased to the
 * bundled mozilla-logo.webp chrome:// resource, so this exercises the real
 * OPFS-backed icon cache without any network dependency.
 */
add_task(async function test_getOwnerIcon_caches_icon_to_opfs() {
  const { sinon } = ChromeUtils.importESModule(
    "resource://testing-common/Sinon.sys.mjs"
  );
  const fullyQualifiedModelName = `mochitests/mozilla/distilvit-${crypto.randomUUID()}`;
  const iconPath = `${OWNER_ICON_ROOT}/mochitests/mozilla/icon`;

  const hub = MLTestUtils.createMockModelHub();
  const modelHub = new ModelHub({
    rootUrl: hub.rootUrl,
    urlTemplate: hub.urlTemplate,
  });

  const fetchUrlSpy = sinon.spy(Progress, "fetchUrl");
  try {
    const icon = await modelHub.getOwnerIcon(fullyQualifiedModelName);
    Assert.notEqual(icon, null, "An icon URL should be returned.");
    Assert.equal(
      fetchUrlSpy.callCount,
      1,
      "The first call fetches the icon since nothing is cached yet."
    );

    await SharedOPFS.getFileHandle(iconPath);

    const icon2 = await modelHub.getOwnerIcon(fullyQualifiedModelName);
    Assert.notEqual(icon2, null, "The cached icon should still be readable.");
    Assert.equal(
      fetchUrlSpy.callCount,
      1,
      "The second call is served from the OPFS cache without fetching again."
    );
  } finally {
    fetchUrlSpy.restore();
  }

  await SharedOPFS.remove(OWNER_ICON_ROOT, {
    recursive: true,
    ignoreErrors: true,
  });
  await hub.cleanup();
});

/**
 * Hubs we don't know how to fetch icons from should resolve to null rather
 * than throwing.
 */
add_task(async function test_getOwnerIcon_unknown_hub_returns_null() {
  const hub = MLTestUtils.createMockModelHub();
  const modelHub = new ModelHub({
    rootUrl: hub.rootUrl,
    urlTemplate: hub.urlTemplate,
  });

  const icon = await modelHub.getOwnerIcon("example.com/acme/bert");
  Assert.equal(
    icon,
    null,
    "An unrecognized hub hostname should not produce an icon."
  );

  await hub.cleanup();
});
