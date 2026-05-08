"use strict";

const { FormAutofill } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofill.sys.mjs"
);
const { FormAutofillParent } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofillParent.sys.mjs"
);
const { FormAutofillUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/shared/FormAutofillUtils.sys.mjs"
);
const { OSKeyStoreTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/OSKeyStoreTestUtils.sys.mjs"
);

let { formAutofillStorage } = ChromeUtils.importESModule(
  "resource://autofill/FormAutofillStorage.sys.mjs"
);

const { ADDRESSES_COLLECTION_NAME, CREDITCARDS_COLLECTION_NAME } =
  FormAutofillUtils;

let destroyed = false;

var ParentUtils = {
  getFormAutofillActor() {
    let win = Services.wm.getMostRecentWindow("navigator:browser");
    let selectedBrowser = win.gBrowser.selectedBrowser;
    return selectedBrowser.browsingContext.currentWindowGlobal.getActor(
      "FormAutofill"
    );
  },

  _getRecords(collectionName) {
    return this.getFormAutofillActor()
      .receiveMessage({
        name: "FormAutofill:GetRecords",
        data: {
          searchString: "",
          collectionName,
        },
      })
      .then(result => result.records);
  },

  async _storageChangeObserved({
    topic = "formautofill-storage-changed",
    type,
    times = 1,
  }) {
    let count = times;

    return new Promise(resolve => {
      Services.obs.addObserver(function observer(subject, obsTopic, data) {
        if ((type && data != type) || !!--count) {
          return;
        }

        // every notification type should have the collection name.
        // We're not allowed to trigger assertions during mochitest
        // cleanup functions.
        if (!destroyed) {
          let allowedNames = [
            ADDRESSES_COLLECTION_NAME,
            CREDITCARDS_COLLECTION_NAME,
          ];
          assert.ok(
            allowedNames.includes(subject.wrappedJSObject.collectionName),
            "should include the collection name"
          );
          // every notification except removeAll should have a guid.
          if (data != "removeAll") {
            assert.ok(subject.wrappedJSObject.guid, "should have a guid");
          }
        }
        Services.obs.removeObserver(observer, obsTopic);
        resolve();
      }, topic);
    });
  },

  async _operateRecord(collectionName, type, msgData) {
    let msgName, times, topic;

    if (collectionName == ADDRESSES_COLLECTION_NAME) {
      switch (type) {
        case "add": {
          msgName = "FormAutofill:SaveAddress";
          break;
        }
        case "update": {
          msgName = "FormAutofill:SaveAddress";
          break;
        }
        case "remove": {
          msgName = "FormAutofill:RemoveAddresses";
          times = msgData.guids.length;
          break;
        }
        default:
          return;
      }
    } else {
      switch (type) {
        case "add": {
          msgData = Object.assign({}, msgData);
          msgName = "FormAutofill:SaveCreditCard";
          break;
        }
        case "remove": {
          msgName = "FormAutofill:RemoveCreditCards";
          times = msgData.guids.length;
          break;
        }
        default:
          return;
      }
    }

    let storageChangePromise = this._storageChangeObserved({
      type,
      times,
      topic,
    });
    this.getFormAutofillActor().receiveMessage({
      name: msgName,
      data: msgData,
    });
    await storageChangePromise;
  },

  async operateAddress(type, msgData) {
    await this._operateRecord(ADDRESSES_COLLECTION_NAME, type, msgData);
  },

  async operateCreditCard(type, msgData) {
    await this._operateRecord(CREDITCARDS_COLLECTION_NAME, type, msgData);
  },

  async cleanUpAddresses() {
    const guids = (await this._getRecords(ADDRESSES_COLLECTION_NAME)).map(
      record => record.guid
    );

    if (!guids.length) {
      return;
    }

    await this.operateAddress("remove", { guids });
  },

  async cleanUpCreditCards() {
    if (!FormAutofill.isAutofillCreditCardsAvailable) {
      return;
    }
    const guids = (await this._getRecords(CREDITCARDS_COLLECTION_NAME)).map(
      record => record.guid
    );

    if (!guids.length) {
      return;
    }

    await this.operateCreditCard("remove", { guids });
  },

  setup() {
    OSKeyStoreTestUtils.setup();
  },

  async cleanup() {
    await this.cleanUpAddresses();
    await this.cleanUpCreditCards();
    await OSKeyStoreTestUtils.cleanup();

    Services.obs.removeObserver(this, "formautofill-storage-changed");
  },

  _areRecordsMatching(recordA, recordB, collectionName) {
    for (let field of formAutofillStorage[collectionName].VALID_FIELDS) {
      if (recordA[field] !== recordB[field]) {
        return false;
      }
    }
    // Check the internal field if both addresses have valid value.
    for (let field of formAutofillStorage.INTERNAL_FIELDS) {
      if (
        field in recordA &&
        field in recordB &&
        recordA[field] !== recordB[field]
      ) {
        return false;
      }
    }
    return true;
  },

  async _checkRecords(collectionName, expectedRecords) {
    const records = await this._getRecords(collectionName);

    if (records.length !== expectedRecords.length) {
      return false;
    }

    for (let record of records) {
      let matching = expectedRecords.some(expectedRecord => {
        return ParentUtils._areRecordsMatching(
          record,
          expectedRecord,
          collectionName
        );
      });

      if (!matching) {
        return false;
      }
    }

    return true;
  },

  async checkAddresses({ expectedAddresses }) {
    return this._checkRecords(ADDRESSES_COLLECTION_NAME, expectedAddresses);
  },

  async checkCreditCards({ expectedCreditCards }) {
    return this._checkRecords(CREDITCARDS_COLLECTION_NAME, expectedCreditCards);
  },

  observe(subject, topic, data) {
    if (!destroyed) {
      assert.ok(topic === "formautofill-storage-changed");
    }
    sendAsyncMessage("formautofill-storage-changed", {
      subject: null,
      topic,
      data,
    });
  },
};

