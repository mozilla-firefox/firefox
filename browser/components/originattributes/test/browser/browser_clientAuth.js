let certCached = true;
let secondTabStarted = false;

function onCertDialogLoaded(subject) {
  certCached = false;
  // Click OK.
  subject.acceptDialog();
}

Services.obs.addObserver(onCertDialogLoaded, "cert-dialog-loaded");

registerCleanupFunction(() => {
  Services.obs.removeObserver(onCertDialogLoaded, "cert-dialog-loaded");
  Cc["@mozilla.org/security/clientAuthRememberService;1"]
    .getService(Ci.nsIClientAuthRememberService)
    .clearRememberedDecisions();
});

async function setup() {
  await SpecialPowers.pushPrefEnv({
    set: [["security.default_personal_cert", "Ask Every Time"]],
  });
}

function getResult() {
  // The first tab always returns true.
  if (!secondTabStarted) {
    certCached = true;
    secondTabStarted = true;
    return true;
  }

  // The second tab returns true if the cert is cached, so it will be different
  // from the result of the first tab, and considered isolated.
  let ret = certCached;
  certCached = true;
  secondTabStarted = false;
  return ret;
}

// aGetResultImmediately must be true because we need to get the result before
// the next tab is opened.
IsolationTestTools.runTests(
  "https://requireclientcert.example.com",
  getResult,
  null, // aCompareResultFunc
  setup, // aBeginFunc
  true
); // aGetResultImmediately
