/*
 * Unit tests for RustAutofillStore: opening the profile's store, the shutdown
 * handling around it -- finalize() and the profile-teardown blocker registered
 * by _registerShutdownBlocker() -- and when its authenticator prompts for the
 * primary password.
 *
 * The shutdown tests exercise the logic in isolation with a fake store and a
 * fake AsyncShutdown phase. Only one test opens a real store.
 */

"use strict";

const { RustAutofillStore, RustAutofillStorageAuthenticator } =
  ChromeUtils.importESModule("resource://autofill/RustAutofillStore.sys.mjs");
const { AuthenticationCanceled } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustDbCrypto.sys.mjs"
);
const { UpdatableCreditCardFields } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/uniffi-bindgen-gecko-js/components/generated/RustAutofill.sys.mjs"
);

/**
 * Constructing normally hands back the shared instance, so drop it first to
 * get a handler with fresh state.
 *
 * @returns {RustAutofillStore}
 */
function makeHandler() {
  RustAutofillStore._instance = null;
  return new RustAutofillStore();
}

function makeFakeStore() {
  return {
    shutdownCount: 0,
    async shutdown() {
      this.shutdownCount++;
    },
  };
}

function makeFakePhase({ isClosed = false } = {}) {
  return {
    isClosed,
    blockers: [],
    addBlocker(name, callback) {
      this.blockers.push({ name, callback });
    },
  };
}

add_task(async function finalize_without_open_store_is_noop() {
  const handler = makeHandler();
  // The store was never opened, so this must resolve without doing anything.
  await handler.finalize();
  Assert.ok(true, "resolves when the store was never opened");
});

add_task(async function finalize_closes_open_store() {
  const handler = makeHandler();
  const store = makeFakeStore();
  handler._storePromise = Promise.resolve(store);

  await handler.finalize();
  Assert.equal(store.shutdownCount, 1, "shutdown() called once");
});

add_task(async function finalize_tolerates_failed_store_open() {
  const handler = makeHandler();
  handler._storePromise = Promise.reject(new Error("open failed"));

  // The store never opened, so there is nothing to close and finalize must not
  // reject -- otherwise the shutdown blocker would fail at teardown.
  await handler.finalize();
  Assert.ok(true, "resolves when the store failed to open");
});

add_task(async function constructing_returns_the_shared_instance() {
  const handler = makeHandler();
  Assert.equal(new RustAutofillStore(), handler, "the singleton is reused");
});

add_task(async function ensure_open_reuses_an_open_store() {
  const handler = makeHandler();
  const store = makeFakeStore();
  handler._storePromise = Promise.resolve(store);

  // Already open, so this must hand back the same store rather than opening a
  // second connection to the same file.
  Assert.equal(await handler.ensureOpen(), store, "the same store is used");
});

add_task(async function blocker_registers_while_phase_open() {
  const handler = makeHandler();
  const store = makeFakeStore();
  handler._storePromise = Promise.resolve(store);
  const phase = makeFakePhase();

  await handler._registerShutdownBlocker(phase);
  Assert.equal(phase.blockers.length, 1, "a blocker was registered");
  Assert.equal(typeof phase.blockers[0].name, "string", "blocker has a name");
  Assert.equal(
    store.shutdownCount,
    0,
    "store not closed until the blocker runs"
  );

  // Running the blocker closes the store.
  await phase.blockers[0].callback();
  Assert.equal(store.shutdownCount, 1, "running the blocker closes the store");
});

add_task(async function store_closes_immediately_when_phase_closed() {
  const handler = makeHandler();
  const store = makeFakeStore();
  handler._storePromise = Promise.resolve(store);
  const phase = makeFakePhase({ isClosed: true });
  phase.addBlocker = () => {
    Assert.ok(false, "addBlocker must not run once the phase is closed");
  };

  await handler._registerShutdownBlocker(phase);
  Assert.equal(store.shutdownCount, 1, "store closed immediately");
});

add_task(async function ensure_open_yields_a_store_that_holds_a_key() {
  // The profile's store gets its key from NSS rather than being handed one,
  // which takes NSS knowing the profile. Nothing else here opens a real store,
  // so this is what says the key arrives at all.
  const handler = makeHandler();
  const store = await handler.ensureOpen();

  const { guid } = await store.addCreditCard(
    new UpdatableCreditCardFields({
      ccName: "",
      ccNumber: "4111111111111111",
      ccExpMonth: 0,
      ccExpYear: 0,
      ccType: "",
    })
  );
  Assert.equal(
    (await store.getCreditCard(guid)).ccNumber,
    "4111111111111111",
    "the store encrypts with a key of its own and reads back what it wrote"
  );

  await store.deleteCreditCard(guid);
  await handler.finalize();
});

/**
 * An authenticator whose dialog answers from `answers` in turn, null being a
 * decline, and counts how often it was shown.
 *
 * @param {Array<?string>} answers
 */
function makeAuthenticator(answers) {
  const authenticator = new RustAutofillStorageAuthenticator();
  authenticator.shown = 0;
  authenticator._prompt = async () => {
    authenticator.shown++;
    return answers.shift() ?? null;
  };
  return authenticator;
}

add_task(async function a_read_nothing_asked_for_is_never_prompted() {
  // The store decrypts every card it reads and asks for the key for each, so
  // being asked says nothing about whether anyone wants a number.
  const authenticator = makeAuthenticator(["secret"]);
  await Assert.rejects(
    authenticator.getPrimaryPassword(),
    e => e instanceof AuthenticationCanceled,
    "a request outside allowingPrompt() is declined"
  );
  Assert.equal(authenticator.shown, 0, "without showing the dialog");
});

add_task(async function one_decline_answers_for_the_whole_operation() {
  const authenticator = makeAuthenticator([null, "secret"]);

  // As a read of several cards asks, once per card, while the token stays
  // locked: the first decline has to answer for the rest.
  await authenticator.allowingPrompt(async () => {
    for (let card = 0; card < 3; card++) {
      await Assert.rejects(
        authenticator.getPrimaryPassword(),
        e => e instanceof AuthenticationCanceled,
        `card ${card} is declined`
      );
    }
  });
  Assert.equal(authenticator.shown, 1, "and the dialog was shown once");

  // The next operation is a new request, and is asked again.
  const password = await authenticator.allowingPrompt(() =>
    authenticator.getPrimaryPassword()
  );
  Assert.equal(password, "secret", "a later operation prompts afresh");
  Assert.equal(authenticator.shown, 2, "showing the dialog once more");
});
