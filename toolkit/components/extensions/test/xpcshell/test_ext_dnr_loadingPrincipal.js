"use strict";

// DNR access checks use the loading principal, i.e. the document that the
// request belongs to, and not the triggering principal. They are often the
// same, but CSS @import is an example where the two differ:
// - the loading principal is the page that loads the parent stylesheet.
// - the triggering principal is the parent stylesheet with @import.
// - the request URL is the @import-ed stylesheet.
//
// test_ext_dnr_system_restrictions.js has additional coverage:
// - dnr_ignores_css_import_at_restrictedDomains checks enforcement of
//   restricted domains in CSS @import.
// - dnr_main_frame_not_affected_by_restricted_initiator shows that a loading
//   principal is used to check restrictions (null for main_frame) instead of
//   the triggering principal (=whatever triggered the navigation).

const server = createHttpServer({
  hosts: ["page.example", "style.example", "import.example"],
});

server.registerPathHandler("/page.html", (req, res) => {
  res.setHeader("Content-Type", "text/html");
  res.write(`<link rel="stylesheet" href="http://style.example/parent.css">`);
});
server.registerPathHandler("/parent.css", (req, res) => {
  res.setHeader("Content-Type", "text/css");
  res.write(`
    @import url('http://import.example/child.css');
    :root { --style-status: dnr_blocked_style; }
  `);
});
server.registerPathHandler("/child.css", (req, res) => {
  res.setHeader("Content-Type", "text/css");
  res.write("body { --style-status: dnr_not_matched }");
});

async function checkIsStylesheetBlocked(url) {
  let contentPage = await ExtensionTestUtils.loadContentPage(url);

  // Likely not needed, but clear cache just in case:
  ChromeUtils.clearResourceCache({
    target: "content",
    types: ["stylesheet"],
  });

  let result = await contentPage.spawn([], () => {
    const { document } = content;
    let style = content.getComputedStyle(document.body);
    return style.getPropertyValue("--style-status");
  });
  await contentPage.close();
  if (result !== "dnr_blocked_style" && result !== "dnr_not_matched") {
    throw new Error(`Unexpected --style-status: ${result}`);
  }
  return result;
}

async function startDNRExtension(host_permissions) {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      manifest_version: 3,
      // Note: using the DNR permission that requires host permissions.
      permissions: ["declarativeNetRequestWithHostAccess"],
      host_permissions,
      granted_host_permissions: true,
    },
    temporarilyInstalled: true, // <-- for granted_host_permissions
    async background() {
      await browser.declarativeNetRequest.updateSessionRules({
        addRules: [
          {
            id: 1,
            condition: { urlFilter: "child.css" },
            action: { type: "block" },
          },
        ],
      });
      browser.test.sendMessage("dnr_registered");
    },
  });
  await extension.startup();
  await extension.awaitMessage("dnr_registered");
  return extension;
}

add_task(async function css_import_blocked_with_document_host_permission() {
  let extension = await startDNRExtension([
    "*://page.example/*", // loading principal.
    // "*://style.example/*", // triggering principal.
    "*://import.example/*", // requested URL.
  ]);

  // With permissions for the loading principal and requested URL, DNR applies.
  Assert.equal(
    await checkIsStylesheetBlocked("http://page.example/page.html"),
    "dnr_blocked_style",
    "Blocked by DNR: the extension has host permissions for the document"
  );

  await extension.unload();
});

add_task(async function css_import_allowed_without_document_host_permission() {
  let extension = await startDNRExtension([
    //"*://page.example/*", // loading principal.
    "*://style.example/*", // triggering principal.
    "*://import.example/*", // requested URL.
  ]);

  // Lack of permission for loading principal results in DNR not applying.
  Assert.equal(
    await checkIsStylesheetBlocked("http://page.example/page.html"),
    "dnr_not_matched",
    "Not blocked by DNR: the extension lacks host permissions for the document"
  );

  await extension.unload();
});
