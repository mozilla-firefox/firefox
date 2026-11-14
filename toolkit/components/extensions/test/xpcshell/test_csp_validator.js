"use strict";

const cps = Cc["@mozilla.org/addons/content-policy;1"].getService(
  Ci.nsIAddonContentPolicy
);

add_task(async function test_csp_validator_flags() {
  let checkPolicy = (policy, flags, expectedResult) => {
    info(`Checking policy: ${policy}`);

    let result = cps.validateAddonCSP(policy, flags);
    equal(result, expectedResult);
  };

  let flags = Ci.nsIAddonContentPolicy;

  checkPolicy(
    "default-src 'self'; script-src 'self' http://localhost",
    0,
    "\u2018script-src\u2019 directive contains a forbidden http: protocol source",
    "localhost disallowed"
  );
  checkPolicy(
    "default-src 'self'; script-src 'self' http://localhost",
    flags.CSP_ALLOW_LOCALHOST,
    null,
    "localhost allowed"
  );

  checkPolicy(
    "default-src 'self'; script-src 'self' 'unsafe-eval'",
    0,
    "\u2018script-src\u2019 directive contains a forbidden 'unsafe-eval' keyword",
    "eval disallowed"
  );
  checkPolicy(
    "default-src 'self'; script-src 'self' 'unsafe-eval'",
    flags.CSP_ALLOW_EVAL,
    null,
    "eval allowed"
  );

  checkPolicy(
    "default-src 'self'; script-src 'self' 'wasm-unsafe-eval'",
    0,
    "\u2018script-src\u2019 directive contains a forbidden 'wasm-unsafe-eval' keyword",
    "wasm disallowed"
  );
  checkPolicy(
    "default-src 'self'; script-src 'self' 'wasm-unsafe-eval'",
    flags.CSP_ALLOW_WASM,
    null,
    "wasm allowed"
  );
  checkPolicy(
    "default-src 'self'; script-src 'self' 'unsafe-eval' 'wasm-unsafe-eval'",
    flags.CSP_ALLOW_EVAL,
    null,
    "wasm and eval allowed"
  );

  checkPolicy(
    "default-src 'self'; script-src 'self' https://example.com",
    0,
    "\u2018script-src\u2019 directive contains a forbidden https: protocol source",
    "remote disallowed"
  );
  checkPolicy(
    "default-src 'self'; script-src 'self' https://example.com",
    flags.CSP_ALLOW_REMOTE,
    null,
    "remote allowed"
  );
});