Services.obs.addObserver(ParentUtils, "formautofill-storage-changed");

FormAutofillParent.addMessageObserver({
  fieldsIdentified() {
    sendAsyncMessage("FormAutofillTest:FieldsIdentified");
  },
});

addMessageListener("FormAutofillTest:AddAddress", msg => {
  return ParentUtils.operateAddress("add", msg);
});

addMessageListener("FormAutofillTest:RemoveAddress", msg => {
  return ParentUtils.operateAddress("remove", msg);
});

addMessageListener("FormAutofillTest:UpdateAddress", msg => {
  return ParentUtils.operateAddress("update", msg);
});

addMessageListener("FormAutofillTest:CheckAddresses", msg => {
  return ParentUtils.checkAddresses(msg);
});

addMessageListener("FormAutofillTest:CleanUpAddresses", _msg => {
  return ParentUtils.cleanUpAddresses();
});

addMessageListener("FormAutofillTest:AddCreditCard", msg => {
  return ParentUtils.operateCreditCard("add", msg);
});

addMessageListener("FormAutofillTest:RemoveCreditCard", msg => {
  return ParentUtils.operateCreditCard("remove", msg);
});

addMessageListener("FormAutofillTest:CheckCreditCards", msg => {
  return ParentUtils.checkCreditCards(msg);
});

addMessageListener("FormAutofillTest:CleanUpCreditCards", _msg => {
  return ParentUtils.cleanUpCreditCards();
});

addMessageListener("FormAutofillTest:CanTestOSKeyStoreLogin", _msg => {
  return { canTest: OSKeyStoreTestUtils.canTestOSKeyStoreLogin() };
});

addMessageListener("FormAutofillTest:OSKeyStoreLogin", msg => {
  OSKeyStoreTestUtils.waitForOSKeyStoreLogin(msg.login);
});

addMessageListener("setup", async _msg => ParentUtils.setup());

addMessageListener("cleanup", async _msg => {
  destroyed = true;
  await ParentUtils.cleanup();
});
