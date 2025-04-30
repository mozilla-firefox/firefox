/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

const { PrincipalUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/quota/test/modules/PrincipalUtils.sys.mjs"
);
const { QuotaUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/quota/test/modules/QuotaUtils.sys.mjs"
);
const { SimpleDBUtils } = ChromeUtils.importESModule(
  "resource://testing-common/dom/simpledb/test/modules/SimpleDBUtils.sys.mjs"
);

async function testArtificialFailure() {
  const categories = [
    Ci.nsIQuotaArtificialFailure.CATEGORY_NONE,
    Ci.nsIQuotaArtificialFailure.CATEGORY_INITIALIZE_ORIGIN,
  ];
  const principal = PrincipalUtils.createPrincipal("https://example.com");
  const name = "foo";

  for (let category of categories) {
    info("Creating database");

    {
      const connection = SimpleDBUtils.createConnection(principal);

      const openRequest = connection.open(name);
      await SimpleDBUtils.requestFinished(openRequest);

      const closeRequest = connection.close();
      await SimpleDBUtils.requestFinished(closeRequest);
    }

    info("Resetting storage");

    {
      const request = Services.qms.reset();
      await QuotaUtils.requestFinished(request);
    }

    info("Opening database");

    const ex = await QuotaUtils.withArtificialFailures(
      category,
      /* probability */ 100,
      Cr.NS_ERROR_FILE_ACCESS_DENIED,
      async function () {
        const connection = SimpleDBUtils.createConnection(principal);
        const request = connection.open(name);
        try {
          await SimpleDBUtils.requestFinished(request);
          return null;
        } catch (ex) {
          return ex;
        }
      }
    );

    if (category == Ci.nsIQuotaArtificialFailure.CATEGORY_NONE) {
      Assert.ok(!ex, "Should not have thrown");
    } else {
      Assert.ok(ex, "Should have thrown");
      Assert.strictEqual(
        ex.resultCode,
        // NS_ERROR_FILE_ACCESS_DENIED is mapped to NS_ERROR_FAILURE during error
        // propagation.
        NS_ERROR_FAILURE,
        "Threw right result code"
      );
    }

    info("Clearing storage");

    {
      const request = Services.qms.clear();
      await QuotaUtils.requestFinished(request);
    }
  }
}

async function testSteps() {
  add_task(
    {
      pref_set: [["dom.quotaManager.loadQuotaFromCache", false]],
    },
    testArtificialFailure
  );
}
