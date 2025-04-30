/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-env node */

const { logTest, logTask } = require("./utils/profiling");

const fs = require("fs");
const path = require("path");

async function waitForH3(maxRetries, browser, url, commands, context) {
  let attempt = 0;
  while (attempt < maxRetries) {
    await commands.navigate(url);

    // Get performance entries to check the protocol
    let protocolInfo = await commands.js.run(
      `
      // Get all performance entries
      const entries = performance.getEntries();

      // Create an array to store the results
      const protocolInfo = entries.map(entry => ({
          name: entry.name,
          protocol: entry.nextHopProtocol,
      }));

      return protocolInfo;
      `
    );

    // Log the protocol information
    context.log.info("protocolInfo: " + JSON.stringify(protocolInfo));

    // Check if the main document is using h3 protocol
    const normalizeUrl = url => url.replace(/\/+$/, "");
    const isH3 = protocolInfo.some(
      entry =>
        normalizeUrl(entry.name) === normalizeUrl(url) &&
        entry.protocol === "h3"
    );

    if (isH3) {
      context.log.info("Protocol h3 detected!");
      return; // Exit the function if h3 is found
    }

    // Increment the attempt counter and retry
    attempt++;
    context.log.info(
      `Retry attempt ${attempt} - Protocol is not h3. Retrying...`
    );
    if (browser === "firefox") {
      const script = `
      Services.obs.notifyObservers(null, "net:cancel-all-connections");
      Services.obs.notifyObservers(null, "network:reset-http3-excluded-list");
    `;
      commands.js.runPrivileged(script);
    }
    context.log.info("Waiting 3s to close connection...");
    await commands.wait.byTime(3000);
  }
}

async function waitForComplete(timeout, commands, context, id) {
  let starttime = performance.now();
  let status = "";
  let goodput = 0;

  while (performance.now() - starttime < timeout) {
    status = await commands.js.run(
      `return document.getElementById('${id}').innerHTML;`
    );

    if (status.startsWith("success")) {
      goodput = parseFloat(status.split(":")[1]);
      status = "success";
      break;
    } else if (status.startsWith("error")) {
      break;
    }

    await commands.wait.byTime(1000);
  }

  let endtime = performance.now();

  return {
    start: starttime,
    end: endtime,
    status,
    goodput,
  };
}

module.exports = logTest(
  "download/upload test",
  async function (context, commands) {
    let serverUrl = `${context.options.browsertime.server_url}`;
    let iterations = `${context.options.browsertime.iterations}`;
    const [protocol, testType] =
      `${context.options.browsertime.test_type}`.split("_");

    await commands.measure.start(serverUrl);
    let accumulatedResults = [];
    let browserName = "";
    let file_size = parseInt(context.options.browsertime.test_file_size, 10);
    if (Number.isNaN(file_size)) {
      // default is 32MB
      file_size = 32000000;
    }
    for (let iteration = 0; iteration < iterations; iteration++) {
      await logTask(context, "cycle " + iteration, async function () {
        const driver = context.selenium.driver;
        const webdriver = context.selenium.webdriver;
        let capabilities = await driver.getCapabilities();
        browserName = capabilities.get("browserName");

        if (protocol === "h3") {
          await waitForH3(10, browserName, serverUrl, commands, context);
        } else {
          await commands.navigate(serverUrl);
        }

        if (testType === "download") {
          const downloadItem = await driver.findElement(
            webdriver.By.id("downloadBtn")
          );

          const actions = driver.actions({ async: true });
          await actions.move({ origin: downloadItem }).click().perform();

          // Start the test and wait for the upload to complete
          let results = await waitForComplete(
            1200000,
            commands,
            context,
            "download_status"
          );
          let downloadTime = results.end - results.start;
          // Store result in megabit/seconds
          let downloadGoodput = (file_size * 8) / ((downloadTime / 1000) * 1e6);
          context.log.info(
            "download results: " +
              results.status +
              " duration: " +
              downloadTime +
              "ms, downloadGoodput: " +
              downloadGoodput +
              "Mbit/s"
          );
          accumulatedResults.push(downloadGoodput);
        } else if (testType === "upload") {
          const uploadItem = await driver.findElement(
            webdriver.By.id("fileUpload")
          );

          let tagName = await uploadItem.getTagName();
          if (tagName === "input") {
            if (context.options.browsertime.moz_fetch_dir == "None") {
              context.log.error(
                "This test depends on the fetch task. Download the file, 'https://github.com/mozilla/perf-automation/raw/master/test_files/upload-test-32MB.dat' and set the os environment variable MOZ_FETCHES_DIR to that directory."
              );
            }

            let localFilePath = path.join(
              `${context.options.browsertime.moz_fetch_dir}`,
              `${context.options.browsertime.test_file_name}`
            );
            if (!fs.existsSync(localFilePath)) {
              localFilePath = path.join(
                `${context.options.browsertime.moz_fetch_dir}`,
                "upload-test-32MB.dat"
              );
            }

            context.log.info("Sending file path: " + localFilePath);
            await uploadItem.sendKeys(localFilePath);
          } else {
            const actions = driver.actions({ async: true });
            await actions.move({ origin: uploadItem }).click().perform();
          }

          // Start the test and wait for the upload to complete
          let results = await waitForComplete(
            1200000,
            commands,
            context,
            "upload_status"
          );
          let uploadTime = results.end - results.start;

          // Store result in megabit/seconds
          let uploadGoodput = Number.isNaN(results.goodput)
            ? (file_size * 8) / ((uploadTime / 1000) * 1e6)
            : results.goodput;
          context.log.info(
            "upload results: " +
              results.status +
              " duration: " +
              uploadTime +
              "ms, uploadGoodput: " +
              uploadGoodput +
              "Mbit/s"
          );
          if (results.status === "success") {
            accumulatedResults.push(uploadGoodput);
          }
        } else {
          context.log.error("Unsupported test type:" + testType);
        }
      });

      if (browserName === "firefox") {
        const statsScript = `
        const gDashboard = Cc["@mozilla.org/network/dashboard;1"].getService(
          Ci.nsIDashboard
        );
        let promise = new Promise((resolve, reject) => {
          gDashboard.requestHttp3ConnectionStats((data) => {
            resolve(data);
          });
        });
        let done = false;
        let result = null;
        let error = null;
        promise
          .catch(e => {
            error = e;
          })
          .then(r => {
            result = r;
            done = true;
          });
        // Spin the event loop until done becomes true.
        Services.tm.spinEventLoopUntil(
          "requestHttpConnections",
          () => done
        );
        return result;
        `;
        let res = await commands.js.runPrivileged(statsScript);
        context.log.info("HTTP/3 Connection Stats" + JSON.stringify(res));
      }

      // No need to close the connection at the last run.
      if (iteration != iterations - 1) {
        await commands.navigate("about:blank");
        if (browserName === "firefox") {
          const script = `
      Services.obs.notifyObservers(null, "net:cancel-all-connections");
    `;
          commands.js.runPrivileged(script);
          context.log.info("Waiting 3s to close connection...");
          await commands.wait.byTime(3000);
        } else {
          // TODO: configiure a shorter idle timeout at server side, so we
          // don't have to wait that long.
          context.log.info("Waiting 35s to close connection...");
          await commands.wait.byTime(35000);
        }
      }
    }

    if (testType === "download") {
      commands.measure.addObject({
        custom_data: { "download-goodput": accumulatedResults },
      });
    } else if (testType === "upload") {
      commands.measure.addObject({
        custom_data: { "upload-goodput": accumulatedResults },
      });
    }
  }
);
