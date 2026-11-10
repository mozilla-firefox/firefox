/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-disable mozilla/avoid-Date-timing */
/* eslint-disable no-unsanitized/method */

const fs = require("fs");
const os = require("os");
const path = require("path");
const { exec } = require("node:child_process");

async function getBrowsertimeResultsPath(context, commands, createDirectories) {
  // Import needs to be done here because importing at the top-level
  // requires a wrapped async function call, but that import can then
  // only be used within the wrapped async call. Outside of it, the imported
  // variable is undefined.
  let pathToFolder;
  if (os.type() == "Windows_NT") {
    pathToFolder = await import(
      `file://${process.env.BROWSERTIME_ROOT.replace(
        "\\",
        "/"
      )}/node_modules/browsertime/lib/support/pathToFolder.js`
    );
  } else {
    pathToFolder = await import(
      path.join(
        process.env.BROWSERTIME_ROOT,
        "node_modules",
        "browsertime",
        "lib",
        "support",
        "pathToFolder.js"
      )
    );
  }

  const browsertimeResultsPath = path.join(
    context.options.resultDir,
    await pathToFolder.pathToFolder(
      commands.measure.result[0].browserScripts.pageinfo.url,
      context.options
    )
  );

  if (createDirectories) {
    try {
      await fs.promises.mkdir(browsertimeResultsPath, { recursive: true });
    } catch (err) {
      context.log.info(
        `Failed to create browsertime results path directories: ${err}`
      );
    }
  }

  return browsertimeResultsPath;
}

async function moveToBrowsertimeResultsPath(
  destFilename,
  srcFilepath,
  context,
  commands
) {
  const browsertimeResultsPath = await getBrowsertimeResultsPath(
    context,
    commands,
    true
  );
  const destFilepath = path.join(browsertimeResultsPath, destFilename);

  try {
    await fs.promises.rename(srcFilepath, destFilepath);
  } catch (err) {
    context.log.info(
      `Failed to rename/copy file into browsertime results: ${err}`
    );
  }

  return destFilepath;
}

function logCommands(commands, logger, command, printFirstArg) {
  let object = commands;
  let path = command.split(".");
  while (path.length > 1) {
    object = object[path.shift()];
  }
  let methodName = path[0];
  let originalFun = object[methodName];
  object[methodName] = async function () {
    let logString = ": " + command;
    if (printFirstArg && arguments.length) {
      logString += ": " + arguments[0];
    }
    logger.info("BEGIN" + logString);
    let rv = await originalFun.apply(object, arguments);
    logger.info("END" + logString);
    return rv;
  };
}

async function logTask(context, logString, task) {
  context.log.info("BEGIN: " + logString);
  let rv = await task();
  context.log.info("END: " + logString);

  return rv;
}

let startedProfiling = false;
let childPromise, child, profilePath, profileFilename;
async function startWindowsPowerProfiling(iterationIndex) {
  let canPowerProfile =
    os.type() == "Windows_NT" &&
    /10.0.2[2-9]/.test(os.release()) &&
    process.env.XPCSHELL_PATH;

  if (canPowerProfile && !startedProfiling) {
    startedProfiling = true;

    profileFilename = `profile_power_${iterationIndex}.json`;
    profilePath = process.env.MOZ_UPLOAD_DIR + "\\" + profileFilename;
    childPromise = new Promise(resolve => {
      child = exec(
        process.env.XPCSHELL_PATH,
        {
          env: {
            MOZ_PROFILER_STARTUP: "1",
            MOZ_PROFILER_STARTUP_FEATURES:
              "power,nostacksampling,winsystemticksampling",
            MOZ_PROFILER_SHUTDOWN: profilePath,
          },
        },
        (error, stdout, stderr) => {
          if (error) {
            console.log("DEBUG ERROR", error);
          }
          if (stderr) {
            console.log("DEBUG stderr", error);
          }
          resolve(stdout);
        }
      );
    });
  }
}

async function stopWindowsPowerProfiling() {
  if (startedProfiling) {
    startedProfiling = false;
    child.stdin.end("quit()");
    await childPromise;
  }
}

async function gatherWindowsPowerUsage(testTimes) {
  let powerDataEntries = [];

  if (profilePath) {
    let profile;

    try {
      profile = JSON.parse(await fs.readFileSync(profilePath, "utf8"));
    } catch (err) {
      throw Error(`Failed to read the profile file: ${err}`);
    }

    for (let [start, end] of testTimes) {
      start -= profile.meta.startTime;
      end -= profile.meta.startTime;
      let powerData = {
        cpu_cores: [],
        cpu_package: [],
        gpu: [],
      };

      for (let counter of profile.counters) {
        let field = "";
        if (counter.name == "Power: iGPU") {
          field = "gpu";
        } else if (counter.name == "Power: CPU package") {
          field = "cpu_package";
        } else if (counter.name == "Power: CPU cores") {
          field = "cpu_cores";
        } else {
          continue;
        }

        let accumulatedPower = 0;
        for (let i = 0; i < counter.samples.data.length; ++i) {
          let time = counter.samples.data[i][counter.samples.schema.time];
          if (time < start) {
            continue;
          }
          if (time > end) {
            break;
          }
          accumulatedPower +=
            counter.samples.data[i][counter.samples.schema.count];
        }
        powerData[field].push(accumulatedPower);
      }

      powerDataEntries.push(powerData);
    }

    return powerDataEntries;
  }
  return null;
}

