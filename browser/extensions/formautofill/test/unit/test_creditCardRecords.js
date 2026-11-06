/**
 * Tests FormAutofillStorage object with creditCards records.
 */

"use strict";

const { CreditCard } = ChromeUtils.importESModule(
  "resource://gre/modules/CreditCard.sys.mjs"
);

let FormAutofillStorage;
let CREDIT_CARD_SCHEMA_VERSION;
add_setup(async () => {
  ({ FormAutofillStorage } = ChromeUtils.importESModule(
    "resource://autofill/FormAutofillStorage.sys.mjs"
  ));
  ({ CREDIT_CARD_SCHEMA_VERSION } = ChromeUtils.importESModule(
    "resource://autofill/FormAutofillStorageBase.sys.mjs"
  ));
});

const TEST_STORE_FILE_NAME = "test-credit-card.json";
const COLLECTION_NAME = "creditCards";

const TEST_CREDIT_CARD_1 = {
  "cc-name": "John Doe",
  "cc-number": "4929001587121045",
  "cc-exp-month": 4,
  "cc-exp-year": 2017,
};

const TEST_CREDIT_CARD_2 = {
  "cc-name": "Timothy Berners-Lee",
  "cc-number": "5103059495477870",
  "cc-exp-month": 12,
  "cc-exp-year": 2022,
};

const TEST_CREDIT_CARD_3 = {
  "cc-number": "3589993783099582",
  "cc-exp-month": 1,
  "cc-exp-year": 2000,
};

const TEST_CREDIT_CARD_WITH_BILLING_ADDRESS = {
  "cc-name": "J. Smith",
  "cc-number": "4111111111111111",
  billingAddressGUID: "9m6hf4gfr6ge",
};

const TEST_CREDIT_CARD_WITH_EMPTY_FIELD = {
  billingAddressGUID: "",
  "cc-name": "",
  "cc-number": "344060747836806",
  "cc-exp-month": 1,
};

const TEST_CREDIT_CARD_WITH_EMPTY_COMPUTED_FIELD = {
  "cc-given-name": "",
  "cc-additional-name": "",
  "cc-family-name": "",
  "cc-exp": "",
  "cc-number": "5415425865751454",
};

const TEST_CREDIT_CARD_WITH_2_DIGITS_YEAR = {
  "cc-number": "344060747836806",
  "cc-exp-month": 1,
  "cc-exp-year": 12,
};

const TEST_CREDIT_CARD_WITH_INVALID_EXPIRY_DATE = {
  "cc-name": "John Doe",
  "cc-number": "5103059495477870",
  "cc-exp-month": 13,
  "cc-exp-year": -3,
};

const TEST_CREDIT_CARD_WITH_SPACES_BETWEEN_DIGITS = {
  "cc-name": "John Doe",
  "cc-number": "5103 0594 9547 7870",
};

const TEST_CREDIT_CARD_EMPTY_AFTER_NORMALIZE = {
  "cc-exp-month": 13,
};

const TEST_CREDIT_CARD_WITH_LONE_SURROGATE = {
  "cc-name": "John Doe\uD83D",
  "cc-number": "4929001587121045",
  "cc-exp-month": 4,
  "cc-exp-year": 2030,
};

const TEST_CREDIT_CARD_EMPTY_AFTER_UPDATE_CREDIT_CARD_1 = {
  "cc-name": "",
  "cc-number": "",
  "cc-exp-month": 13,
  "cc-exp-year": "",
};

let prepareTestCreditCards = async function (path) {
  let profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  let onChanged = TestUtils.topicObserved(
    "formautofill-storage-changed",
    (subject, data) =>
      data == "add" &&
      subject.wrappedJSObject.guid &&
      subject.wrappedJSObject.collectionName == COLLECTION_NAME
  );
  Assert.ok(await profileStorage.creditCards.add(TEST_CREDIT_CARD_1));
  await onChanged;
  Assert.ok(await profileStorage.creditCards.add(TEST_CREDIT_CARD_2));
  await onChanged;
  await profileStorage._saveImmediately();
};

let reCCNumber = /^(\•+)(.{4})$/;

