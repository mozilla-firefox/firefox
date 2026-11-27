// META: title=Scripts allowed by url hashes in policies
// META: variant=?header
// META: variant=?metatag
// META: script=/common/get-host-info.sub.js
// META: script=support/util.js

// Scripts are allowed by the URL strings they're requested with, e.g. the
// value of the src attribute. add_dynamic_script.js inserts externalScript.js
// with an absolute URL string.
const { ORIGIN, REMOTE_ORIGIN } = get_host_info();
const scriptUrl = new URL("support/externalScript.js", location.href).href;
const redirectUrl =
  new URL(`/common/redirect.py?location=${scriptUrl}`, location.href).href;

url_string_test({
  name: "script-src should allow script by its url hash",
  csp: policy`script-src ${url_hash("externalScript.js")}`,
  script_url: "externalScript.js",
  expected: 'SCRIPT_RAN',
});

url_string_test({
  name: "a url hash should make host sources ignored",
  csp: policy`script-src ${url_hash("foo.js")} ${ORIGIN} ${REMOTE_ORIGIN}`,
  script_url: "externalScript.js",
  expected: 'CSP_VIOLATION',
});

url_string_test({
  name: "default-src should allow script by its url hash",
  csp: policy`default-src ${url_hash("externalScript.js")}`,
  script_url: "externalScript.js",
  expected: 'SCRIPT_RAN',
});

url_string_test({
  name: "script-src-elem should allow script by its url hash",
  csp: policy`script-src-elem ${url_hash("externalScript.js")}`,
  script_url: "externalScript.js",
  expected: 'SCRIPT_RAN',
});

url_string_test({
  // add_dynamic_script.js inserts externalScript.js, which isn't allowed.
  name: "url hashes should not allow dynamically inserted script transitively",
  csp: policy`script-src ${url_hash("add_dynamic_script.js")}`,
  script_url: "add_dynamic_script.js",
  expected: 'CSP_VIOLATION',
  blocked_url: scriptUrl,
});

url_string_test({
  name: "url hashes should allow dynamically inserted script if allowed itself",
  csp: policy`script-src ${url_hash("add_dynamic_script.js")} ${url_hash(scriptUrl)}`,
  script_url: "add_dynamic_script.js",
  expected: 'SCRIPT_RAN',
});

url_string_test({
  // The target of the redirect, externalScript.js, isn't allowed itself.
  name: "url hashes should allow redirected scripts",
  csp: policy`script-src ${url_hash(redirectUrl)}`,
  script_url: redirectUrl,
  expected: 'SCRIPT_RAN',
});
