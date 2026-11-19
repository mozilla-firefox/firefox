/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Drives the fxcontainers component straight through its generated bindings,
// without ContextualIdentityService in the way. Two things are being checked
// that the Rust unit tests cannot: that the bindings carry every type across
// intact, and that the component's host normalization agrees with the one
// Gecko applies.

"use strict";

const {
  ContainerColor,
  ContainerIcon,
  ContainerLabel,
  ContainersCallback,
  ContainersStore,
  InvalidSite,
  Malformed,
  NoSuchContainer,
  UnsupportedVersion,
  colorName,
  iconName,
  labelGeckoL10nId,
  latestVersion,
  maxUserContextId,
} = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustFxcontainers.sys.mjs"
);

const idnService = Cc["@mozilla.org/network/idn-service;1"].getService(
  Ci.nsIIDNService
);

/**
 * Stands in for the embedder: counts the times the component asked to be
 * written out.
 */
class Recorder extends ContainersCallback {
  constructor() {
    super();
    this.persists = 0;
  }

  persist() {
    this.persists++;
  }
}

function documentOf(store) {
  return JSON.parse(new TextDecoder().decode(store.serialize()));
}

// persist is FireAndForget, so the component dispatches it as a main-thread
// runnable instead of calling it inline: it lands a turn after the call that
// triggered it. Dispatching our own runnable now puts us behind theirs in the
// queue.
function flushCallbacks() {
  return new Promise(resolve => Services.tm.dispatchToMainThread(resolve));
}

function seededStore() {
  const recorder = new Recorder();
  return { store: ContainersStore.init(null, null, recorder), recorder };
}

add_task(async function seedsTheShippedContainers() {
  const { store, recorder } = seededStore();
  await flushCallbacks();

  equal(store.publicIdentities().length, 4, "Four shipped containers");
  equal(recorder.persists, 1, "Seeding asks to be written once");

  const document = documentOf(store);
  equal(document.version, latestVersion(), "Written at the current version");
  equal(
    document.identities.at(-1).userContextId,
    maxUserContextId(),
    "The reserved identity keeps the last valid id"
  );
  ok(
    store.privateIdentity("userContextIdInternal.webextStorageLocal"),
    "The reserved identity is reachable by name"
  );
});

// A shipped container's label is one of the component's own variants, not a
// stored string: the Fluent id comes from the variant, so renaming the string
// behind it needs no migration.
add_task(async function shippedContainersCarryALocalizedLabel() {
  const { store } = seededStore();

  for (const container of store.publicIdentities()) {
    ok(
      !(container.label instanceof ContainerLabel.Name),
      `Container ${container.userContextId} is not labelled by name`
    );
    ok(
      labelGeckoL10nId(container.label),
      "The variant resolves to a Fluent id"
    );
    ok(container.isPublic, "Shipped containers are public");
  }
});

add_task(async function createUpdateAndRemove() {
  const { store, recorder } = seededStore();

  const created = store.create(
    "Reading",
    ContainerIcon.TREE,
    ContainerColor.PURPLE
  );
  equal(created.userContextId, 6, "The next id is assigned");
  ok(
    created.label instanceof ContainerLabel.Name,
    "A user-created container is labelled by name"
  );
  equal(created.label.name, "Reading", "The name survives the round trip");
  equal(iconName(created.icon), "tree", "The icon survives the round trip");
  equal(
    colorName(created.color),
    "purple",
    "The color survives the round trip"
  );

  const updated = store.update(
    6,
    "Reading list",
    ContainerIcon.GIFT,
    ContainerColor.CYAN
  );
  equal(updated.label.name, "Reading list", "The name is replaced");
  equal(colorName(updated.color), "cyan", "And so is the color");

  // The appearance is one of a fixed set, so the document can only ever name an
  // icon and a color the embedder has artwork for.
  const stored = documentOf(store).identities.find(
    identity => identity.userContextId === 6
  );
  equal(stored.icon, "gift", "The document stores the icon by name");
  equal(stored.color, "cyan", "The document stores the color by name");

  const removed = store.remove(6);
  equal(removed.userContextId, 6, "The removed container is reported back");
  equal(store.publicIdentities().length, 4, "And is gone from the list");

  await flushCallbacks();
  equal(recorder.persists, 4, "Each change asked to be written");
});