let do_check_credit_card_matches = (creditCardWithMeta, creditCard) => {
  for (let key in creditCard) {
    if (key == "cc-number") {
      let matches = reCCNumber.exec(creditCardWithMeta["cc-number"]);
      Assert.notEqual(matches, null);
      Assert.equal(
        creditCardWithMeta["cc-number"].length,
        creditCard["cc-number"].length
      );
      Assert.equal(creditCard["cc-number"].endsWith(matches[2]), true);
      Assert.notEqual(creditCard["cc-number-encrypted"], "");
    } else {
      Assert.equal(creditCardWithMeta[key], creditCard[key], "Testing " + key);
    }
  }
};

add_task(async function test_initialize() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  let profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  Assert.equal(profileStorage._store.data.version, 1);
  Assert.equal(profileStorage._store.data.creditCards.length, 0);

  let data = profileStorage._store.data;
  Assert.deepEqual(data.creditCards, []);

  await profileStorage._saveImmediately();

  profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  Assert.deepEqual(profileStorage._store.data, data);
});

add_task(async function test_getAll() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  await prepareTestCreditCards(path);

  let profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  let creditCards = await profileStorage.creditCards.getAll();

  Assert.equal(creditCards.length, 2);
  do_check_credit_card_matches(creditCards[0], TEST_CREDIT_CARD_1);
  do_check_credit_card_matches(creditCards[1], TEST_CREDIT_CARD_2);

  // Check computed fields.
  Assert.equal(creditCards[0]["cc-given-name"], "John");
  Assert.equal(creditCards[0]["cc-family-name"], "Doe");
  Assert.equal(creditCards[0]["cc-exp"], "2017-04");

  // Test with rawData set.
  creditCards = await profileStorage.creditCards.getAll({ rawData: true });
  Assert.equal(creditCards[0]["cc-given-name"], undefined);
  Assert.equal(creditCards[0]["cc-family-name"], undefined);
  Assert.equal(creditCards[0]["cc-exp"], undefined);

  // Modifying output shouldn't affect the storage.
  creditCards[0]["cc-name"] = "test";
  do_check_credit_card_matches(
    (await profileStorage.creditCards.getAll())[0],
    TEST_CREDIT_CARD_1
  );
});

add_task(async function test_get() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  await prepareTestCreditCards(path);

  let profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  let creditCards = await profileStorage.creditCards.getAll();
  let guid = creditCards[0].guid;

  let creditCard = await profileStorage.creditCards.get(guid);
  do_check_credit_card_matches(creditCard, TEST_CREDIT_CARD_1);

  // Modifying output shouldn't affect the storage.
  creditCards[0]["cc-name"] = "test";
  do_check_credit_card_matches(
    await profileStorage.creditCards.get(guid),
    TEST_CREDIT_CARD_1
  );

  Assert.equal(await profileStorage.creditCards.get("INVALID_GUID"), null);
});

add_task(async function test_add() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  await prepareTestCreditCards(path);

  let profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  let creditCards = await profileStorage.creditCards.getAll();

  Assert.equal(creditCards.length, 2);

  do_check_credit_card_matches(creditCards[0], TEST_CREDIT_CARD_1);
  do_check_credit_card_matches(creditCards[1], TEST_CREDIT_CARD_2);

  Assert.notEqual(creditCards[0].guid, undefined);
  Assert.equal(creditCards[0].version, CREDIT_CARD_SCHEMA_VERSION);
  Assert.notEqual(creditCards[0].timeCreated, undefined);
  Assert.equal(creditCards[0].timeLastModified, creditCards[0].timeCreated);
  Assert.equal(creditCards[0].timeLastUsed, 0);
  Assert.equal(creditCards[0].timesUsed, 0);

  // Empty string should be deleted before saving.
  await profileStorage.creditCards.add(TEST_CREDIT_CARD_WITH_EMPTY_FIELD);
  let creditCard = profileStorage.creditCards._data[2];
  Assert.equal(
    creditCard["cc-exp-month"],
    TEST_CREDIT_CARD_WITH_EMPTY_FIELD["cc-exp-month"]
  );
  Assert.equal(creditCard["cc-name"], undefined);
  Assert.equal(creditCard.billingAddressGUID, undefined);

  // Empty computed fields shouldn't cause any problem.
  await profileStorage.creditCards.add(
    TEST_CREDIT_CARD_WITH_EMPTY_COMPUTED_FIELD
  );
  creditCard = profileStorage.creditCards._data[3];
  Assert.equal(
    creditCard["cc-number"],
    CreditCard.getLongMaskedNumber(
      TEST_CREDIT_CARD_WITH_EMPTY_COMPUTED_FIELD["cc-number"]
    )
  );

  await Assert.rejects(
    profileStorage.creditCards.add({}),
    /Record contains no valid field\./
  );

  await Assert.rejects(
    profileStorage.creditCards.add(TEST_CREDIT_CARD_EMPTY_AFTER_NORMALIZE),
    /Record contains no valid field\./
  );

  await Assert.rejects(
    profileStorage.creditCards.add(TEST_CREDIT_CARD_WITH_LONE_SURROGATE),
    /"cc-name" contains lone surrogates/
  );
});