add_task(async function test_csp_validator() {
  let checkPolicy = (policy, expectedResult) => {
    info(`Checking policy: ${policy}`);

    let result = cps.validateAddonCSP(
      policy,
      Ci.nsIAddonContentPolicy.CSP_ALLOW_ANY
    );
    equal(result, expectedResult);
  };

  checkPolicy("script-src 'self';", null);

  // In the past, object-src was required to be secure and defaulted to 'self'.
  // But that is no longer required (see bug 1766881).
  checkPolicy("script-src 'self'; object-src 'self';", null);
  checkPolicy("script-src 'self'; object-src https:;", null);

  let hash =
    "'sha256-NjZhMDQ1YjQ1MjEwMmM1OWQ4NDBlYzA5N2Q1OWQ5NDY3ZTEzYTNmMzRmNjQ5NGU1MzlmZmQzMmMxYmIzNWYxOCAgLQo='";

  checkPolicy(
    `script-src 'self' https://com https://*.example.com moz-extension://09abcdef blob: filesystem: ${hash} 'unsafe-eval'; ` +
      `object-src 'self' https://com https://*.example.com moz-extension://09abcdef blob: filesystem: ${hash}`,
    null
  );

  checkPolicy(
    "",
    "Policy is missing a required \u2018script-src\u2019 directive"
  );

  checkPolicy(
    "object-src 'none';",
    "Policy is missing a required \u2018script-src\u2019 directive"
  );

  checkPolicy(
    "default-src 'self' http:",
    "Policy is missing a required \u2018script-src\u2019 directive",
    "A strict default-src is required as a fallback if script-src is missing"
  );

  checkPolicy(
    "default-src 'self' http:; script-src 'self'",
    null,
    "A valid script-src removes the need for a strict default-src fallback"
  );

  checkPolicy(
    "default-src 'self'",
    null,
    "A valid default-src should count as a valid script-src"
  );

  checkPolicy(
    "default-src 'self'; script-src 'self'",
    null,
    "A valid default-src should count as a valid script-src"
  );

  checkPolicy(
    "default-src 'self'; script-src http://example.com",
    "\u2018script-src\u2019 directive contains a forbidden http: protocol source",
    "A valid default-src should not allow an invalid script-src directive"
  );

  checkPolicy(
    "script-src 'none'",
    "\u2018script-src\u2019 must include the source 'self'"
  );

  checkPolicy(
    "script-src 'self' 'unsafe-inline'",
    "\u2018script-src\u2019 directive contains a forbidden 'unsafe-inline' keyword"
  );

  // Localhost is always valid
  for (let src of [
    "http://localhost",
    "https://localhost",
    "http://127.0.0.1",
    "https://127.0.0.1",
  ]) {
    checkPolicy(`script-src 'self' ${src};`, null);
  }

  let directives = ["script-src", "worker-src"];

  for (let [directive, other] of [directives, directives.slice().reverse()]) {
    for (let src of ["https://*", "https://*.blogspot.com", "https://*"]) {
      checkPolicy(
        `${directive} 'self' ${src}; ${other} 'self';`,
        `https: wildcard sources in \u2018${directive}\u2019 directives must include at least one non-generic sub-domain (e.g., *.example.com rather than *.com)`
      );
    }

    for (let protocol of ["http", "https"]) {
      checkPolicy(
        `${directive} 'self' ${protocol}:; ${other} 'self';`,
        `${protocol}: protocol requires a host in \u2018${directive}\u2019 directives`
      );
    }

    checkPolicy(
      `${directive} 'self' http://example.com; ${other} 'self';`,
      `\u2018${directive}\u2019 directive contains a forbidden http: protocol source`
    );

    for (let protocol of ["ftp", "meh"]) {
      checkPolicy(
        `${directive} 'self' ${protocol}:; ${other} 'self';`,
        `\u2018${directive}\u2019 directive contains a forbidden ${protocol}: protocol source`
      );
    }

    checkPolicy(
      `${directive} 'self' 'nonce-01234'; ${other} 'self';`,
      `\u2018${directive}\u2019 directive contains a forbidden 'nonce-*' keyword`
    );

    checkPolicy(
      `${directive} 'self' 'url-sha256-AAAA'; ${other} 'self';`,
      `\u2018${directive}\u2019 directive contains a forbidden 'url-*' keyword`
    );
  }
});

