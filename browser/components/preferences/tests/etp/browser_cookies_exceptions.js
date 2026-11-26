/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

requestLongerTimeout(3);

async function clickPermissionButton(button, urlField) {
  if (urlField) {
    urlField.dispatchEvent(new Event("input", { bubbles: true }));
  }
  await button.updateComplete;
  button.click();
}

add_task(async function testAllow() {
  await runTest(
    async (params, observeAllPromise, apply) => {
      assertListContents(params, []);

      params.url.value = "test.com";
      await clickPermissionButton(params.btnAllow, params.url);

      assertListContents(params, [
        ["http://test.com", params.allowL10nId],
        ["https://test.com", params.allowL10nId],
      ]);

      apply();
      await observeAllPromise;
    },
    () => {
      return [
        {
          type: "cookie",
          origin: "http://test.com",
          data: "added",
          capability: Ci.nsIPermissionManager.ALLOW_ACTION,
        },
        {
          type: "cookie",
          origin: "https://test.com",
          data: "added",
          capability: Ci.nsIPermissionManager.ALLOW_ACTION,
        },
      ];
    }
  );
});

add_task(async function testBlock() {
  await runTest(
    async (params, observeAllPromise, apply) => {
      params.url.value = "test.com";
      await clickPermissionButton(params.btnBlock, params.url);

      assertListContents(params, [
        ["http://test.com", params.denyL10nId],
        ["https://test.com", params.denyL10nId],
      ]);

      apply();
      await observeAllPromise;
    },
    () => {
      return [
        {
          type: "cookie",
          origin: "http://test.com",
          data: "changed",
          capability: Ci.nsIPermissionManager.DENY_ACTION,
        },
        {
          type: "cookie",
          origin: "https://test.com",
          data: "changed",
          capability: Ci.nsIPermissionManager.DENY_ACTION,
        },
      ];
    }
  );
});

add_task(async function testAllowAgain() {
  await runTest(
    async (params, observeAllPromise, apply) => {
      params.url.value = "test.com";
      await clickPermissionButton(params.btnAllow, params.url);

      assertListContents(params, [
        ["http://test.com", params.allowL10nId],
        ["https://test.com", params.allowL10nId],
      ]);

      apply();
      await observeAllPromise;
    },
    () => {
      return [
        {
          type: "cookie",
          origin: "http://test.com",
          data: "changed",
          capability: Ci.nsIPermissionManager.ALLOW_ACTION,
        },
        {
          type: "cookie",
          origin: "https://test.com",
          data: "changed",
          capability: Ci.nsIPermissionManager.ALLOW_ACTION,
        },
      ];
    }
  );
});

add_task(async function testRemove() {
  await runTest(
    async (params, observeAllPromise, apply) => {
      while (params.richlistbox.itemCount) {
        params.richlistbox.selectedIndex = 0;
        await clickPermissionButton(params.btnRemove);
      }
      assertListContents(params, []);

      apply();
      await observeAllPromise;
    },
    params => {
      let richlistItems = params.richlistbox.getElementsByAttribute(
        "origin",
        "*"
      );
      let observances = [];
      for (let item of richlistItems) {
        observances.push({
          type: "cookie",
          origin: item.getAttribute("origin"),
          data: "deleted",
        });
      }
      return observances;
    }
  );
});

add_task(async function testAdd() {
  await runTest(
    async (params, observeAllPromise, apply) => {
      let uri = Services.io.newURI("http://test.com");
      PermissionTestUtils.add(
        uri,
        "popup",
        Ci.nsIPermissionManager.DENY_ACTION
      );

      info("Adding unrelated permission should not change display.");
      assertListContents(params, []);

      apply();
      await observeAllPromise;

      PermissionTestUtils.remove(uri, "popup");
    },
    () => {
      return [
        {
          type: "popup",
          origin: "http://test.com",
          data: "added",
          capability: Ci.nsIPermissionManager.DENY_ACTION,
        },
      ];
    }
  );
});

add_task(async function testAllowHTTPSWithPort() {
  await runTest(
    async (params, observeAllPromise, apply) => {
      params.url.value = "https://test.com:12345";
      await clickPermissionButton(params.btnAllow, params.url);

      assertListContents(params, [
        ["https://test.com:12345", params.allowL10nId],
      ]);

      apply();
      await observeAllPromise;
    },
    () => {
      return [
        {
          type: "cookie",
          origin: "https://test.com:12345",
          data: "added",
          capability: Ci.nsIPermissionManager.ALLOW_ACTION,
        },
      ];
    }
  );
});