add_task(async function test_addWithBillingAddress() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  let profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  let creditCards = await profileStorage.creditCards.getAll();

  Assert.equal(creditCards.length, 0);

  await profileStorage.creditCards.add(TEST_CREDIT_CARD_WITH_BILLING_ADDRESS);

  creditCards = await profileStorage.creditCards.getAll();
  Assert.equal(creditCards.length, 1);
  do_check_credit_card_matches(
    creditCards[0],
    TEST_CREDIT_CARD_WITH_BILLING_ADDRESS
  );
});

add_task(async function test_update() {
  // Test assumes that when an entry is saved a second time, it's last modified date will
  // be different from the first. With high values of precision reduction, we execute too
  // fast for that to be true.
  let timerPrecision = Services.prefs.getBoolPref(
    "privacy.reduceTimerPrecision"
  );
  Services.prefs.setBoolPref("privacy.reduceTimerPrecision", false);

  registerCleanupFunction(function () {
    Services.prefs.setBoolPref("privacy.reduceTimerPrecision", timerPrecision);
  });

  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  await prepareTestCreditCards(path);

  let profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  let creditCards = await profileStorage.creditCards.getAll();
  let guid = creditCards[1].guid;
  let timeLastModified = creditCards[1].timeLastModified;

  let onChanged = TestUtils.topicObserved(
    "formautofill-storage-changed",
    (subject, data) =>
      data == "update" &&
      subject.wrappedJSObject.guid == guid &&
      subject.wrappedJSObject.collectionName == COLLECTION_NAME
  );

  Assert.notEqual(creditCards[1]["cc-name"], undefined);
  await profileStorage.creditCards.update(guid, TEST_CREDIT_CARD_3);
  await onChanged;
  await profileStorage._saveImmediately();

  profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  let creditCard = await profileStorage.creditCards.get(guid);

  Assert.equal(creditCard["cc-name"], undefined);
  Assert.notEqual(creditCard.timeLastModified, timeLastModified);
  do_check_credit_card_matches(creditCard, TEST_CREDIT_CARD_3);

  // Empty string should be deleted while updating.
  await profileStorage.creditCards.update(
    profileStorage.creditCards._data[0].guid,
    TEST_CREDIT_CARD_WITH_EMPTY_FIELD
  );
  creditCard = profileStorage.creditCards._data[0];
  Assert.equal(
    creditCard["cc-exp-month"],
    TEST_CREDIT_CARD_WITH_EMPTY_FIELD["cc-exp-month"]
  );
  Assert.equal(creditCard["cc-name"], undefined);
  Assert.equal(creditCard["cc-type"], "amex");
  Assert.equal(creditCard.billingAddressGUID, undefined);

  // Empty computed fields shouldn't cause any problem.
  await profileStorage.creditCards.update(
    profileStorage.creditCards._data[0].guid,
    TEST_CREDIT_CARD_WITH_EMPTY_COMPUTED_FIELD,
    false
  );
  creditCard = profileStorage.creditCards._data[0];
  Assert.equal(
    creditCard["cc-number"],
    CreditCard.getLongMaskedNumber(
      TEST_CREDIT_CARD_WITH_EMPTY_COMPUTED_FIELD["cc-number"]
    )
  );
  await profileStorage.creditCards.update(
    profileStorage.creditCards._data[1].guid,
    TEST_CREDIT_CARD_WITH_EMPTY_COMPUTED_FIELD,
    true
  );
  creditCard = profileStorage.creditCards._data[1];
  Assert.equal(
    creditCard["cc-number"],
    CreditCard.getLongMaskedNumber(
      TEST_CREDIT_CARD_WITH_EMPTY_COMPUTED_FIELD["cc-number"]
    )
  );

  // Decryption failure of existing record should not prevent it from being updated.
  creditCard = profileStorage.creditCards._data[0];
  creditCard["cc-number-encrypted"] = "INVALID";
  await profileStorage.creditCards.update(
    profileStorage.creditCards._data[0].guid,
    TEST_CREDIT_CARD_WITH_EMPTY_COMPUTED_FIELD,
    false
  );
  creditCard = profileStorage.creditCards._data[0];
  Assert.equal(
    creditCard["cc-number"],
    CreditCard.getLongMaskedNumber(
      TEST_CREDIT_CARD_WITH_EMPTY_COMPUTED_FIELD["cc-number"]
    )
  );

  await Assert.rejects(
    profileStorage.creditCards.update("INVALID_GUID", TEST_CREDIT_CARD_3),
    /No matching record\./
  );

  await Assert.rejects(
    profileStorage.creditCards.update(guid, {}),
    /Record contains no valid field\./
  );

  await Assert.rejects(
    profileStorage.creditCards.update(
      guid,
      TEST_CREDIT_CARD_EMPTY_AFTER_NORMALIZE
    ),
    /Record contains no valid field\./
  );

  await Assert.rejects(
    profileStorage.creditCards.update(
      guid,
      TEST_CREDIT_CARD_WITH_LONE_SURROGATE
    ),
    /"cc-name" contains lone surrogates/
  );

  await profileStorage.creditCards.update(guid, TEST_CREDIT_CARD_1);
  await Assert.rejects(
    profileStorage.creditCards.update(
      guid,
      TEST_CREDIT_CARD_EMPTY_AFTER_UPDATE_CREDIT_CARD_1
    ),
    /Record contains no valid field\./
  );
});

