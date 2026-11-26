/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SITE_PERMISSIONS_URL =
  "chrome://browser/content/preferences/dialogs/sitePermissions.xhtml";
const PERMISSIONS_URL =
  "chrome://browser/content/preferences/dialogs/permissions.xhtml";

const ORIGIN = "https://www.example.com";
const USER_CONTEXT_ID = 1;

// Set in add_setup()
let gIdentity;
let gContainerLabel;

function addPermission(type, capability, userContextId, origin = ORIGIN) {
  let principal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(origin),
    { userContextId }
  );
  Services.perms.addFromPrincipal(principal, type, capability);
  return principal;
}

/**
 * Opens a permission dialog and waits for it to have listed `rowCount`
 * permissions.
 *
 * @param {string} url The dialog to open.
 * @param {object} params The dialog arguments.
 * @param {number} rowCount The number of permissions the dialog should list.
 * @returns {Document} The dialog document.
 */
async function openDialog(url, params, rowCount) {
  let dialogWin = await openAndLoadSubDialog(url, null, params);
  let doc = dialogWin.document;
  await doc.mozSubdialogReady;

  let list = doc.getElementById("permissionsBox");
  await BrowserTestUtils.waitForMutationCondition(
    list,
    { childList: true, subtree: true },
    () => list.itemCount === rowCount
  );
  return doc;
}

/**
 * Clicks a button of a permission dialog and waits for the dialog to be gone.
 *
 * @param {Document} doc The dialog document.
 * @param {string} button The button to close the dialog with.
 */
async function closeDialog(doc, button = "cancel") {
  let dialogClosed = BrowserTestUtils.waitForEvent(
    content.gSubDialog._dialogStack,
    "dialogclose"
  );
  doc.querySelector("dialog").getButton(button).click();
  await dialogClosed;
}

function rowFor(doc, principal) {
  return doc.getElementsByAttribute("origin", principal.origin)[0];
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["permissions.isolateBy.userContext", true],
      // Keep the sync pane from persisting a device name behind our back.
      ["identity.fxaccounts.account.device.name", ""],
    ],
  });
  gIdentity =
    ContextualIdentityService.getPublicIdentityFromId(USER_CONTEXT_ID);
  gContainerLabel =
    ContextualIdentityService.getUserContextLabel(USER_CONTEXT_ID);
  Assert.ok(gIdentity, "The container used by this test exists");

  let tab = await openPrefsTab();
  registerCleanupFunction(() => {
    BrowserTestUtils.removeTab(tab);
    Services.perms.removeByType("desktop-notification");
    Services.perms.removeByType("popup");
  });
});

/**
 * The site permissions dialog lists a container permission next to the
 * permission of the default container, showing the container in a column of its
 * own instead of leaking the userContextId into the website column.
 */
add_task(async function containerColumnInSitePermissionsDialog() {
  Services.perms.removeByType("desktop-notification");
  let defaultPrincipal = addPermission(
    "desktop-notification",
    Services.perms.ALLOW_ACTION,
    0
  );
  let containerPrincipal = addPermission(
    "desktop-notification",
    Services.perms.DENY_ACTION,
    USER_CONTEXT_ID
  );

  Assert.notEqual(
    defaultPrincipal.origin,
    containerPrincipal.origin,
    "The container permission is stored separately"
  );

  let doc = await openDialog(
    SITE_PERMISSIONS_URL,
    { permissionType: "desktop-notification" },
    2
  );

  Assert.ok(
    BrowserTestUtils.isVisible(doc.getElementById("containerCol")),
    "The container column is shown"
  );

  let defaultRow = rowFor(doc, defaultPrincipal);
  Assert.equal(
    defaultRow.querySelector(".website-name").textContent,
    ORIGIN,
    "The default container row shows the origin"
  );
  Assert.ok(
    !defaultRow.querySelector(".container-icon"),
    "The default container row has no container icon"
  );

  let containerRow = rowFor(doc, containerPrincipal);
  Assert.equal(
    containerRow.querySelector(".website-name").textContent,
    ORIGIN,
    "The container row shows the origin without the userContextId"
  );
  let pill = containerRow.querySelector(".container-pill");
  Assert.equal(
    pill.querySelector(".container-icon").getAttribute("src"),
    ContextualIdentityService.getContainerIconURL(gIdentity.icon),
    "The container row shows the container icon"
  );
  Assert.ok(
    pill.classList.contains(`identity-color-${gIdentity.color}`),
    "The pill carries the container color"
  );
  Assert.equal(
    pill.querySelector(".container-name").textContent,
    gContainerLabel,
    "The pill shows the container name next to the icon"
  );
  Assert.equal(
    pill.getAttribute("tooltiptext"),
    gContainerLabel,
    "The pill is labelled with the container name"
  );

  await closeDialog(doc);
});