add_task(async function testBlockHTTPSWithPort() {
  await runTest(
    async (params, observeAllPromise, apply) => {
      params.url.value = "https://test.com:12345";
      await clickPermissionButton(params.btnBlock, params.url);

      assertListContents(params, [
        ["https://test.com:12345", params.denyL10nId],
      ]);

      apply();
      await observeAllPromise;
    },
    () => {
      return [
        {
          type: "cookie",
          origin: "https://test.com:12345",
          data: "changed",
          capability: Ci.nsIPermissionManager.DENY_ACTION,
        },
      ];
    }
  );
});

add_task(async function testAllowAgainHTTPSWithPort() {
  await runTest(
    async (params, observeAllPromise, apply) => {
      params.url.value = "https://test.com:12345";
      await clickPermissionButton(params.btnAllow, params.url);

      assertListContents(params, [
        ["https://test.com:12345", params.allowL10nId],
      ]);

      apply();
      await observeAllPromise;
    },
    () => {
      return [
        {
          type: "cookie",
          origin: "https://test.com:12345",
          data: "changed",
          capability: Ci.nsIPermissionManager.ALLOW_ACTION,
        },
      ];
    }
  );
});

add_task(async function testRemoveHTTPSWithPort() {
  await runTest(
    async (params, observeAllPromise, apply) => {
      while (params.richlistbox.itemCount) {
        params.richlistbox.selectedIndex = 0;
        await clickPermissionButton(params.btnRemove);
      }

      assertListContents(params, []);

      apply();
      await observeAllPromise;
    },
    params => {
      let richlistItems = params.richlistbox.getElementsByAttribute(
        "origin",
        "*"
      );
      let observances = [];
      for (let item of richlistItems) {
        observances.push({
          type: "cookie",
          origin: item.getAttribute("origin"),
          data: "deleted",
        });
      }
      return observances;
    }
  );
});

add_task(async function testAllowPort() {
  await runTest(
    async (params, observeAllPromise, apply) => {
      params.url.value = "localhost:12345";
      await clickPermissionButton(params.btnAllow, params.url);

      assertListContents(params, [
        ["http://localhost:12345", params.allowL10nId],
        ["https://localhost:12345", params.allowL10nId],
      ]);

      apply();
      await observeAllPromise;
    },
    () => {
      return [
        {
          type: "cookie",
          origin: "http://localhost:12345",
          data: "added",
          capability: Ci.nsIPermissionManager.ALLOW_ACTION,
        },
        {
          type: "cookie",
          origin: "https://localhost:12345",
          data: "added",
          capability: Ci.nsIPermissionManager.ALLOW_ACTION,
        },
      ];
    }
  );
});

add_task(async function testBlockPort() {
  await runTest(
    async (params, observeAllPromise, apply) => {
      params.url.value = "localhost:12345";
      await clickPermissionButton(params.btnBlock, params.url);

      assertListContents(params, [
        ["http://localhost:12345", params.denyL10nId],
        ["https://localhost:12345", params.denyL10nId],
      ]);

      apply();
      await observeAllPromise;
    },
    () => {
      return [
        {
          type: "cookie",
          origin: "http://localhost:12345",
          data: "changed",
          capability: Ci.nsIPermissionManager.DENY_ACTION,
        },
        {
          type: "cookie",
          origin: "https://localhost:12345",
          data: "changed",
          capability: Ci.nsIPermissionManager.DENY_ACTION,
        },
      ];
    }
  );
});

add_task(async function testAllowAgainPort() {
  await runTest(
    async (params, observeAllPromise, apply) => {
      params.url.value = "localhost:12345";
      await clickPermissionButton(params.btnAllow, params.url);

      assertListContents(params, [
        ["http://localhost:12345", params.allowL10nId],
        ["https://localhost:12345", params.allowL10nId],
      ]);

      apply();
      await observeAllPromise;
    },
    () => {
      return [
        {
          type: "cookie",
          origin: "http://localhost:12345",
          data: "changed",
          capability: Ci.nsIPermissionManager.ALLOW_ACTION,
        },
        {
          type: "cookie",
          origin: "https://localhost:12345",
          data: "changed",
          capability: Ci.nsIPermissionManager.ALLOW_ACTION,
        },
      ];
    }
  );
});

add_task(async function testRemovePort() {
  await runTest(
    async (params, observeAllPromise, apply) => {
      while (params.richlistbox.itemCount) {
        params.richlistbox.selectedIndex = 0;
        await clickPermissionButton(params.btnRemove);
      }

      assertListContents(params, []);

      apply();
      await observeAllPromise;
    },
    params => {
      let richlistItems = params.richlistbox.getElementsByAttribute(
        "origin",
        "*"
      );
      let observances = [];
      for (let item of richlistItems) {
        observances.push({
          type: "cookie",
          origin: item.getAttribute("origin"),
          data: "deleted",
        });
      }
      return observances;
    }
  );
});

