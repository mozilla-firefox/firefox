/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

add_task(async function init() {
  const testSet = "dom/fs/test/common/test_fileIdCarry.js";

  const testCases = await require_module(testSet);

  Object.values(testCases).forEach(testItem => {
    add_task(testItem);
  });
});
