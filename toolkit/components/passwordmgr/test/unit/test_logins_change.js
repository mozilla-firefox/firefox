/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests methods that add, remove, and modify logins.
 */

"use strict";

// Globals

const MAX_DATE_MS = 8640000000000000;

/**
 * Verifies that the specified login is considered invalid by addLogin and by
 * modifyLogin with both nsILoginInfo and nsIPropertyBag arguments.
 *
 * This test requires that the login store is empty.
 *
 * @param aLoginInfo
 *        nsILoginInfo corresponding to an invalid login.
 * @param aExpectedError
 *        This argument is passed to the "Assert.throws" test to determine which
 *        error is expected from the modification functions.
 */
async function checkLoginInvalid(aLoginInfo, aExpectedError) {
  // Try to add the new login, and verify that no data is stored.
  await Assert.rejects(
    Services.logins.addLoginAsync(aLoginInfo),
    aExpectedError
  );
  await LoginTestUtils.checkLogins([]);

  // Add a login for the modification tests.
  let testLogin = TestData.formLogin({ origin: "http://modify.example.com" });
  testLogin = await Services.logins.addLoginAsync(testLogin);

  // Try to modify the existing login using nsILoginInfo and nsIPropertyBag.
  await Assert.rejects(
    Services.logins.modifyLoginAsync(testLogin, aLoginInfo),
    aExpectedError
  );
  await Assert.rejects(
    Services.logins.modifyLoginAsync(
      testLogin,
      newPropertyBag({
        origin: aLoginInfo.origin,
        formActionOrigin: aLoginInfo.formActionOrigin,
        httpRealm: aLoginInfo.httpRealm,
        username: aLoginInfo.username,
        password: aLoginInfo.password,
        usernameField: aLoginInfo.usernameField,
        passwordField: aLoginInfo.passwordField,
      })
    ),
    aExpectedError
  );

  // Verify that no data was stored by the previous calls.
  await LoginTestUtils.checkLogins([testLogin]);
  await Services.logins.removeLoginAsync(testLogin);
}

/**
 * Verifies that two objects are not the same instance
 * but have equal attributes.
 *
 * @param {object} objectA
 *        An object to compare.
 *
 * @param {object} objectB
 *        Another object to compare.
 *
 * @param {string[]} attributes
 *        Attributes to compare.
 *
 * @return true if all passed attributes are equal for both objects, false otherwise.
 */
function compareAttributes(objectA, objectB, attributes) {
  // If it's the same object, we want to return false.
  if (objectA == objectB) {
    return false;
  }
  return attributes.every(attr => objectA[attr] == objectB[attr]);
}

// Tests

/**
 * Tests that adding logins to the database works.
 */
add_task(async function test_addLogin_removeLogin() {
  // Each login from the test data should be valid and added to the list.
  const addedLogins = await Services.logins.addLogins(TestData.loginList());
  await LoginTestUtils.checkLogins(TestData.loginList());

  // Trying to add each login again should result in an error.
  for (let loginInfo of TestData.loginList()) {
    await Assert.rejects(
      Services.logins.addLoginAsync(loginInfo),
      /already exists/
    );
  }

  // Removing each login should succeed.
  for (let loginInfo of addedLogins) {
    await Services.logins.removeLoginAsync(loginInfo);
  }

  await LoginTestUtils.checkLogins([]);
});

add_task(async function add_login_works_with_empty_array() {
  const result = await Services.logins.addLogins([]);
  Assert.equal(result.length, 0, "no logins added");
});

add_task(async function duplicated_logins_are_not_added() {
  const login = TestData.formLogin({
    username: "user",
  });
  await Services.logins.addLogins([login]);
  const result = await Services.logins.addLogins([login]);
  Assert.equal(result, 0, "no logins added");
  await Services.logins.removeAllUserFacingLoginsAsync();
});

add_task(async function logins_containing_nul_in_username_are_not_added() {
  const result = await Services.logins.addLogins([
    TestData.formLogin({ username: "user\0name" }),
  ]);
  Assert.equal(result, 0, "no logins added");
});