add_task(async function testSort() {
  await runTest(
    async (params, observeAllPromise, apply) => {
      // Sort by site name.
      EventUtils.synthesizeMouseAtCenter(
        params.doc.getElementById("siteCol"),
        {},
        params.doc.defaultView
      );

      for (let URL of ["http://a", "http://z", "http://b"]) {
        let URI = Services.io.newURI(URL);
        PermissionTestUtils.add(
          URI,
          "cookie",
          Ci.nsIPermissionManager.ALLOW_ACTION
        );
      }

      assertListContents(params, [
        ["http://a", params.allowL10nId],
        ["http://b", params.allowL10nId],
        ["http://z", params.allowL10nId],
      ]);

      // Sort by site name in descending order.
      EventUtils.synthesizeMouseAtCenter(
        params.doc.getElementById("siteCol"),
        {},
        params.doc.defaultView
      );

      assertListContents(params, [
        ["http://z", params.allowL10nId],
        ["http://b", params.allowL10nId],
        ["http://a", params.allowL10nId],
      ]);

      apply();
      await observeAllPromise;

      for (let URL of ["http://a", "http://z", "http://b"]) {
        let uri = Services.io.newURI(URL);
        PermissionTestUtils.remove(uri, "cookie");
      }
    },
    () => {
      return [
        {
          type: "cookie",
          origin: "http://a",
          data: "added",
          capability: Ci.nsIPermissionManager.ALLOW_ACTION,
        },
        {
          type: "cookie",
          origin: "http://z",
          data: "added",
          capability: Ci.nsIPermissionManager.ALLOW_ACTION,
        },
        {
          type: "cookie",
          origin: "http://b",
          data: "added",
          capability: Ci.nsIPermissionManager.ALLOW_ACTION,
        },
      ];
    }
  );
});

add_task(async function testPrivateBrowsingSessionPermissionsAreShown() {
  await runTest(
    async params => {
      assertListContents(params, []);

      let uri = Services.io.newURI("http://test.com");
      let privateBrowsingPrincipal =
        Services.scriptSecurityManager.createContentPrincipal(uri, {
          privateBrowsingId: 1,
        });

      // The cookie permission is never isolated by private browsing, so a
      // session permission added in private browsing also applies outside of it.
      // See bug 2088398.
      PermissionTestUtils.add(
        privateBrowsingPrincipal,
        "cookie",
        Services.perms.ALLOW_ACTION,
        Services.perms.EXPIRE_SESSION
      );

      assertListContents(params, [["http://test.com", params.allowL10nId]]);

      PermissionTestUtils.remove(uri, "cookie");
    },
    () => {
      return [];
    }
  );
});

function assertListContents(params, expected) {
  Assert.equal(params.richlistbox.itemCount, expected.length);

  for (let i = 0; i < expected.length; i++) {
    let website = expected[i][0];
    let elements = params.richlistbox.getElementsByAttribute("origin", website);
    Assert.equal(elements.length, 1); // "It should find only one coincidence"
    Assert.equal(
      elements[0]
        .querySelector(".website-capability-value")
        .getAttribute("data-l10n-id"),
      expected[i][1]
    );
  }
}

async function runTest(test, getObservances) {
  registerCleanupFunction(function () {
    Services.prefs.clearUserPref("privacy.history.custom");
  });

  await openPreferencesViaOpenPreferencesAPI("panePrivacy", {
    leaveOpen: true,
  });

  let doc = gBrowser.contentDocument;

  await selectHistoryMode(gBrowser.contentWindow, "custom");

  let promiseSubDialogLoaded = promiseLoadSubDialog(
    "chrome://browser/content/preferences/dialogs/permissions.xhtml"
  );

  let cookieExceptionsButton = doc.getElementById("cookieExceptions");
  cookieExceptionsButton.scrollIntoView();
  EventUtils.synthesizeMouseAtCenter(
    cookieExceptionsButton,
    {},
    doc.documentGlobal
  );

  let win = await promiseSubDialogLoaded;

  doc = win.document;
  let params = {
    doc,
    richlistbox: doc.getElementById("permissionsBox"),
    url: doc.getElementById("url"),
    btnAllow: doc.getElementById("btnAllow"),
    btnBlock: doc.getElementById("btnBlock"),
    btnRemove: doc.getElementById("removePermission"),
    allowL10nId: win.gPermissionManager._getCapabilityL10nId(
      Ci.nsIPermissionManager.ALLOW_ACTION
    ),
    denyL10nId: win.gPermissionManager._getCapabilityL10nId(
      Ci.nsIPermissionManager.DENY_ACTION
    ),
    allow: Ci.nsIPermissionManager.ALLOW_ACTION,
    deny: Ci.nsIPermissionManager.DENY_ACTION,
  };
  let btnApplyChanges = doc.querySelector("dialog").getButton("accept");
  let observances = getObservances(params);
  let observeAllPromise = createObserveAllPromise(observances);

  await test(params, observeAllPromise, () => btnApplyChanges.doCommand());

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
}
