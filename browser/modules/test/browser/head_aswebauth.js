/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

ChromeUtils.defineESModuleGetters(this, {
  ASWebAuthSessionService:
    "moz-src:///browser/modules/ASWebAuthSessionService.sys.mjs",
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
});

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const TEST_BASE = "https://example.com/browser/browser/modules/test/browser/";
const LOGIN_URL = TEST_BASE + "browser_aswebauth_form.sjs";
const CALLBACK_SCHEME = "data";
const CALLBACK_URL = "data:text/html,authcomplete?code=abc123";

function startAuthSession(params) {
  let {
    url,
    uuid,
    callbackScheme = "",
    hasCallback = false,
    ephemeral = false,
    headers = {},
    matchURLs = [],
  } = params;

  let request = {
    QueryInterface: ChromeUtils.generateQI(["nsIASWebAuthSessionRequest"]),
    uuid,
    url,
    callbackScheme,
    hasCallback,
    useEphemeralSession: ephemeral,
    get additionalHeaderNames() {
      return Object.keys(headers);
    },
    getAdditionalHeader(name) {
      return headers[name] ?? "";
    },
    matchesCallbackURL(candidate) {
      return matchURLs.includes(candidate);
    },
    complete(callbackURL) {
      Services.obs.notifyObservers(
        null,
        "aswebauthsession-test-complete",
        JSON.stringify({ uuid, callbackURL })
      );
    },
    cancel() {
      Services.obs.notifyObservers(
        null,
        "aswebauthsession-test-cancel",
        JSON.stringify({ uuid })
      );
    },
  };

  Services.obs.notifyObservers(request, "aswebauthsession-request-begin");
}

function newUUID() {
  return Services.uuid.generateUUID().toString().slice(1, -1).toUpperCase();
}

function waitForObserver(topic) {
  return TestUtils.topicObserved(topic).then(([, data]) => data);
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.userContext.enabled", true]],
  });
});