/**
 * The search box matches the origin without the userContextId, and the
 * container name.
 */
add_task(async function searchContainerPermission() {
  Services.perms.removeByType("desktop-notification");
  addPermission("desktop-notification", Services.perms.ALLOW_ACTION, 0);
  let containerPrincipal = addPermission(
    "desktop-notification",
    Services.perms.DENY_ACTION,
    USER_CONTEXT_ID
  );

  let doc = await openDialog(
    SITE_PERMISSIONS_URL,
    { permissionType: "desktop-notification" },
    2
  );
  let list = doc.getElementById("permissionsBox");
  let searchBox = doc.getElementById("searchBox");

  searchBox.value = "usercontextid";
  searchBox.dispatchEvent(new CustomEvent("MozInputSearch:search"));
  Assert.equal(
    list.itemCount,
    0,
    "The userContextId is not part of the searched origin"
  );

  searchBox.value = gContainerLabel.toLowerCase();
  searchBox.dispatchEvent(new CustomEvent("MozInputSearch:search"));
  Assert.equal(list.itemCount, 1, "Only the container permission matches");
  Assert.equal(
    list.itemChildren[0].getAttribute("origin"),
    containerPrincipal.origin,
    "The container permission was found by container name"
  );

  await closeDialog(doc);
});

/**
 * Only the container permission is removed when its row is removed.
 */
add_task(async function removeContainerPermission() {
  Services.perms.removeByType("desktop-notification");
  let defaultPrincipal = addPermission(
    "desktop-notification",
    Services.perms.ALLOW_ACTION,
    0
  );
  let containerPrincipal = addPermission(
    "desktop-notification",
    Services.perms.ALLOW_ACTION,
    USER_CONTEXT_ID
  );

  let doc = await openDialog(
    SITE_PERMISSIONS_URL,
    { permissionType: "desktop-notification" },
    2
  );

  doc.getElementById("permissionsBox").selectedItem = rowFor(
    doc,
    containerPrincipal
  );
  doc.getElementById("removePermission").click();
  await closeDialog(doc, "accept");

  Assert.equal(
    Services.perms.testExactPermissionFromPrincipal(
      defaultPrincipal,
      "desktop-notification"
    ),
    Services.perms.ALLOW_ACTION,
    "The default container permission is untouched"
  );
  Assert.equal(
    Services.perms.testExactPermissionFromPrincipal(
      containerPrincipal,
      "desktop-notification"
    ),
    Services.perms.UNKNOWN_ACTION,
    "The container permission was removed"
  );
});

/**
 * A permission of a container that does not exist anymore falls back to a
 * generic icon, so that it stays distinguishable from a permission of the
 * default container.
 */