add_task(async function test_validate() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;

  let profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  await profileStorage.creditCards.add(
    TEST_CREDIT_CARD_WITH_INVALID_EXPIRY_DATE
  );
  await profileStorage.creditCards.add(TEST_CREDIT_CARD_WITH_2_DIGITS_YEAR);
  await profileStorage.creditCards.add(
    TEST_CREDIT_CARD_WITH_SPACES_BETWEEN_DIGITS
  );
  let creditCards = await profileStorage.creditCards.getAll();

  Assert.equal(creditCards[0]["cc-exp-month"], undefined);
  Assert.equal(creditCards[0]["cc-exp-year"], undefined);
  Assert.equal(creditCards[0]["cc-exp"], undefined);

  let month = TEST_CREDIT_CARD_WITH_2_DIGITS_YEAR["cc-exp-month"];
  let year =
    parseInt(TEST_CREDIT_CARD_WITH_2_DIGITS_YEAR["cc-exp-year"], 10) + 2000;
  Assert.equal(creditCards[1]["cc-exp-month"], month);
  Assert.equal(creditCards[1]["cc-exp-year"], year);
  Assert.equal(
    creditCards[1]["cc-exp"],
    year + "-" + month.toString().padStart(2, "0")
  );

  Assert.equal(creditCards[2]["cc-number"].length, 16);
});

