"use strict";

// A document that sends its own Permissions-Policy header combines it with
// the policy inherited from its container instead of replacing it.

const PATH = "/browser/dom/security/permissionspolicy/test/browser/";
const EMPTY_PAGE = "empty.html";
const HEADER_PAGE = "file_header_policy.sjs";

async function addFrame(bc, src, allow) {
  return SpecialPowers.spawn(bc, [src, allow], async (aSrc, aAllow) => {
    let iframe = content.document.createElement("iframe");
    iframe.allow = aAllow;
    iframe.src = aSrc;
    let loaded = new Promise(resolve =>
      iframe.addEventListener("load", resolve, { once: true })
    );
    content.document.body.appendChild(iframe);
    await loaded;
    return iframe.browsingContext;
  });
}

async function navigateFrame(parentBC, frameBC, target) {
  let loaded = SpecialPowers.spawn(parentBC, [], () => {
    let iframe = content.document.querySelector("iframe");
    return new Promise(resolve =>
      iframe.addEventListener("load", () => resolve(), { once: true })
    );
  });
  await SpecialPowers.spawn(frameBC, [target], aTarget => {
    content.location = aTarget;
  });
  await loaded;
}

function isGeoDelegationUnsafe(bc) {
  return SpecialPowers.spawn(bc, [], () => {
    is(
      content.document.nodePrincipal.origin + "",
      content.location.origin,
      "Checking the expected document"
    );
    return content.document.permDelegateHandler
      .QueryInterface(Ci.nsIPermissionDelegateHandler)
      .maybeUnsafePermissionDelegate(["geo"]);
  });
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.security.permissionsPolicy.header.enabled", true]],
  });
});

add_task(async function testKeepsSrcOrigin() {
  await BrowserTestUtils.withNewTab(
    "https://example.com" + PATH + EMPTY_PAGE,
    async browser => {
      let frame = await addFrame(
        browser,
        "https://example.org" + PATH + HEADER_PAGE + "?microphone=()",
        "geolocation *"
      );
      ok(
        !(await isGeoDelegationUnsafe(frame)),
        "Document from the src origin is trusted"
      );
    }
  );
});

add_task(async function testKeepsAncestorChain() {
  await BrowserTestUtils.withNewTab(
    "https://example.com" + PATH + EMPTY_PAGE,
    async browser => {
      let ancestor = await addFrame(
        browser,
        "https://test1.example.com" + PATH + EMPTY_PAGE,
        "geolocation https://test1.example.com https://test2.example.com"
      );
      let frame = await addFrame(
        ancestor,
        "https://test1.example.com" + PATH + EMPTY_PAGE,
        "geolocation *"
      );
      await navigateFrame(
        ancestor,
        frame,
        "https://test2.example.com" + PATH + HEADER_PAGE + "?microphone=()"
      );
      ok(
        !(await isGeoDelegationUnsafe(frame)),
        "Document from an origin allowed in the ancestor chain is trusted"
      );
    }
  );
});

async function checkCombinedPolicy(allow, header, expected, message) {
  await BrowserTestUtils.withNewTab(
    "https://example.com" + PATH + EMPTY_PAGE,
    async browser => {
      let frame = await addFrame(
        browser,
        "https://example.org" + PATH + HEADER_PAGE + "?" + header,
        allow
      );
      let state = await SpecialPowers.spawn(
        frame,
        [],
        async () =>
          (await content.navigator.permissions.query({ name: "geolocation" }))
            .state
      );
      is(state, expected, message);
    }
  );
}

add_task(async function testAttributeAllowsHeaderDenies() {
  await checkCombinedPolicy(
    "geolocation",
    "geolocation=()",
    "denied",
    "Header denies what the attribute allows"
  );
});

add_task(async function testAttributeDeniesHeaderAllows() {
  await checkCombinedPolicy(
    "geolocation 'none'",
    "geolocation=*",
    "denied",
    "Header cannot allow what the attribute denies"
  );
});

add_task(async function testAttributeAllowsHeaderAllows() {
  await checkCombinedPolicy(
    "geolocation",
    "geolocation=*",
    "prompt",
    "Allowed when both the attribute and the header allow"
  );
});
