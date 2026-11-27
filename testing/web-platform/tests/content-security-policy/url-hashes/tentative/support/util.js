// Url hashes allow a script by the hash of the URL string it's requested with,
// e.g. the value of the src attribute. Tests get the url hash source of a URL
// string from url_hash() and check reports with assert_url_hash_report(), so
// that the syntax is only in these two functions.

// How tests with variants deliver the policy: "header" or "metatag".
const test_type = location.search.substring(1);

// Returns the SHA-256 hash of the UTF-8 encoding of |url_string|.
async function sha256_of(url_string) {
  const data = new TextEncoder().encode(url_string);
  return new Uint8Array(await crypto.subtle.digest('SHA-256', data));
}

// Returns the url hash source which allows |url_string|.
async function url_hash(url_string) {
  const hash = await sha256_of(url_string);
  return `'url-sha256-${hash.toBase64({alphabet: "base64url"})}'`;
}

// A tag for template literals of policies, which waits for the url hash
// sources from url_hash() in them, e.g.
// policy`script-src ${url_hash("externalScript.js")}`.
async function policy(strings, ...values) {
  const sources = await Promise.all(values);
  return strings.reduce(
      (result, string, i) => result + sources[i - 1] + string);
}

// Checks that |violation|, i.e. a SecurityPolicyViolationEvent or the data
// copied from it, has the url hash which allows the script requested with
// |url|. The urlHash member is in standard base64 with padding.
async function assert_url_hash_report(violation, url) {
  assert_equals(violation.urlHash,
                `url-sha256-${(await sha256_of(url)).toBase64()}`);
}

// Returns the URL of the support document |file|, with |policy| delivered
// either in the response header (|test_type| "header") or in a meta tag
// (|test_type| "metatag"), and the additional query parameters |params|. The
// document is served by document.py, which adds the meta tag. For |file| in a
// subdirectory, e.g. "child_dir/iframe", the document.py there serves
// support/iframe.sub.html at a URL in the subdirectory.
function get_support_url(test_type, file, policy, params = {}) {
  const query = new URLSearchParams(params);
  if (test_type == "header") {
    query.set("pipe", `header(Content-Security-Policy,${policy})`);
  } else if (test_type == "metatag") {
    query.set("policy", policy);
  } else {
    throw new Error(`Unknown test type "${test_type}"`);
  }
  const dir = file.slice(0, file.lastIndexOf("/") + 1);
  query.set("file", file.slice(dir.length));
  return `support/${dir}document.py?${query}`;
}

// Loads |src| in an iframe and resolves with the first message the iframe
// posts: "SCRIPT_RAN", or a violation from support/frame.js.
function load_frame(t, src) {
  const frame = document.createElement('iframe');
  frame.src = src;
  t.add_cleanup(() => frame.remove());
  const result = new Promise(resolve => {
    window.onmessage = e => {
      if (e.source === frame.contentWindow) {
        resolve(e.data);
      }
    };
  });
  document.body.appendChild(frame);
  return result;
}

// Checks that |result|, the message of a support document, is |expected|:
// "SCRIPT_RAN", or "CSP_VIOLATION" for a violation of |directive| by the script
// requested with |blocked_url|, by default the <script> element being tested,
// so that a test can't pass because something else was blocked.
function assert_result(result, expected,
                       {blocked_url, directive = "script-src-elem"} = {}) {
  const actual = typeof result == "string" ? result :
      `CSP_VIOLATION ${result.directive} ${result.blockedURI}`;
  if (expected == "CSP_VIOLATION") {
    blocked_url ??= result.scriptURL ?? "<the script being tested>";
    expected = `CSP_VIOLATION ${directive} ${blocked_url}`;
  }
  assert_equals(actual, expected);
}

// Returns |policy| with 'nonce-forinlinescript' added to its first directive,
// for the scripts of the support document.
function with_nonce(policy) {
  const [first, ...rest] = policy.split(";");
  return [`${first} 'nonce-forinlinescript'`, ...rest].join(";");
}

// Tests whether a script requested with |script_url|, e.g. the value of the
// src attribute, is allowed by the policy |csp|, a string or a promise of one,
// e.g. from policy``. 'nonce-forinlinescript' is added to its first directive,
// for the script of the support document. The policy is delivered as given by
// the variant of the test, see get_support_url(). Loads |script_url| in the
// support document |frame| and checks that it posts |expected|, see
// assert_result(), with |blocked_url| if the blocked script isn't the one
// requested with |script_url|.
function url_string_test({name, csp, script_url, expected, frame = "iframe",
                          blocked_url}) {
  promise_test(async t => {
    const result = await load_frame(t, get_support_url(test_type, frame,
        with_nonce(await csp), {script_url}));
    assert_result(result, expected, {blocked_url});
  }, name);
}
