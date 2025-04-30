/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-env node */

const { logTest } = require("./utils/profiling");

module.exports = logTest(
  "IndexedDB open test",
  async function (context, commands) {
    context.log.info("Starting an IndexedDB open test");

    const post_startup_delay = context.options.browsertime.post_startup_delay;
    const url = context.options.browsertime.url;
    const iterations = context.options.browsertime.iterations;
    const parallel = context.options.browsertime.parallel;

    await commands.navigate(url);

    context.log.info(
      "Waiting for %d ms (post_startup_delay)",
      post_startup_delay
    );
    await commands.wait.byTime(post_startup_delay);

    await commands.measure.start();

    const open_duration = await context.selenium.driver.executeAsyncScript(`
        const notifyDone = arguments[arguments.length - 1];

        const iterations = ${iterations};
        const parallel = ${parallel};

        function startOpen() {
          return new Promise((resolve, reject) => {
            try {
              const openRequest = indexedDB.open("rootsdb");
              openRequest.onsuccess = () => resolve(openRequest.result);
              openRequest.onerror = () => reject(openRequest.error);
            } catch (e) {
              reject(e);
            }
          });
        }

        async function openInSequence() {
          const result = [];

          for (let index = 0; index < iterations; index++) {
            const database = await startOpen();
            result.push(database);
          }

          return result;
        }

        async function openInParallel() {
          const openPromises = [];

          for (let index = 0; index < iterations; index++) {
            openPromises.push(startOpen());
          }

          return Promise.all(openPromises);
        }

        async function main() {
          const databases =
            parallel ? await openInParallel() : await openInSequence();

          for (const database of databases) {
            database.close();
          }
        }

        const startTime = performance.now();
        main().then(() => {
          notifyDone(performance.now() - startTime);
        });
      `);
    console.log("Open duration ", open_duration);

    const delete_duration = await context.selenium.driver.executeAsyncScript(`
        const notifyDone = arguments[arguments.length - 1];

        function startDelete() {
          return new Promise((resolve, reject) => {
            try {
              const openRequest = indexedDB.deleteDatabase("rootsdb");
              openRequest.onsuccess = () => resolve();
              openRequest.onerror = () => reject(openRequest.error);
            } catch (e) {
              reject(e);
            }
          });
        }

        const startTime = performance.now();
        startDelete().then(() => {
          notifyDone(performance.now() - startTime);
        });
      `);
    console.log("Delete duration ", delete_duration);

    const time_duration = open_duration + delete_duration;
    console.log("Time duration ", time_duration);

    await commands.measure.stop();

    await commands.measure.addObject({
      custom_data: { open_duration, delete_duration, time_duration },
    });

    context.log.info("IndexedDB open test ended");

    return true;
  }
);