add_task(async function logins_containing_nul_in_password_are_not_added() {
  const result = await Services.logins.addLogins([
    TestData.formLogin({ password: "pass\0word" }),
  ]);
  Assert.equal(result, 0, "no logins added");
});

add_task(
  async function return_value_includes_plaintext_username_and_password() {
    const login = TestData.formLogin({});
    const [result] = await Services.logins.addLogins([login]);
    Assert.equal(result.username, login.username, "plaintext username is set");
    Assert.equal(result.password, login.password, "plaintext password is set");
    await Services.logins.removeAllUserFacingLoginsAsync();
  }
);

add_task(async function event_data_includes_plaintext_username_and_password() {
  const login = TestData.formLogin({});
  const TestObserver = {
    QueryInterface: ChromeUtils.generateQI([
      "nsIObserver",
      "nsISupportsWeakReference",
    ]),
    observe(subject, _topic, _data) {
      Assert.ok(subject instanceof Ci.nsILoginInfo);
      Assert.ok(subject instanceof Ci.nsILoginMetaInfo);
      Assert.equal(
        subject.username,
        login.username,
        "plaintext username is set"
      );
      Assert.equal(
        subject.password,
        login.password,
        "plaintext password is set"
      );
    },
  };
  Services.obs.addObserver(TestObserver, "passwordmgr-storage-changed");
  await Services.logins.addLogins([login]);
  Services.obs.removeObserver(TestObserver, "passwordmgr-storage-changed");
  await Services.logins.removeAllUserFacingLoginsAsync();
});

/**
 * Tests invalid combinations of httpRealm and formActionOrigin.
 *
 * For an nsILoginInfo to be valid for storage, one of the two properties should
 * be strictly equal to null, and the other must not be null.
 *
 * The legacy case of an empty string in formActionOrigin and a null value in
 * httpRealm is also supported for storage at the moment.
 */
add_task(async function test_invalid_httpRealm_formActionOrigin() {
  // httpRealm === null, formActionOrigin === null
  await checkLoginInvalid(
    TestData.formLogin({ formActionOrigin: null }),
    /without a httpRealm or formActionOrigin/
  );

  // httpRealm === null, formActionOrigin === ""
  // TODO: This is not enforced for now.
  // await checkLoginInvalid(TestData.formLogin({ formActionOrigin: "" }),
  //                   /without a httpRealm or formActionOrigin/);

  // httpRealm === "", formActionOrigin === ""
  let login = TestData.formLogin({ formActionOrigin: "" });
  login.httpRealm = "";
  await checkLoginInvalid(login, /both a httpRealm and formActionOrigin/);

  // !!httpRealm, !!formActionOrigin
  login = TestData.formLogin();
  login.httpRealm = "The HTTP Realm";
  await checkLoginInvalid(login, /both a httpRealm and formActionOrigin/);

  // httpRealm === "", !!formActionOrigin
  login = TestData.formLogin();
  login.httpRealm = "";
  await checkLoginInvalid(login, /both a httpRealm and formActionOrigin/);

  // !!httpRealm, formActionOrigin === ""
  login = TestData.authLogin();
  login.formActionOrigin = "";
  await checkLoginInvalid(login, /both a httpRealm and formActionOrigin/);
});

/**
 * Tests null or empty values in required login properties.
 */
add_task(async function test_missing_properties() {
  await checkLoginInvalid(
    TestData.formLogin({ origin: null }),
    /null or empty origin/
  );

  await checkLoginInvalid(
    TestData.formLogin({ origin: "" }),
    /null or empty origin/
  );

  await checkLoginInvalid(
    TestData.formLogin({ username: null }),
    /null username/
  );

  await checkLoginInvalid(
    TestData.formLogin({ password: null }),
    /null or empty password/
  );

  await checkLoginInvalid(
    TestData.formLogin({ password: "" }),
    /null or empty password/
  );
});

/**
 * Tests invalid NUL characters in nsILoginInfo properties.
 */
