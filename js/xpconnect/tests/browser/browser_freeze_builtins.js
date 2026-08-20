/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

function checkCtor(global, name, description) {
  ok(Object.isFrozen(global[name]), `${description} ${name} is frozen`);
  ok(
    Object.isSealed(global[name].prototype),
    `${description} ${name}.prototype is sealed`
  );

  let descr = Object.getOwnPropertyDescriptor(global, name);
  ok(!descr.configurable, `${description} ${name} should be non-configurable`);
  ok(!descr.writable, `${description} ${name} should not be writable`);
}

function checkGlobal(global, description) {
  checkCtor(global, "Object", description);
  checkCtor(global, "Array", description);
  checkCtor(global, "Function", description);
}

add_task(async function test_shared_system_global() {
  let systemGlobal = Cu.getGlobalForObject(Services);
  checkGlobal(systemGlobal, "system global");
});

add_task(async function test_browser_window_global() {
  checkGlobal(window, "browser chrome window");
});

add_task(async function test_non_system_window_global() {
  await BrowserTestUtils.withNewTab("about:blank", async function (browser) {
    let isFrozen = await SpecialPowers.spawn(browser, [], () => {
      return Object.isFrozen(content.wrappedJSObject.Object);
    });
    ok(!isFrozen, "content window builtins should not be frozen");
  });
});
