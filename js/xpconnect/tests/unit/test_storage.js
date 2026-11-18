/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

function run_test() {
  var sb = new Cu.Sandbox('http://www.example.com',
                          { wantGlobalProperties: ["storage"] });
  sb.ok = ok;
  Cu.evalInSandbox('ok(storage instanceof StorageManager);',
                   sb);
  Cu.importGlobalProperties(["storage"]);
  Assert.ok(storage instanceof StorageManager);
}
