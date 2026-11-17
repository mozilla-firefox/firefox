/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test that JS sources collected in a content process are propagated through
 * the ShutdownProfile IPC message when that process shuts down.
 */
add_task(
  async function test_js_sources_propagated_from_shutdown_content_process() {
    // Disable the content process cache so the process is terminated gracefully
    // (running ShutdownInternal -> GrabShutdownProfileAndShutdown) as soon as
    // its tab is removed.
    await SpecialPowers.pushPrefEnv({
      set: [
        ["dom.ipc.keepProcessesAlive.web", 0],
        ["dom.ipc.processPreload.enabled", false],
      ],
    });

    await ProfilerTestUtils.assertProfilerInactive();

    await ProfilerTestUtils.startProfiler({ features: ["js", "jssources"] });

    let processShutdownPromise;
    const token = crypto.randomUUID().replace(/-/g, "");

    await BrowserTestUtils.withNewTab(
      BASE_URL + "simple.html",
      async contentBrowser => {
        const pid = await SpecialPowers.spawn(contentBrowser, [], () => {
          return Services.appinfo.processID;
        });

        const domProcess = ChromeUtils.getAllDOMProcesses().find(
          p => p.osPid === pid
        );
        Assert.ok(!!domProcess, "Should find the content process");

        // Execute identifiable JS so we have a source to look for in the final
        // profile's additionalInformation. The function name is built from a
        // random token at runtime so it never appears as a literal in the
        // serialized SpecialPowers.spawn callback (which is itself a JS source
        // captured in the content process).
        await SpecialPowers.spawn(contentBrowser, [token], t => {
          content.window.eval(
            "function shutdownTestFn_" +
              t +
              "(){return 42;}" +
              "shutdownTestFn_" +
              t +
              "();"
          );
        });

        // Set up the shutdown observer before the tab is removed so we don't
        // race against the ipc:content-shutdown notification.
        processShutdownPromise = new Promise(resolve => {
          Services.obs.addObserver(function obs(subject) {
            if (
              subject
                .QueryInterface(Ci.nsIPropertyBag2)
                .getProperty("childID") === domProcess.childID
            ) {
              Services.obs.removeObserver(obs, "ipc:content-shutdown");
              resolve();
            }
          }, "ipc:content-shutdown");
        });
      }
    );

    // Tab has been removed. Wait for the content process to fully shut down and
    // send its ShutdownProfile IPC message to the parent.
    await processShutdownPromise;

    Services.profiler.Pause();
    const profileData =
      await Services.profiler.getProfileDataAsGzippedArrayBuffer();
    await Services.profiler.StopProfiler();

    Assert.ok(
      !!profileData.additionalInformation,
      "Profile should have additionalInformation"
    );

    const sources = profileData.additionalInformation.jsSources;
    Assert.ok(!!sources, "additionalInformation should contain jsSources");

    let foundTestFunction = false;
    for (const sourceId in sources) {
      const sourceText = sources[sourceId]?.sourceText;
      if (
        typeof sourceText === "string" &&
        sourceText.includes("shutdownTestFn_" + token)
      ) {
        foundTestFunction = true;
        break;
      }
    }

    if (!foundTestFunction) {
      info(
        `Token: shutdownTestFn_${token}; jsSources entries: ${Object.keys(sources).length}`
      );
    }

    Assert.ok(
      foundTestFunction,
      "JS sources from the shutdown content process should appear in the final profile"
    );
  }
);