add_task(async function test_notifyUsed() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  await prepareTestCreditCards(path);

  let profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  let creditCards = await profileStorage.creditCards.getAll();
  let guid = creditCards[1].guid;
  let timeLastUsed = creditCards[1].timeLastUsed;
  let timesUsed = creditCards[1].timesUsed;

  let onChanged = TestUtils.topicObserved(
    "formautofill-storage-changed",
    (subject, data) =>
      data == "notifyUsed" &&
      subject.wrappedJSObject.collectionName == COLLECTION_NAME &&
      subject.wrappedJSObject.guid == guid
  );

  profileStorage.creditCards.notifyUsed(guid);
  await onChanged;
  await profileStorage._saveImmediately();

  profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  let creditCard = await profileStorage.creditCards.get(guid);

  Assert.equal(creditCard.timesUsed, timesUsed + 1);
  Assert.notEqual(creditCard.timeLastUsed, timeLastUsed);
});

add_task(async function test_remove() {
  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  await prepareTestCreditCards(path);

  let profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  let creditCards = await profileStorage.creditCards.getAll();
  let guid = creditCards[1].guid;

  let onChanged = TestUtils.topicObserved(
    "formautofill-storage-changed",
    (subject, data) =>
      data == "remove" &&
      subject.wrappedJSObject.guid == guid &&
      subject.wrappedJSObject.collectionName == COLLECTION_NAME
  );

  Assert.equal(creditCards.length, 2);

  profileStorage.creditCards.remove(guid);
  await onChanged;
  await profileStorage._saveImmediately();

  profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  creditCards = await profileStorage.creditCards.getAll();

  Assert.equal(creditCards.length, 1);

  Assert.equal(await profileStorage.creditCards.get(guid), null);
});

add_task(async function test_getDuplicateRecords() {
  let profileStorage = await initProfileStorage(
    TEST_STORE_FILE_NAME,
    [TEST_CREDIT_CARD_3],
    "creditCards"
  );
  let guid = profileStorage.creditCards._data[0].guid;

  // Absolutely a duplicate.
  let getDuplicateRecords =
    profileStorage.creditCards.getDuplicateRecords(TEST_CREDIT_CARD_3);
  let dupe = (await getDuplicateRecords.next()).value;
  Assert.equal(dupe.guid, guid);

  // Absolutely not a duplicate.
  getDuplicateRecords =
    profileStorage.creditCards.getDuplicateRecords(TEST_CREDIT_CARD_1);
  dupe = (await getDuplicateRecords.next()).value;
  Assert.equal(dupe, null);

  // Subset with the same number is a duplicate.
  let record = Object.assign({}, TEST_CREDIT_CARD_3);
  delete record["cc-exp-month"];
  getDuplicateRecords = profileStorage.creditCards.getDuplicateRecords(record);
  dupe = (await getDuplicateRecords.next()).value;
  Assert.equal(dupe.guid, guid);

  // Superset with the same number is a duplicate.
  record = Object.assign({}, TEST_CREDIT_CARD_3);
  record["cc-name"] = "John Doe";
  getDuplicateRecords = profileStorage.creditCards.getDuplicateRecords(record);
  dupe = (await getDuplicateRecords.next()).value;
  Assert.equal(dupe.guid, guid);

  // Numbers with the same last 4 digits shouldn't be treated as a duplicate.
  record = Object.assign({}, TEST_CREDIT_CARD_3);
  let last4Digits = record["cc-number"].substr(-4);
  getDuplicateRecords = profileStorage.creditCards.getDuplicateRecords(record);
  dupe = (await getDuplicateRecords.next()).value;
  Assert.equal(dupe.guid, guid);

  // This number differs from TEST_CREDIT_CARD_3 by swapping the order of the
  // 09 and 90 adjacent digits, which is still a valid credit card number.
  record["cc-number"] = "358999378390" + last4Digits;

  // We don't treat numbers with the same last 4 digits as a duplicate.
  getDuplicateRecords = profileStorage.creditCards.getDuplicateRecords(record);
  dupe = (await getDuplicateRecords.next()).value;
  Assert.equal(dupe, null);
});