add_task(async function siteAssociations() {
  const { store } = seededStore();

  equal(
    store.getSiteAssociation("example.com"),
    0,
    "No association by default"
  );
  store.setSiteAssociation("example.com", 2);
  equal(store.getSiteAssociation("example.com"), 2, "Association was set");

  equal(store.getSiteAssociations(2).length, 1, "Filtered by container");
  equal(store.getSiteAssociations(null).length, 1, "Or listed in full");

  store.removeSiteAssociation("example.com");
  equal(store.getSiteAssociation("example.com"), 0, "Association was removed");

  Assert.throws(
    () => store.setSiteAssociation("example.com", 999),
    e => e instanceof NoSuchContainer,
    "An unknown container is rejected"
  );
});

// The component normalizes hosts with the `idna` crate, Gecko with
// nsIIDNService. Nothing guarantees the two agree, and a divergence would stop
// associations from matching without any visible error, so pin it.
add_task(async function hostNormalizationAgreesWithGecko() {
  const { store } = seededStore();

  // Everything test_siteAssociations.js expects Gecko to accept or reject, so
  // that a divergence shows up here rather than as associations that silently
  // stop matching.
  const hosts = [
    "example.org",
    "Example.ORG",
    "EXAMPLE.example",
    "sub.domain.example.org",
    "bücher.example",
    "xn--bcher-kva.example",
    "例え.テスト",
    "example.org.",
    "xn--e1afmkfd.xn--p1ai",
    "",
    "  example.com  ",
    "example .com",
    "example.com:8080",
    "example.com/path",
    "user@example.com",
    "exa%mple.com",
    "[::1]",
    "*",
    "*.example.com",
  ];

  for (const host of hosts) {
    // What ContextualIdentityService has always accepted: domainToASCII must
    // succeed, and the result must still look like a host.
    let geckoHost = null;
    try {
      const ascii = idnService.domainToASCII(host);
      if (ascii && !/[\s:/@%[\]*]/.test(ascii)) {
        geckoHost = ascii;
      }
    } catch (e) {
      geckoHost = null;
    }

    if (geckoHost === null) {
      Assert.throws(
        () => store.setSiteAssociation(host, 1),
        e => e instanceof InvalidSite,
        `"${host}" is rejected by both`
      );
      continue;
    }

    store.setSiteAssociation(host, 1);
    equal(
      store.getSiteAssociation(geckoHost),
      1,
      `"${host}" normalizes to the same host as nsIIDNService ("${geckoHost}")`
    );
    store.removeSiteAssociation(geckoHost);
  }
});

add_task(async function wildcardsAreNotSites() {
  const { store } = seededStore();

  Assert.throws(
    () => store.setSiteAssociation("*.example.org", 1),
    e => e instanceof InvalidSite,
    "A wildcard can never match a navigation"
  );
});

add_task(async function aDocumentSurvivesAReload() {
  const { store } = seededStore();
  store.create("Reading", ContainerIcon.TREE, ContainerColor.PURPLE);
  store.setSiteAssociation("example.org", 1);

  const reloaded = ContainersStore.init(
    store.serialize(),
    null,
    new Recorder()
  );

  Assert.deepEqual(
    reloaded.publicUserContextIds(),
    store.publicUserContextIds(),
    "The container list round trips through the stored document"
  );
  equal(
    reloaded.getSiteAssociation("example.org"),
    1,
    "And so do the site associations"
  );
});

add_task(async function unusableDataIsReported() {
  // The bindings check `instanceof Uint8Array` against the system module's
  // global, and this script runs in a different one, so an array built here is
  // rejected before it ever reaches Rust. Borrow the constructor from bytes the
  // component itself produced.
  const { store } = seededStore();
  const template = store.serialize();
  const bytes = text => {
    const encoded = new TextEncoder().encode(text);
    const sameRealm = new template.constructor(encoded.length);
    sameRealm.set(encoded);
    return sameRealm;
  };

  Assert.throws(
    () => ContainersStore.init(bytes("{ vers"), null, new Recorder()),
    e => e instanceof Malformed,
    "Malformed bytes are reported, not swallowed"
  );

  Assert.throws(
    () =>
      ContainersStore.init(
        bytes(JSON.stringify({ version: latestVersion() + 1, identities: [] })),
        null,
        new Recorder()
      ),
    e => e instanceof UnsupportedVersion,
    "A document from a newer Firefox is reported, not misparsed"
  );
});
