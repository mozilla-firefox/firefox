// META: title='strict-dynamic-url' with scripts allowed by url hashes
// META: variant=?header
// META: variant=?metatag
// META: script=support/util.js

// The behavior of 'strict-dynamic-url' which doesn't depend on url hashes is
// tested in /content-security-policy/strict-dynamic-url/.

url_string_test({
  // add_dynamic_script.js inserts externalScript.js, which isn't allowed
  // itself.
  name: "strict-dynamic-url should allow script dynamically inserted by a script allowed by its url hash",
  csp: policy`script-src 'strict-dynamic-url' ${url_hash("add_dynamic_script.js")}`,
  script_url: "add_dynamic_script.js",
  expected: 'SCRIPT_RAN',
});
