// Posts the result of a test to it when the policy of the support document is
// violated: the directive and the blocked URL, so that the test can check that
// it's the script it's about which was blocked, together with the URL of the
// <script> element being tested, the one with the id "test-script", and the
// urlHash, for tests of violation reports. Scripts which run post
// "SCRIPT_RAN" themselves.

// Returns the URL the <script> element |script| requests, without its
// fragment, like the blocked URL of a violation.
function test_script_url(script) {
  const url = new URL(script.src);
  url.hash = '';
  return url.href;
}

window.addEventListener('securitypolicyviolation', e => {
  const script = document.getElementById('test-script');
  top.postMessage({
    directive: e.effectiveDirective,
    blockedURI: e.blockedURI,
    urlHash: e.urlHash,
    scriptURL: script ? test_script_url(script) : null,
  }, '*');
});
