// META: title=Url hashes of different URL strings
// META: variant=?header
// META: variant=?metatag
// META: script=support/util.js

url_string_test({
  name: "url hashes should allow script with a relative URL string by its URL string",
  csp: policy`script-src ${url_hash("externalScript.js")}`,
  script_url: "externalScript.js",
  expected: 'SCRIPT_RAN',
});

url_string_test({
  name: "url hashes should allow script with a relative URL string with a directory by its URL string",
  csp: policy`script-src ${url_hash("child_dir/externalScript.js")}`,
  script_url: "child_dir/externalScript.js",
  expected: 'SCRIPT_RAN',
});

url_string_test({
  name: "url hashes should not allow script with a relative URL string by the URL string with a single-dot segment",
  csp: policy`script-src ${url_hash("./externalScript.js")}`,
  script_url: "externalScript.js",
  expected: 'CSP_VIOLATION',
});

url_string_test({
  name: "url hashes should allow script with a relative URL string in a document in another directory by its URL string",
  frame: "child_dir/iframe",
  csp: policy`script-src ${url_hash("externalScript.js")}`,
  script_url: "externalScript.js",
  expected: 'SCRIPT_RAN',
});

url_string_test({
  name: "url hashes should allow script with a double-dot segment in its URL string in a document in another directory by its URL string",
  frame: "child_dir/iframe",
  csp: policy`script-src ${url_hash("../externalScript.js")}`,
  script_url: "../externalScript.js",
  expected: 'SCRIPT_RAN',
});