add_task(async function test_getDuplicateRecordsMatch() {
  let profileStorage = await initProfileStorage(
    TEST_STORE_FILE_NAME,
    [TEST_CREDIT_CARD_2],
    "creditCards"
  );
  let guid = profileStorage.creditCards._data[0].guid;

  // Absolutely a duplicate.
  let getDuplicateRecords =
    profileStorage.creditCards.getDuplicateRecords(TEST_CREDIT_CARD_2);
  let dupe = (await getDuplicateRecords.next()).value;
  Assert.equal(dupe.guid, guid);

  // Absolutely not a duplicate.
  getDuplicateRecords =
    profileStorage.creditCards.getDuplicateRecords(TEST_CREDIT_CARD_1);
  dupe = (await getDuplicateRecords.next()).value;
  Assert.equal(dupe, null);

  record = Object.assign({}, TEST_CREDIT_CARD_2);

  // We change month from `1` to `2`
  record["cc-exp-month"] = 2;
  getDuplicateRecords = profileStorage.creditCards.getDuplicateRecords(record);
  dupe = (await getDuplicateRecords.next()).value;
  Assert.equal(dupe.guid, guid);

  // We change year from `2000` to `2001`
  record["cc-exp-year"] = 2001;
  getDuplicateRecords = profileStorage.creditCards.getDuplicateRecords(record);
  dupe = (await getDuplicateRecords.next()).value;
  Assert.equal(dupe.guid, guid);

  // New name, same card
  record["cc-name"] = "John Doe";
  getDuplicateRecords = profileStorage.creditCards.getDuplicateRecords(record);
  dupe = (await getDuplicateRecords.next()).value;
  Assert.equal(dupe.guid, guid);
});

add_task(async function test_getMatchRecord() {
  let profileStorage = await initProfileStorage(
    TEST_STORE_FILE_NAME,
    [TEST_CREDIT_CARD_2],
    "creditCards"
  );
  let guid = profileStorage.creditCards._data[0].guid;

  const TEST_FIELDS = {
    "cc-name": "John Doe",
    "cc-exp-month": 10,
    "cc-exp-year": 2001,
  };

  // Absolutely a match.
  let getMatchRecords =
    profileStorage.creditCards.getMatchRecords(TEST_CREDIT_CARD_2);
  let match = (await getMatchRecords.next()).value;
  Assert.equal(match.guid, guid);

  // Subset with the same number is a match.
  for (const field of Object.keys(TEST_FIELDS)) {
    let record = Object.assign({}, TEST_CREDIT_CARD_2);
    delete record[field];
    getMatchRecords = profileStorage.creditCards.getMatchRecords(record);
    match = (await getMatchRecords.next()).value;
    Assert.equal(match.guid, guid);
  }

  // Subset with different number is not a match.
  for (const field of Object.keys(TEST_FIELDS)) {
    let record = Object.assign({}, TEST_CREDIT_CARD_2, {
      "cc-number": TEST_CREDIT_CARD_1["cc-number"],
    });
    delete record[field];
    getMatchRecords = profileStorage.creditCards.getMatchRecords(record);
    match = (await getMatchRecords.next()).value;
    Assert.equal(match, null);
  }

  // Superset with the same number is not a match.
  for (const [field, value] of Object.entries(TEST_FIELDS)) {
    let record = Object.assign({}, TEST_CREDIT_CARD_2);
    record[field] = value;
    getMatchRecords = profileStorage.creditCards.getMatchRecords(record);
    match = (await getMatchRecords.next()).value;
    Assert.equal(match, null);
  }

  // Superset with different number is not a match.
  for (const [field, value] of Object.entries(TEST_FIELDS)) {
    let record = Object.assign({}, TEST_CREDIT_CARD_2, {
      "cc-number": TEST_CREDIT_CARD_1["cc-number"],
    });
    record[field] = value;
    getMatchRecords = profileStorage.creditCards.getMatchRecords(record);
    match = (await getMatchRecords.next()).value;
    Assert.equal(match, null);
  }
});

add_task(async function test_creditCardFillDisabled() {
  Services.prefs.setBoolPref(
    "extensions.formautofill.creditCards.enabled",
    false
  );

  let path = getTempFile(TEST_STORE_FILE_NAME).path;
  let profileStorage = new FormAutofillStorage(path);
  await profileStorage.initialize();

  Assert.equal(
    !!profileStorage.creditCards,
    true,
    "credit card records initialized and available."
  );

  Services.prefs.setBoolPref(
    "extensions.formautofill.creditCards.enabled",
    true
  );
});
