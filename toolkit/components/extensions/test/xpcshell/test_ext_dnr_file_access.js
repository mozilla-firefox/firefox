"use strict";

add_setup(() => {
  Services.prefs.setBoolPref("extensions.dnr.feedback", true);
});

async function test_dnr_with_file_initiator(hasFilePermissions) {
  const host_permissions = ["http://example.com/*"];
  if (hasFilePermissions) {
    host_permissions.push("file:///*");
  }
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["declarativeNetRequest", "declarativeNetRequestFeedback"],
      host_permissions,
    },
    background: async () => {
      const hasFilePermissions = await browser.permissions.contains({
        origins: ["file:///*"],
      });
      const dnr = browser.declarativeNetRequest;
      await dnr.updateSessionRules({
        addRules: [
          {
            id: 1,
            condition: { excludedResourceTypes: [] },
            // "allow" action does not require permissions because of the
            // "declarativeNetRequest" permission, which enables actions such
            // as "block" and "allow" without host permissions.
            action: { type: "allow" },
          },
          {
            id: 2,
            priority: 2,
            condition: { excludedResourceTypes: [] },
            // "modifyHeaders" action requires host permissions to the request
            // URL and the initiator (with exceptions, see below).
            action: {
              type: "modifyHeaders",
              responseHeaders: [{ operation: "set", header: "x", value: "y" }],
            },
          },
        ],
      });

      async function testMatchesRequest(request, ruleIds, description) {
        browser.test.assertDeepEq(
          ruleIds,
          (await dnr.testMatchOutcome(request)).matchedRules.map(m => m.ruleId),
          description
        );
      }

      const url = "http://example.com/";
      if (hasFilePermissions) {
        await testMatchesRequest(
          { url, initiator: "file:///tmp", type: "other" },
          [1, 2],
          "Can modify requests from file:-initiator, with permission"
        );
      } else {
        await testMatchesRequest(
          { url, initiator: "file:///tmp", type: "other" },
          [1],
          "Cannot modify requests from file:-initiator, without permission"
        );
      }

      // main_frame/sub_frame are exempt from initiator checks by design, for
      // context see: https://bugzilla.mozilla.org/show_bug.cgi?id=1825824#c2
      await testMatchesRequest(
        { url, initiator: "file:///tmp", type: "main_frame" },
        [1, 2],
        "Can match requests from file:-initiator for main_frame requests"
      );
      await testMatchesRequest(
        { url, initiator: "file:///tmp", type: "sub_frame" },
        [1, 2],
        "Can match requests from file:-initiator for sub_frame requests"
      );

      // Regardless of initiator, file:-requests themselves cannot be matched by
      // DNR, both by design and in its implementation, which is only hooked up
      // to http(s) requests via NetworkIntegration.startDNREvaluation.
      await testMatchesRequest(
        { url: "file:///", initiator: "file:///tmp", type: "sub_frame" },
        [],
        "Cannot match file:-requests from file."
      );
      await testMatchesRequest(
        { url: "file:///", type: "sub_frame" },
        [],
        "Cannot match file:-requests from file."
      );
      browser.test.notifyPass();
    },
  });
  await extension.startup();
  await extension.awaitFinish();
  await extension.unload();
}

add_task(async function dnr_without_file_permissions() {
  await test_dnr_with_file_initiator(/* hasFilePermissions */ false);
});

add_task(async function dnr_with_file_permissions() {
  await test_dnr_with_file_initiator(/* hasFilePermissions */ true);
});