add_task(async function test_csp_validator_extension_pages() {
  let checkPolicy = (policy, expectedResult) => {
    info(`Checking policy: ${policy}`);

    // While Schemas.sys.mjs uses Ci.nsIAddonContentPolicy.CSP_ALLOW_WASM, we
    // don't pass that here because we are only verifying that remote scripts are
    // blocked here.
    let result = cps.validateAddonCSP(policy, 0);
    equal(result, expectedResult);
  };

  checkPolicy("script-src 'self';", null);
  checkPolicy("script-src 'self'; worker-src 'none'", null);
  checkPolicy("script-src 'self'; worker-src 'self'", null);

  // In the past, object-src was required to be secure and defaulted to 'self'.
  // But that is no longer required (see bug 1766881).
  checkPolicy("script-src 'self'; object-src 'self';", null);
  checkPolicy("script-src 'self'; object-src https:;", null);

  let hash =
    "'sha256-NjZhMDQ1YjQ1MjEwMmM1OWQ4NDBlYzA5N2Q1OWQ5NDY3ZTEzYTNmMzRmNjQ5NGU1MzlmZmQzMmMxYmIzNWYxOCAgLQo='";

  checkPolicy(
    `script-src 'self' moz-extension://09abcdef blob: filesystem: ${hash}; `,
    null
  );

  for (let policy of ["", "script-src-elem 'none';", "worker-src 'none';"]) {
    checkPolicy(
      policy,
      "Policy is missing a required \u2018script-src\u2019 directive"
    );
  }

  checkPolicy(
    "default-src 'self' http:; script-src 'self'",
    null,
    "A valid script-src removes the need for a strict default-src fallback"
  );

  checkPolicy(
    "default-src 'self'",
    null,
    "A valid default-src should count as a valid script-src"
  );

  for (let directive of ["script-src", "worker-src"]) {
    checkPolicy(
      `default-src 'self'; ${directive} 'self'`,
      null,
      `A valid default-src should count as a valid ${directive}`
    );
    checkPolicy(
      `default-src 'self'; ${directive} http://example.com`,
      `\u2018${directive}\u2019 directive contains a forbidden http: protocol source`,
      `A valid default-src should not allow an invalid ${directive} directive`
    );
  }

  checkPolicy(
    "script-src 'none'",
    "\u2018script-src\u2019 must include the source 'self'"
  );

  checkPolicy(
    "script-src 'self' 'unsafe-inline';",
    "\u2018script-src\u2019 directive contains a forbidden 'unsafe-inline' keyword"
  );

  checkPolicy(
    "script-src 'self' 'unsafe-eval';",
    "\u2018script-src\u2019 directive contains a forbidden 'unsafe-eval' keyword"
  );

  // Localhost is invalid under normal circumstance, but passes through
  // if validated under CSP_ALLOW_LOCALHOST, which is the validation
  // setting used for temporarily loaded extensions.
  for (let src of [
    "http://localhost",
    "https://localhost",
    "http://127.0.0.1",
    "https://127.0.0.1",
  ]) {
    const protocol = src.split(":")[0];
    checkPolicy(
      `script-src 'self' ${src};`,
      `\u2018script-src\u2019 directive contains a forbidden ${protocol}: protocol source`
    );

    equal(
      cps.validateAddonCSP(
        `script-src 'self' ${src}`,
        Ci.nsIAddonContentPolicy.CSP_ALLOW_LOCALHOST
      ),
      null,
      `Localhost source should be allowed by CSP_ALLOW_LOCALHOST: ${src}`
    );
  }

  let directives = ["script-src", "worker-src"];

  for (let [directive, other] of [directives, directives.slice().reverse()]) {
    for (let protocol of ["http", "https"]) {
      checkPolicy(
        `${directive} 'self' ${protocol}:; ${other} 'self';`,
        `${protocol}: protocol requires a host in \u2018${directive}\u2019 directives`
      );
    }

    checkPolicy(
      `${directive} 'self' https://example.com; ${other} 'self';`,
      `\u2018${directive}\u2019 directive contains a forbidden https: protocol source`
    );

    checkPolicy(
      `${directive} 'self' http://example.com; ${other} 'self';`,
      `\u2018${directive}\u2019 directive contains a forbidden http: protocol source`
    );

    for (let protocol of ["ftp", "meh"]) {
      checkPolicy(
        `${directive} 'self' ${protocol}:; ${other} 'self';`,
        `\u2018${directive}\u2019 directive contains a forbidden ${protocol}: protocol source`
      );
    }

    checkPolicy(
      `${directive} 'self' 'nonce-01234'; ${other} 'self';`,
      `\u2018${directive}\u2019 directive contains a forbidden 'nonce-*' keyword`
    );

    checkPolicy(
      `${directive} 'self' 'url-sha256-AAAA'; ${other} 'self';`,
      `\u2018${directive}\u2019 directive contains a forbidden 'url-*' keyword`
    );
  }
});

add_task(async function test_csp_validator_sandbox_pages() {
  const checkPolicy = (policy, expectedResult) => {
    info(`Checking policy: ${policy}`);

    const result = cps.validateAddonSandboxCSP(policy);
    equal(result, expectedResult);
  };

  // Validate hard coded default csp
  const defaultPolicy = "sandbox allow-scripts; script-src 'self';";
  checkPolicy(defaultPolicy, null);

  for (const policy of [
    "script-src 'self';",
    "script-src 'self'; default-src 'self';",
  ]) {
    checkPolicy(
      policy,
      "Policy is missing a required \u2018sandbox\u2019 directive"
    );
    checkPolicy(`sandbox allow-scripts; ${policy}`, null);
  }

  for (const policy of [
    "sandbox allow-same-origin;",
    "sandbox allow-scripts allow-same-origin;",
  ]) {
    checkPolicy(
      policy,
      "‘sandbox’ directive contains a forbidden 'allow-same-origin' keyword"
    );
  }

  // check with all existing sandbox flags except for allow-same-origin
  checkPolicy(
    `sandbox allow-downloads allow-downloads-without-user-activation allow-forms allow-modals allow-orientation-lock allow-pointer-lock allow-popups allow-popups-to-escape-sandbox allow-presentation allow-scripts allow-storage-access-by-user-activation allow-top-navigation allow-top-navigation-by-user-activation allow-top-navigation-to-custom-protocols; script-src 'unsafe-inline' 'unsafe-eval' https: http: data: blob: 'self';`,
    null
  );
});

add_task(
  { pref_set: [["security.csp.url-hashes.enabled", false]] },
  async function test_csp_validator_url_hashes_disabled() {
    // Without the pref, url hashes aren't parsed, and are ignored like any other
    // unknown source expression.
    equal(
      cps.validateAddonCSP(
        "script-src 'self' 'url-sha256-AAAA';",
        Ci.nsIAddonContentPolicy.CSP_ALLOW_ANY
      ),
      null
    );
  }
);
