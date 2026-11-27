// META: title=Url hashes in policies in headers and meta tags
// META: script=support/util.js

// Some of these tests set CSP in both the header and the meta tag, others set
// multiple policies in multiple meta tags. They all load externalScript.js,
// which they allow by its URL string.

const scriptUrlString = "externalScript.js";

promise_test(async t => {
  const headerPolicy = `script-src 'nonce-forinlinescript'`;
  const metaPolicy = await policy`script-src 'nonce-forinlinescript' ${url_hash(scriptUrlString)}`;

  const result = await load_frame(t, get_support_url('metatag', 'iframe',
      metaPolicy, {
        pipe: `header(Content-Security-Policy,${headerPolicy})`,
        script_url: scriptUrlString,
      }));
  assert_result(result, 'CSP_VIOLATION');
}, "url hash in meta tag should not relax policy set by header");

promise_test(async t => {
  const headerPolicy = await policy`script-src 'nonce-forinlinescript' ${url_hash(scriptUrlString)}`;
  const metaPolicy = `script-src 'nonce-forinlinescript'`;

  const result = await load_frame(t, get_support_url('metatag', 'iframe',
      metaPolicy, {
        pipe: `header(Content-Security-Policy,${headerPolicy})`,
        script_url: scriptUrlString,
      }));
  assert_result(result, 'CSP_VIOLATION');
}, "meta tag can restrict policy set by header");

promise_test(async t => {
  const headerPolicy = await policy`script-src 'nonce-forinlinescript' ${url_hash(scriptUrlString)}`;
  const metaPolicy = await policy`script-src 'nonce-forinlinescript' ${url_hash(scriptUrlString)} ${url_hash("abc.js")}`;

  const result = await load_frame(t, get_support_url('metatag', 'iframe',
      metaPolicy, {
        pipe: `header(Content-Security-Policy,${headerPolicy})`,
        script_url: scriptUrlString,
      }));
  assert_result(result, 'SCRIPT_RAN');
}, "more lax meta tag should still allow script");

promise_test(async t => {
  const metaPolicy1 = await policy`script-src 'nonce-forinlinescript' ${url_hash(scriptUrlString)}`;
  const metaPolicy2 = `script-src 'nonce-forinlinescript'`;

  const result = await load_frame(t,
      `support/iframe_meta_multiple.html?${new URLSearchParams({
        pipe: 'sub',
        policy1: metaPolicy1,
        policy2: metaPolicy2,
      })}`);
  assert_result(result, 'CSP_VIOLATION');
}, "multiple meta tags should apply most strict policy - lax first");

promise_test(async t => {
  const metaPolicy1 = `script-src 'nonce-forinlinescript'`;
  const metaPolicy2 = await policy`script-src 'nonce-forinlinescript' ${url_hash(scriptUrlString)}`;

  const result = await load_frame(t,
      `support/iframe_meta_multiple.html?${new URLSearchParams({
        pipe: 'sub',
        policy1: metaPolicy1,
        policy2: metaPolicy2,
      })}`);
  assert_result(result, 'CSP_VIOLATION');
}, "multiple meta tags should apply most strict policy - strict first");

promise_test(async t => {
  const metaPolicy1 = await policy`script-src 'nonce-forinlinescript' ${url_hash(scriptUrlString)} ${url_hash("abc.js")}`;
  const metaPolicy2 = await policy`script-src 'nonce-forinlinescript' ${url_hash(scriptUrlString)} ${url_hash("def.js")}`;

  const result = await load_frame(t,
      `support/iframe_meta_multiple.html?${new URLSearchParams({
        pipe: 'sub',
        policy1: metaPolicy1,
        policy2: metaPolicy2,
      })}`);
  assert_result(result, 'SCRIPT_RAN');
}, "multiple meta tags should apply most strict policy - both lax");