function logTest(name, test) {
  return async function wrappedTest(context, commands) {
    let testTimes = [];

    let start;
    let originalStart = commands.measure.start;
    commands.measure.start = function () {
      start = Date.now();
      return originalStart.apply(commands.measure, arguments);
    };
    let originalStop = commands.measure.stop;
    commands.measure.stop = function () {
      testTimes.push([start, Date.now()]);
      return originalStop.apply(commands.measure, arguments);
    };

    for (let [commandName, printFirstArg] of [
      ["addText.bySelector", true],
      ["android.shell", true],
      ["click.byXpath", true],
      ["click.byXpathAndWait", true],
      ["js.run", false],
      ["js.runAndWait", false],
      ["js.runPrivileged", false],
      ["measure.add", true],
      ["measure.addObject", false],
      ["measure.start", true],
      ["measure.stop", false],
      ["mouse.doubleClick.bySelector", true],
      ["mouse.doubleClick.byXpath", true],
      ["mouse.singleClick.bySelector", true],
      ["navigate", true],
      ["profiler.start", false],
      ["profiler.stop", false],
      ["trace.start", false],
      ["trace.stop", false],
      ["wait.byTime", true],
    ]) {
      logCommands(commands, context.log, commandName, printFirstArg);
    }

    if (context.options.browsertime.support_class) {
      await startWindowsPowerProfiling(context.index);
    }

    let iterationName = "iteration";
    if (
      context.options.firefox.geckoProfiler ||
      context.options.browsertime.expose_profiler === "true"
    ) {
      iterationName = "profiling iteration";
    }
    let logString = `: ${iterationName} ${context.index}: ${name}`;
    context.log.info("BEGIN" + logString);
    let rv = await test(context, commands);
    context.log.info("END" + logString);

    if (context.options.browsertime.support_class) {
      await stopWindowsPowerProfiling();
      let powerData = await gatherWindowsPowerUsage(testTimes);

      if (powerData?.length) {
        // Move the profile to the appropriate location in the browsertime results folder
        await moveToBrowsertimeResultsPath(
          profileFilename,
          profilePath,
          context,
          commands
        );

        powerData.forEach((powerUsage, ind) => {
          if (!commands.measure.result[ind].extras.powerUsage) {
            commands.measure.result[ind].extras.powerUsagePageload = [];
          }
          commands.measure.result[ind].extras.powerUsagePageload.push({
            powerUsagePageload: powerUsage,
          });
        });
      }
    }

    return rv;
  };
}

/**
 * Front-load or postpone Firefox background work that would otherwise run
 * during the first measured iterations. Each step is independent, and a
 * failing step is logged rather than failing the test.
 */
async function deferBackgroundTasks(context, commands) {
  const failures = await commands.js.runPrivilegedAsync(`
    const done = arguments[arguments.length - 1];
    const steps = {
      // Mark the background update timer as just fired so that it doesn't
      // load the update service during the test. This only helps if it runs
      // before the timer manager's first check (app.update.timerFirstInterval,
      // 30s after startup by default); otherwise the update service has
      // already been loaded.
      "update timer": () => {
        Services.prefs.setIntPref(
          "app.update.lastUpdateTime.background-update-timer",
          Math.round(Date.now() / 1000)
        );
      },
      // Load GMP code now instead of during its late idle callback.
      "GMP": () => {
        ChromeUtils.importESModule(
          "resource://gre/modules/GMPInstallManager.sys.mjs"
        );
      },
      // Write the pending session now. The interval prefs from the Raptor
      // profile then defer the next save.
      "session store": async () => {
        const { SessionSaver } = ChromeUtils.importESModule(
          "moz-src:///browser/components/sessionstore/SessionSaver.sys.mjs"
        );
        await SessionSaver.run();
      },
      // Collect and compact the parent's startup garbage now. Without a
      // settle delay, the startup GCs would otherwise run during the
      // measurement.
      "garbage collection": () => {
        Cu.forceGC();
        Cu.forceCC();
        Cu.forceShrinkingGC();
      },
    };
    (async () => {
      const failures = [];
      for (const [name, step] of Object.entries(steps)) {
        try {
          await step();
        } catch (e) {
          failures.push(name + ": " + e);
        }
      }
      return failures;
    })().then(done, e => done([String(e)]));
  `);
  for (const failure of failures) {
    context.log.warn(`Failed to defer background task ${failure}`);
  }
}

module.exports = {
  deferBackgroundTasks,
  logTest,
  logTask,
  gatherWindowsPowerUsage,
  getBrowsertimeResultsPath,
  moveToBrowsertimeResultsPath,
  startWindowsPowerProfiling,
  stopWindowsPowerProfiling,
};