add_task(async function unknownContainerFallsBackToGenericIcon() {
  Services.perms.removeByType("desktop-notification");
  const UNKNOWN_USER_CONTEXT_ID = 99999;
  Assert.ok(
    !ContextualIdentityService.getPublicIdentityFromId(UNKNOWN_USER_CONTEXT_ID),
    "The container used by this test does not exist"
  );

  let defaultPrincipal = addPermission(
    "desktop-notification",
    Services.perms.ALLOW_ACTION,
    0
  );
  let unknownPrincipal = addPermission(
    "desktop-notification",
    Services.perms.DENY_ACTION,
    UNKNOWN_USER_CONTEXT_ID
  );

  let doc = await openDialog(
    SITE_PERMISSIONS_URL,
    { permissionType: "desktop-notification" },
    2
  );

  Assert.ok(
    BrowserTestUtils.isVisible(doc.getElementById("containerCol")),
    "The container column is shown for an unknown container"
  );
  Assert.ok(
    !rowFor(doc, defaultPrincipal).querySelector(".container-icon"),
    "The default container row still has no container icon"
  );

  let row = rowFor(doc, unknownPrincipal);
  let pill = row.querySelector(".container-pill");
  Assert.ok(
    pill.classList.contains("container-pill-unknown"),
    "The unknown container row shows the generic pill"
  );
  Assert.equal(
    pill.querySelector(".container-icon").getAttribute("src"),
    "chrome://global/skin/icons/help.svg",
    "The generic icon is used as the icon source"
  );
  Assert.equal(
    pill.querySelector(".container-name").textContent,
    `#${UNKNOWN_USER_CONTEXT_ID}`,
    "The pill falls back to the bare userContextId as the container name"
  );
  Assert.equal(
    pill.getAttribute("data-l10n-id"),
    "permissions-container-unknown",
    "The generic pill is localized"
  );

  await BrowserTestUtils.waitForMutationCondition(
    pill,
    { attributes: true },
    () => pill.hasAttribute("tooltiptext")
  );
  Assert.ok(pill.getAttribute("tooltiptext"), "The generic pill has a tooltip");

  Assert.equal(
    row.getAttribute("containerlabel"),
    `#${UNKNOWN_USER_CONTEXT_ID}`,
    "The unknown container does not sort with the default container"
  );
  Assert.equal(
    rowFor(doc, defaultPrincipal).getAttribute("containerlabel"),
    "",
    "The default container has no container label"
  );

  await closeDialog(doc);
});

/**
 * The container column is not shown when there are no container permissions
 */
add_task(async function containerColumnHiddenWithoutContainers() {
  Services.perms.removeByType("desktop-notification");
  addPermission("desktop-notification", Services.perms.ALLOW_ACTION, 0);

  let doc = await openDialog(
    SITE_PERMISSIONS_URL,
    { permissionType: "desktop-notification" },
    1
  );

  Assert.ok(
    BrowserTestUtils.isHidden(doc.getElementById("containerCol")),
    "The container column is hidden"
  );
  Assert.ok(
    !doc.querySelector(".website-container"),
    "No row has a container cell"
  );

  await closeDialog(doc);
});

/**
 * The exceptions dialog presents container permissions the same way with
 * a container column
 */
add_task(async function containerColumnInExceptionsDialog() {
  Services.perms.removeByType("popup");
  let defaultPrincipal = addPermission("popup", Services.perms.ALLOW_ACTION, 0);
  let containerPrincipal = addPermission(
    "popup",
    Services.perms.DENY_ACTION,
    USER_CONTEXT_ID
  );

  let doc = await openDialog(
    PERMISSIONS_URL,
    {
      blockVisible: true,
      sessionVisible: false,
      allowVisible: true,
      prefilledHost: "",
      permissionType: "popup",
    },
    2
  );

  Assert.ok(
    BrowserTestUtils.isVisible(doc.getElementById("containerCol")),
    "The container column is shown"
  );

  for (let principal of [defaultPrincipal, containerPrincipal]) {
    Assert.equal(
      rowFor(doc, principal).querySelector(".website-name-value").value,
      ORIGIN,
      `${principal.origin} is shown without its userContextId`
    );
  }

  Assert.ok(
    !rowFor(doc, defaultPrincipal).querySelector(".container-icon"),
    "The default container row has no container icon"
  );
  Assert.equal(
    rowFor(doc, containerPrincipal)
      .querySelector(".container-icon")
      .getAttribute("src"),
    ContextualIdentityService.getContainerIconURL(gIdentity.icon),
    "The container row shows the container icon"
  );

  await closeDialog(doc);
});

/**
 * The exceptions dialog has no container column when there are no container permissions
 */
