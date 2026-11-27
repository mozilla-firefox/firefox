# Url hashes

These tests are for url hashes (`'url-sha256-...'`), which allow a script by
the hash of the URL string it's requested with, e.g. the value of the `src`
attribute of a `<script>` element, rather than of the absolute URL it resolves
to. Url hashes are proposed in the
[spec PR](https://github.com/w3c/webappsec-csp/pull/784), and matching the URL
string is discussed in
[issue #826](https://github.com/w3c/webappsec-csp/issues/826).

## Writing tests

Most tests use `url_string_test()` in `support/util.js`, which loads a script
with a given URL string in a document with a policy, and checks whether it ran.
The policy is written out in each test, e.g.
``policy`script-src ${url_hash("externalScript.js")}` ``, where the `policy`
tag waits for the url hash sources. `url_string_test()` adds
`'nonce-forinlinescript'` to it, for the script of the support document,
`support/frame.js`, which posts violations to the test. A test expecting a
violation checks that it's of `script-src-elem` for the URL of the script being
tested, the `<script>` element with the id `test-script`, so that it can't pass
because something else was blocked.

Tests get the url hash source of a URL string from `url_hash()`, and check
violation reports with `assert_url_hash_report()`. If another syntax of sources
matching URL strings is ever added, these two functions are the only place
which needs to know about it.

Most tests run with the policy both in a header and in a meta tag, as the
`?header` and `?metatag` variants. Tests of policies which can only be
delivered in a header, e.g. a worker's own policy, only use a header.

Support documents are served by `support/document.py`, with the policy from
`get_support_url()` in a header or in a meta tag, which `document.py` adds, so
that there's no copy of each document with the meta tag. The `document.py` in a
subdirectory serves them at a URL in it, for tests of relative URL strings.

The behavior of `'strict-dynamic-url'` which doesn't depend on url hashes is
tested in `/content-security-policy/strict-dynamic-url/`.
