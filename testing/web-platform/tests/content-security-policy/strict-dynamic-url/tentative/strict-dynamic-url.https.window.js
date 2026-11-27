// META: title='strict-dynamic-url'
// META: variant=?header
// META: variant=?metatag
// META: script=support/util.js

// The behavior of 'strict-dynamic-url' with scripts allowed by url hashes is
// tested in /content-security-policy/url-hashes/.

promise_test(async t => {
  // The hash of the inline script in iframe_inline.sub.html, which inserts
  // externalScript.js, which isn't allowed itself.
  const inlineScriptHash = "F8UqObF6TSi2W4dDcDzAOAplJkYovBE6JpJjsZJy5HA=";
  const policy = `script-src 'nonce-forinlinescript' 'strict-dynamic-url' ` +
    `'sha256-${inlineScriptHash}'`;

  const result = await load_frame(t,
      get_support_url(test_type, 'iframe_inline', policy));
  assert_equals(result, 'SCRIPT_RAN');
}, "strict-dynamic-url should allow script dynamically inserted by an inline script allowed by its hash");