add_task(async function exceptionsContainerColumnHiddenWithoutContainers() {
  Services.perms.removeByType("popup");
  addPermission("popup", Services.perms.ALLOW_ACTION, 0);

  let doc = await openDialog(
    PERMISSIONS_URL,
    {
      blockVisible: true,
      sessionVisible: false,
      allowVisible: true,
      prefilledHost: "",
      permissionType: "popup",
    },
    1
  );

  Assert.ok(
    BrowserTestUtils.isHidden(doc.getElementById("containerCol")),
    "The container column is hidden"
  );
  Assert.ok(
    !doc.querySelector(".website-container"),
    "No row has a container cell"
  );

  await closeDialog(doc);
});

const OTHER_USER_CONTEXT_ID = 2;
const COLLATOR = new Services.intl.Collator(undefined, { usage: "sort" });

// Rows as [displayorigin, containerlabel] pairs, in list order.
function rowKeys(doc) {
  return Array.from(doc.getElementById("permissionsBox").itemChildren, item => [
    item.getAttribute("displayorigin"),
    item.getAttribute("containerlabel"),
  ]);
}

// The order the dialog is expected to produce for the column sorted by the
// `primary` key, which breaks ties on the other key.
function sortedByKey(keys, primary) {
  let secondary = primary == 0 ? 1 : 0;
  return [...keys].sort(
    (a, b) =>
      COLLATOR.compare(a[primary], b[primary]) ||
      COLLATOR.compare(a[secondary], b[secondary])
  );
}

function clickColumn(doc, column) {
  EventUtils.synthesizeMouseAtCenter(column, {}, doc.defaultView);
}

function assertSortedBy(doc, columnId, primary) {
  let column = doc.getElementById(columnId);
  let expected = sortedByKey(rowKeys(doc), primary);

  clickColumn(doc, column);
  Assert.deepEqual(
    rowKeys(doc),
    expected,
    `${columnId} sorts ascending by its own column first`
  );
  Assert.equal(
    column.getAttribute("sortDirection"),
    "ascending",
    `${columnId} is marked as sorted ascending`
  );

  clickColumn(doc, column);
  Assert.deepEqual(
    rowKeys(doc),
    expected.reverse(),
    `${columnId} sorts descending on a second click`
  );
  Assert.equal(
    column.getAttribute("sortDirection"),
    "descending",
    `${columnId} is marked as sorted descending`
  );

  for (let other of ["siteCol", "statusCol", "containerCol"]) {
    if (other != columnId) {
      Assert.ok(
        !doc.getElementById(other).hasAttribute("sortDirection"),
        `${other} is no longer marked as the sorted column`
      );
    }
  }
}

/**
 * The website and the container column sort by their own key first and break
 * ties on the other one, so that the rows of one origin isolated in several
 * containers stay grouped and ordered.
 */
async function testSortsByContainer(url, type, params) {
  Services.perms.removeByType(type);
  const ORIGIN_A = "https://a.example.com";
  const ORIGIN_B = "https://b.example.com";

  Assert.ok(
    ContextualIdentityService.getPublicIdentityFromId(OTHER_USER_CONTEXT_ID),
    "The second container used by this test exists"
  );

  addPermission(type, Services.perms.ALLOW_ACTION, USER_CONTEXT_ID, ORIGIN_A);
  addPermission(
    type,
    Services.perms.ALLOW_ACTION,
    OTHER_USER_CONTEXT_ID,
    ORIGIN_A
  );
  addPermission(type, Services.perms.ALLOW_ACTION, 0, ORIGIN_B);
  addPermission(type, Services.perms.DENY_ACTION, USER_CONTEXT_ID, ORIGIN_B);

  let doc = await openDialog(url, { ...params, permissionType: type }, 4);

  let keys = rowKeys(doc);
  Assert.notDeepEqual(
    sortedByKey(keys, 0),
    sortedByKey(keys, 1),
    "The two columns are expected to order these permissions differently"
  );

  assertSortedBy(doc, "siteCol", 0);
  assertSortedBy(doc, "containerCol", 1);

  await closeDialog(doc);
}

add_task(async function sortsByContainerInSitePermissionsDialog() {
  await testSortsByContainer(SITE_PERMISSIONS_URL, "desktop-notification", {});
});

add_task(async function sortsByContainerInExceptionsDialog() {
  await testSortsByContainer(PERMISSIONS_URL, "popup", {
    blockVisible: true,
    sessionVisible: false,
    allowVisible: true,
    prefilledHost: "",
  });
});