add_task(async function test_invalid_characters() {
  let loginList = [
    TestData.authLogin({ origin: "http://null\0X.example.com" }),
    TestData.authLogin({ httpRealm: "realm\0" }),
    TestData.formLogin({ formActionOrigin: "http://null\0X.example.com" }),
    TestData.formLogin({ usernameField: "field\0_null" }),
    TestData.formLogin({ usernameField: ".\0" }), // Special single dot case
    TestData.formLogin({ passwordField: "field\0_null" }),
    TestData.formLogin({ username: "user\0name" }),
    TestData.formLogin({ password: "pass\0word" }),
  ];
  for (let loginInfo of loginList) {
    await checkLoginInvalid(loginInfo, /login values can't contain nulls/);
  }
});

/**
 * Tests lone surrogates, which can come from web form fields, in nsILoginInfo
 * properties.
 */
add_task(async function test_lone_surrogates() {
  let loginList = [
    TestData.authLogin({ origin: "http://lone\uD83D.example.com" }),
    TestData.authLogin({ httpRealm: "realm\uDE00" }),
    TestData.formLogin({ formActionOrigin: "http://lone\uD83D.example.com" }),
    TestData.formLogin({ usernameField: "field\uD83D" }),
    TestData.formLogin({ passwordField: "\uDE00field" }),
    TestData.formLogin({ username: "Foobar\uD83D" }),
    TestData.formLogin({ password: "\uDE00MyVeryCoolPassword" }),
  ];
  for (let loginInfo of loginList) {
    await checkLoginInvalid(
      loginInfo,
      /login values can't contain lone surrogates/
    );
  }
});

/**
 * Tests removing a login that does not exists.
 */
add_task(async function test_removeLogin_nonexisting() {
  const nonExistentLogin = TestData.formLogin();
  nonExistentLogin.QueryInterface(Ci.nsILoginMetaInfo).guid = Services.uuid
    .generateUUID()
    .toString();
  await Assert.rejects(
    Services.logins.removeLoginAsync(nonExistentLogin),
    /No matching logins/
  );
});

/**
 * Tests that removing a login requires the credentials to match, and does not
 * fall back to a stored login with an empty username.
 */
add_task(async function test_removeLogin_requiresMatchingCredentials() {
  const loginInfo = await Services.logins.addLoginAsync(TestData.formLogin());

  const wrongPassword = loginInfo.clone();
  wrongPassword.password = "not the stored password";
  await Assert.rejects(
    Services.logins.removeLoginAsync(wrongPassword),
    /No matching logins/
  );
  await LoginTestUtils.checkLogins([loginInfo]);

  await LoginTestUtils.clearData();

  const emptyUsernameLogin = await Services.logins.addLoginAsync(
    TestData.formLogin({ username: "" })
  );
  await Assert.rejects(
    Services.logins.removeLoginAsync(TestData.formLogin()),
    /No matching logins/
  );
  await LoginTestUtils.checkLogins([emptyUsernameLogin]);

  await LoginTestUtils.clearData();
});

/**
 * Tests that modifying a login requires the password of the login to modify to
 * match the stored one.
 */
add_task(async function test_modifyLogin_requiresMatchingCredentials() {
  const loginInfo = await Services.logins.addLoginAsync(TestData.formLogin());

  const wrongPassword = loginInfo.clone();
  wrongPassword.password = "not the stored password";
  await Assert.rejects(
    Services.logins.modifyLoginAsync(
      wrongPassword,
      newPropertyBag({ password: "the new password" })
    ),
    /No matching logins/
  );
  await LoginTestUtils.checkLogins([loginInfo]);

  await LoginTestUtils.clearData();
});

/**
 * Tests that a login can still be removed when the storage backend normalized
 * a field on write, so the caller's login is not field-for-field identical to
 * the stored one.
 */
add_task(async function test_removeLogin_normalizedOrigin() {
  const loginInfo = TestData.formLogin({
    origin: "https://example.org",
    formActionOrigin: "https://example.org/",
  });
  await Services.logins.addLoginAsync(loginInfo);

  await Services.logins.removeLoginAsync(loginInfo);
  await LoginTestUtils.checkLogins([]);
});

/**
 * Tests removing all logins at once.
 */
add_task(async function test_removeAllUserFacingLogins() {
  await Services.logins.addLogins(TestData.loginList());

  await Services.logins.removeAllUserFacingLoginsAsync();
  await LoginTestUtils.checkLogins([]);

  // The function should also work when there are no logins to delete.
  await Services.logins.removeAllUserFacingLoginsAsync();
});

/**
 * Tests the modifyLogin function with an nsILoginInfo argument.
 */
add_task(async function test_modifyLogin_nsILoginInfo() {
  const loginInfo = TestData.formLogin();
  const updatedLoginInfo = TestData.formLogin({
    username: "new username",
    password: "new password",
    usernameField: "new_form_field_username",
    passwordField: "new_form_field_password",
  });
  const differentLoginInfo = TestData.authLogin();

  // Trying to modify a login that does not exist should throw.
  // Use a login with a non-existent GUID to work with both JSON and Rust.
  const nonExistentLogin = loginInfo.clone();
  nonExistentLogin.QueryInterface(Ci.nsILoginMetaInfo).guid = Services.uuid
    .generateUUID()
    .toString();
  await Assert.rejects(
    Services.logins.modifyLoginAsync(nonExistentLogin, updatedLoginInfo),
    /No matching logins/
  );

  // Add the first form login, then modify it to match the second.
  let storedLogin = await Services.logins.addLoginAsync(loginInfo);
  await Services.logins.modifyLoginAsync(storedLogin, updatedLoginInfo);

  // The data should now match the second login.
  await LoginTestUtils.checkLogins([updatedLoginInfo]);
  await Assert.rejects(
    Services.logins.modifyLoginAsync(nonExistentLogin, updatedLoginInfo),
    /No matching logins/
  );

  // Get the stored login to use for further modifications.
  [storedLogin] = await Services.logins.getAllLogins();

  // The login can be changed to have a different type and origin.
  await Services.logins.modifyLoginAsync(storedLogin, differentLoginInfo);
  await LoginTestUtils.checkLogins([differentLoginInfo]);

  // It is now possible to add a login with the old type and origin.
  await Services.logins.addLoginAsync(loginInfo);
  await LoginTestUtils.checkLogins([loginInfo, differentLoginInfo]);

  // Modifying a login to match an existing one should not be possible.
  // Find the stored login that matches loginInfo's content.
  const storedLogins = await Services.logins.getAllLogins();
  const storedLoginInfo = storedLogins.find(l => l.equals(loginInfo));
  await Assert.rejects(
    Services.logins.modifyLoginAsync(storedLoginInfo, differentLoginInfo),
    /already exists/
  );
  await LoginTestUtils.checkLogins([loginInfo, differentLoginInfo]);

  LoginTestUtils.clearData();
});

/**
 * Tests the modifyLogin function with an nsIPropertyBag argument.
 */
add_task(async function test_modifyLogin_nsIProperyBag() {
  const loginInfo = TestData.formLogin();
  const updatedLoginInfo = TestData.formLogin({
    username: "new username",
    password: "new password",
    usernameField: "",
    passwordField: "new_form_field_password",
  });
  const differentLoginInfo = TestData.authLogin();
  const differentLoginProperties = newPropertyBag({
    origin: differentLoginInfo.origin,
    formActionOrigin: differentLoginInfo.formActionOrigin,
    httpRealm: differentLoginInfo.httpRealm,
    username: differentLoginInfo.username,
    password: differentLoginInfo.password,
    usernameField: differentLoginInfo.usernameField,
    passwordField: differentLoginInfo.passwordField,
  });

  // Trying to modify a login that does not exist should throw.
  const nonExistentLogin = loginInfo.clone();
  nonExistentLogin.QueryInterface(Ci.nsILoginMetaInfo).guid = Services.uuid
    .generateUUID()
    .toString();
  await Assert.rejects(
    Services.logins.modifyLoginAsync(nonExistentLogin, newPropertyBag()),
    /No matching logins/
  );

  // Add the first form login, then modify it to match the second.
  let storedLogin = await Services.logins.addLoginAsync(loginInfo);
  await Services.logins.modifyLoginAsync(
    storedLogin,
    newPropertyBag({
      username: "new username",
      password: "new password",
      usernameField: "",
      passwordField: "new_form_field_password",
    })
  );

  // The data should now match the second login.
  await LoginTestUtils.checkLogins([updatedLoginInfo]);
  await Assert.rejects(
    Services.logins.modifyLoginAsync(nonExistentLogin, newPropertyBag()),
    /No matching logins/
  );

  // Get the stored login (now has updatedLoginInfo content + storedLogin.guid).
  [storedLogin] = await Services.logins.getAllLogins();

  // It is also possible to provide no properties to be modified.
  await Services.logins.modifyLoginAsync(storedLogin, newPropertyBag());

  // Specifying a null property for a required value should throw.
  await Assert.rejects(
    Services.logins.modifyLoginAsync(
      nonExistentLogin,
      newPropertyBag({ usernameField: null })
    ),
    /No matching logins/
  );

  // The login can be changed to have a different type and origin.
  await Services.logins.modifyLoginAsync(storedLogin, differentLoginProperties);
  await LoginTestUtils.checkLogins([differentLoginInfo]);

  // It is now possible to add a login with the old type and origin.
  await Services.logins.addLoginAsync(loginInfo);
  await LoginTestUtils.checkLogins([loginInfo, differentLoginInfo]);

  // Modifying a login to match an existing one should not be possible.
  const storedLogins = await Services.logins.getAllLogins();
  const storedLoginInfo = storedLogins.find(l => l.equals(loginInfo));
  await Assert.rejects(
    Services.logins.modifyLoginAsync(storedLoginInfo, differentLoginProperties),
    /already exists/
  );
  await LoginTestUtils.checkLogins([loginInfo, differentLoginInfo]);

  LoginTestUtils.clearData();
});

/**
 * Tests the login deduplication function.
 */
add_task(function test_deduplicate_logins() {
  // Different key attributes combinations and the amount of unique
  // results expected for the TestData login list.
  let keyCombinations = [
    {
      keyset: ["username", "password"],
      results: 17,
    },
    {
      keyset: ["origin", "username"],
      results: 21,
    },
    {
      keyset: ["origin", "username", "password"],
      results: 22,
    },
    {
      keyset: ["origin", "username", "password", "formActionOrigin"],
      results: 27,
    },
  ];

  let logins = TestData.loginList();

  for (let testCase of keyCombinations) {
    // Deduplicate the logins using the current testcase keyset.
    let deduped = LoginHelper.dedupeLogins(logins, testCase.keyset);
    Assert.equal(
      deduped.length,
      testCase.results,
      "Correct amount of results."
    );

    // Checks that every login after deduping is unique.
    Assert.ok(
      deduped.every(loginA =>
        deduped.every(
          loginB => !compareAttributes(loginA, loginB, testCase.keyset)
        )
      ),
      "Every login is unique."
    );
  }
});

/**
 * Ensure that the login deduplication function keeps the most recent login.
 */
add_task(function test_deduplicate_keeps_most_recent() {
  // Logins to deduplicate.
  let logins = [
    TestData.formLogin({ timeLastUsed: Date.UTC(2004, 11, 4, 0, 0, 0) }),
    TestData.formLogin({
      formActionOrigin: "http://example.com",
      timeLastUsed: Date.UTC(2015, 11, 4, 0, 0, 0),
    }),
  ];

  // Deduplicate the logins.
  let deduped = LoginHelper.dedupeLogins(logins);
  Assert.equal(deduped.length, 1, "Deduplicated the logins array.");

  // Verify that the remaining login have the most recent date.
  let loginTimeLastUsed = deduped[0].QueryInterface(
    Ci.nsILoginMetaInfo
  ).timeLastUsed;
  Assert.equal(
    loginTimeLastUsed,
    Date.UTC(2015, 11, 4, 0, 0, 0),
    "Most recent login was kept."
  );

  // Deduplicate the reverse logins array.
  deduped = LoginHelper.dedupeLogins(logins.reverse());
  Assert.equal(deduped.length, 1, "Deduplicated the reversed logins array.");

  // Verify that the remaining login have the most recent date.
  loginTimeLastUsed = deduped[0].QueryInterface(
    Ci.nsILoginMetaInfo
  ).timeLastUsed;
  Assert.equal(
    loginTimeLastUsed,
    Date.UTC(2015, 11, 4, 0, 0, 0),
    "Most recent login was kept."
  );
});

/**
 * Tests handling when adding a login with bad date values
 */
add_task(async function test_addLogin_badDates() {
  LoginTestUtils.clearData();

  let now = Date.now();
  let defaultLoginDates = {
    timeCreated: now,
    timeLastUsed: now,
    timePasswordChanged: now,
  };

  let defaultsLogin = TestData.formLogin();
  for (let pname of ["timeCreated", "timeLastUsed", "timePasswordChanged"]) {
    Assert.ok(!defaultsLogin[pname]);
  }
  Assert.ok(
    !!(await Services.logins.addLoginAsync(defaultsLogin)),
    "Sanity check adding defaults formLogin"
  );
  await Services.logins.removeAllUserFacingLoginsAsync();

  // 0 is a valid date in this context - new nsLoginInfo timestamps init to 0
  for (let pname of ["timeCreated", "timeLastUsed", "timePasswordChanged"]) {
    let loginInfo = TestData.formLogin(
      Object.assign({}, defaultLoginDates, {
        [pname]: 0,
      })
    );
    Assert.ok(
      !!(await Services.logins.addLoginAsync(loginInfo)),
      "Check 0 value for " + pname
    );
    await Services.logins.removeAllUserFacingLoginsAsync();
  }

  // negative dates get clamped to 0 and are ok
  for (let pname of ["timeCreated", "timeLastUsed", "timePasswordChanged"]) {
    let loginInfo = TestData.formLogin(
      Object.assign({}, defaultLoginDates, {
        [pname]: -1,
      })
    );
    Assert.ok(
      !!(await Services.logins.addLoginAsync(loginInfo)),
      "Check -1 value for " + pname
    );
    await Services.logins.removeAllUserFacingLoginsAsync();
  }

  // out-of-range dates will throw
  for (let pname of ["timeCreated", "timeLastUsed", "timePasswordChanged"]) {
    let loginInfo = TestData.formLogin(
      Object.assign({}, defaultLoginDates, {
        [pname]: MAX_DATE_MS + 1,
      })
    );
    await Assert.rejects(
      Services.logins.addLoginAsync(loginInfo),
      /invalid date properties/
    );
    Assert.equal((await Services.logins.getAllLogins()).length, 0);
  }

  await LoginTestUtils.checkLogins([]);
});

/**
 * Tests handling when adding multiple logins with bad date values
 */
add_task(async function test_addLogins_badDates() {
  LoginTestUtils.clearData();

  let defaultsLogin = TestData.formLogin({
    username: "defaults",
  });
  await Services.logins.addLoginAsync(defaultsLogin);

  // -11644473600000 is the value you get if you convert Dec 31 1600 16:07:02 to unix epoch time
  let timeCreatedLogin = TestData.formLogin({
    username: "tc",
    timeCreated: -11644473600000,
  });
  await Assert.rejects(
    Services.logins.addLoginAsync(timeCreatedLogin),
    /Can\'t add a login with invalid date properties./
  );

  let timeLastUsedLogin = TestData.formLogin({
    username: "tlu",
    timeLastUsed: -11644473600000,
  });
  await Assert.rejects(
    Services.logins.addLoginAsync(timeLastUsedLogin),
    /Can\'t add a login with invalid date properties./
  );

  let timePasswordChangedLogin = TestData.formLogin({
    username: "tpc",
    timePasswordChanged: -11644473600000,
  });
  await Assert.rejects(
    Services.logins.addLoginAsync(timePasswordChangedLogin),
    /Can\'t add a login with invalid date properties./
  );

  await Services.logins.removeAllUserFacingLoginsAsync();
});
