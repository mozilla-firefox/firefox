/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
// @ts-check
/**
 * @typedef {import("../@types/perf").NumberScaler} NumberScaler
 * @typedef {import("../@types/perf").ScaleFunctions} ScaleFunctions
 * @typedef {import("../@types/perf").FeatureDescription} FeatureDescription
 */
"use strict";

const UNIT_L10N_IDS = [
  "perftools-memory-unit-b",
  "perftools-memory-unit-kib",
  "perftools-memory-unit-mib",
  "perftools-memory-unit-gib",
  "perftools-memory-unit-tib",
  "perftools-memory-unit-pib",
  "perftools-memory-unit-eib",
  "perftools-memory-unit-zib",
  "perftools-memory-unit-yib",
];

const AppConstants = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
).AppConstants;

/**
 * Linearly interpolate between values.
 * https://en.wikipedia.org/wiki/Linear_interpolation
 *
 * @param {number} frac - Value ranged 0 - 1 to interpolate between the range start and range end.
 * @param {number} rangeStart - The value to start from.
 * @param {number} rangeEnd - The value to interpolate to.
 * @returns {number}
 */
function lerp(frac, rangeStart, rangeEnd) {
  return (1 - frac) * rangeStart + frac * rangeEnd;
}

/**
 * Make sure a value is clamped between a min and max value.
 *
 * @param {number} val - The value to clamp.
 * @param {number} min - The minimum value.
 * @param {number} max - The max value.
 * @returns {number}
 */
function clamp(val, min, max) {
  return Math.max(min, Math.min(max, val));
}

/**
 * Formats a file size, returning the numeric value along with
 * a respective memory unit localization id.
 *
 * @param {number} num - The number (in bytes) to format.
 * @returns {{ size: number, unitL10nId: string }}
 */
function formatFileSize(num) {
  if (!Number.isFinite(num)) {
    throw new TypeError(`Expected a finite number, got ${typeof num}: ${num}`);
  }

  if (num < 1) {
    return { size: num, unitL10nId: UNIT_L10N_IDS[0] };
  }

  const exponent = Math.min(
    Math.floor(Math.log2(num) / Math.log2(1024)),
    UNIT_L10N_IDS.length - 1
  );
  const size = Number((num / Math.pow(1024, exponent)).toPrecision(3));
  const unitL10nId = UNIT_L10N_IDS[exponent];

  return { size, unitL10nId };
}

/**
 * Creates numbers that increment linearly within a base 10 scale:
 * 0.1, 0.2, 0.3, ..., 0.8, 0.9, 1, 2, 3, ..., 9, 10, 20, 30, etc.
 *
 * @param {number} rangeStart
 * @param {number} rangeEnd
 *
 * @returns {ScaleFunctions}
 */
function makeLinear10Scale(rangeStart, rangeEnd) {
  const start10 = Math.log10(rangeStart);
  const end10 = Math.log10(rangeEnd);

  if (!Number.isInteger(start10)) {
    throw new Error(`rangeStart is not a power of 10: ${rangeStart}`);
  }

  if (!Number.isInteger(end10)) {
    throw new Error(`rangeEnd is not a power of 10: ${rangeEnd}`);
  }

  // Intervals are base 10 intervals:
  // - [0.01 .. 0.09]
  // - [0.1 .. 0.9]
  // - [1 .. 9]
  // - [10 .. 90]
  const intervals = end10 - start10;

  // Note that there are only 9 steps per interval, not 10:
  // 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9
  const STEP_PER_INTERVAL = 9;

  const steps = intervals * STEP_PER_INTERVAL;

  /** @type {NumberScaler} */
  const fromFractionToValue = frac => {
    const step = Math.round(frac * steps);
    const base = Math.floor(step / STEP_PER_INTERVAL);
    const factor = (step % STEP_PER_INTERVAL) + 1;
    return Math.pow(10, base) * factor * rangeStart;
  };

  /** @type {NumberScaler} */
  const fromValueToFraction = value => {
    const interval = Math.floor(Math.log10(value / rangeStart));
    const base = rangeStart * Math.pow(10, interval);
    return (interval * STEP_PER_INTERVAL + value / base - 1) / steps;
  };

  /** @type {NumberScaler} */
  const fromFractionToSingleDigitValue = frac => {
    return +fromFractionToValue(frac).toPrecision(1);
  };

  return {
    // Takes a number ranged 0-1 and returns it within the range.
    fromFractionToValue,
    // Takes a number in the range, and returns a value between 0-1
    fromValueToFraction,
    // Takes a number ranged 0-1 and returns a value in the range, but with
    // a single digit value.
    fromFractionToSingleDigitValue,
    // The number of steps available on this scale.
    steps,
  };
}

/**
 * Creates numbers that scale exponentially as powers of 2.
 *
 * @param {number} rangeStart
 * @param {number} rangeEnd
 *
 * @returns {ScaleFunctions}
 */
function makePowerOf2Scale(rangeStart, rangeEnd) {
  const startExp = Math.log2(rangeStart);
  const endExp = Math.log2(rangeEnd);

  if (!Number.isInteger(startExp)) {
    throw new Error(`rangeStart is not a power of 2: ${rangeStart}`);
  }

  if (!Number.isInteger(endExp)) {
    throw new Error(`rangeEnd is not a power of 2: ${rangeEnd}`);
  }

  const steps = endExp - startExp;

  /** @type {NumberScaler} */
  const fromFractionToValue = frac =>
    Math.pow(2, Math.round((1 - frac) * startExp + frac * endExp));

  /** @type {NumberScaler} */
  const fromValueToFraction = value =>
    (Math.log2(value) - startExp) / (endExp - startExp);

  /** @type {NumberScaler} */
  const fromFractionToSingleDigitValue = frac => {
    // fromFractionToValue returns an exact power of 2, we don't want to change
    // its precision. Note that formatFileSize will display it in a nice binary
    // unit with up to 3 digits.
    return fromFractionToValue(frac);
  };

  return {
    // Takes a number ranged 0-1 and returns it within the range.
    fromFractionToValue,
    // Takes a number in the range, and returns a value between 0-1
    fromValueToFraction,
    // Takes a number ranged 0-1 and returns a value in the range, but with
    // a single digit value.
    fromFractionToSingleDigitValue,
    // The number of steps available on this scale.
    steps,
  };
}

/**
 * Scale a source range to a destination range, but clamp it within the
 * destination range.
 *
 * @param {number} val - The source range value to map to the destination range,
 * @param {number} sourceRangeStart,
 * @param {number} sourceRangeEnd,
 * @param {number} destRangeStart,
 * @param {number} destRangeEnd
 */
function scaleRangeWithClamping(
  val,
  sourceRangeStart,
  sourceRangeEnd,
  destRangeStart,
  destRangeEnd
) {
  const frac = clamp(
    (val - sourceRangeStart) / (sourceRangeEnd - sourceRangeStart),
    0,
    1
  );
  return lerp(frac, destRangeStart, destRangeEnd);
}

/**
 * Use some heuristics to guess at the overhead of the recording settings.
 *
 * TODO - Bug 1597383. The UI for this has been removed, but it needs to be reworked
 * for new overhead calculations. Keep it for now in tree.
 *
 * @param {number} interval
 * @param {number} bufferSize
 * @param {string[]} features - List of the selected features.
 */
function calculateOverhead(interval, bufferSize, features) {
  // NOT "nostacksampling" (double negative) means periodic sampling is on.
  const periodicSampling = !features.includes("nostacksampling");
  const overheadFromSampling = periodicSampling
    ? scaleRangeWithClamping(
        Math.log(interval),
        Math.log(0.05),
        Math.log(1),
        1,
        0
      ) +
      scaleRangeWithClamping(
        Math.log(interval),
        Math.log(1),
        Math.log(100),
        0.1,
        0
      )
    : 0;
  const overheadFromBuffersize = scaleRangeWithClamping(
    Math.log(bufferSize),
    Math.log(10),
    Math.log(1000000),
    0,
    0.1
  );
  const overheadFromStackwalk =
    features.includes("stackwalk") && periodicSampling ? 0.05 : 0;
  const overheadFromJavaScript =
    features.includes("js") && periodicSampling ? 0.05 : 0;
  const overheadFromJSTracer = features.includes("jstracer") ? 0.05 : 0;
  const overheadFromJSAllocations = features.includes("jsallocations")
    ? 0.05
    : 0;
  const overheadFromNativeAllocations = features.includes("nativeallocations")
    ? 0.5
    : 0;

  return clamp(
    overheadFromSampling +
      overheadFromBuffersize +
      overheadFromStackwalk +
      overheadFromJavaScript +
      overheadFromJSTracer +
      overheadFromJSAllocations +
      overheadFromNativeAllocations,
    0,
    1
  );
}

/**
 * Given an array of absolute paths on the file system, return an array that
 * doesn't contain the common prefix of the paths; in other words, if all paths
 * share a common ancestor directory, cut off the path to that ancestor
 * directory and only leave the path components that differ.
 * This makes some lists look a little nicer. For example, this turns the list
 * ["/Users/foo/code/obj-m-android-opt", "/Users/foo/code/obj-m-android-debug"]
 * into the list ["obj-m-android-opt", "obj-m-android-debug"].
 *
 * @param {string[]} pathArray The array of absolute paths.
 * @returns {string[]} A new array with the described adjustment.
 */
function withCommonPathPrefixRemoved(pathArray) {
  if (pathArray.length === 0) {
    return [];
  }

  const firstPath = pathArray[0];
  const isWin = /^[A-Za-z]:/.test(firstPath);
  const firstWinDrive = getWinDrive(firstPath);
  for (const path of pathArray) {
    const winDrive = getWinDrive(path);

    if (!PathUtils.isAbsolute(path) || winDrive !== firstWinDrive) {
      // We expect all paths to be absolute and on Windows we expect all
      // paths to be on the same disk. If this is not the case return the
      // original array.
      return pathArray;
    }
  }

  // At this point we're either not on Windows or all paths are on the same
  // Windows disk and all paths are absolute.
  // Find the common prefix. Start by assuming the entire path except for the
  // last folder is shared.
  const splitPaths = pathArray.map(path => PathUtils.split(path));
  const [firstSplitPath, ...otherSplitPaths] = splitPaths;
  const prefix = firstSplitPath.slice(0, -1);
  for (const sp of otherSplitPaths) {
    prefix.length = Math.min(prefix.length, sp.length - 1);
    for (let i = 0; i < prefix.length; i++) {
      if (prefix[i] !== sp[i]) {
        prefix.length = i;
        break;
      }
    }
  }
  if (
    prefix.length === 0 ||
    (prefix.length === 1 && (prefix[0] === firstWinDrive || prefix[0] === "/"))
  ) {
    // There is no shared prefix.
    // We treat a prefix of ["/"] as "no prefix", too: Absolute paths on
    // non-Windows start with a slash, so PathUtils.split(path) always returns
    // an array whose first element is "/" on those platforms.
    // Stripping off a prefix of ["/"] from the split paths would simply remove
    // the leading slash from the un-split paths, which is not useful.
    return pathArray;
  }

  // Strip the common prefix from all paths.
  return splitPaths.map(sp => {
    return sp.slice(prefix.length).join(isWin ? "\\" : "/");
  });
}

/**
 * This method has been copied as part of the migration from `OS.Path` to
 * `PathUtils`.
 *
 * Return the windows drive name of a path, or |null| if the path does
 * not contain a drive name.
 *
 * Drive name appear either as "DriveName:..." (the return drive
 * name includes the ":") or "\\\\DriveName..." (the returned drive name
 * includes "\\\\").
 *
 * @param {string} path The path from which we are to return the Windows drive name.
 * @returns {?string} Windows drive name e.g. "C:" or null if path is not a Windows path.
 */
function getWinDrive(path) {
  if (path == null) {
    throw new TypeError("path is invalid");
  }

  if (path.startsWith("\\\\")) {
    // UNC path
    if (path.length == 2) {
      return null;
    }
    const index = path.indexOf("\\", 2);
    if (index == -1) {
      return path;
    }
    return path.slice(0, index);
  }
  // Non-UNC path
  const index = path.indexOf(":");
  if (index <= 0) {
    return null;
  }
  return path.slice(0, index + 1);
}

/**
 * @type {FeatureDescription[]}
 */
const featureDescriptions = [
  {
    name: "Native Stacks",
    value: "stackwalk",
    title:
      "Record native stacks (C++ and Rust). This is not available on all platforms.",
    recommended: true,
    disabledReason: "Native stack walking is not supported on this platform.",
  },
  {
    name: "JavaScript",
    value: "js",
    title:
      "Record JavaScript stack information, and interleave it with native stacks.",
    recommended: true,
  },
  {
    name: "Memory Tracking",
    value: "memory",
    title:
      "Track the memory allocations and deallocations per process over time.",
    recommended: true,
  },
  {
    name: "Java",
    value: "java",
    title: "Profile Java code",
    disabledReason: "This feature is only available on Android.",
  },
  {
    name: "No Periodic Sampling",
    value: "nostacksampling",
    title: "Disable interval-based stack sampling",
  },
  {
    name: "Main Thread File IO",
    value: "mainthreadio",
    title: "Record main thread File I/O markers.",
  },
  {
    name: "Profiled Threads File IO",
    value: "fileio",
    title: "Record File I/O markers from only profiled threads.",
  },
  {
    name: "All File IO",
    value: "fileioall",
    title:
      "Record File I/O markers from all threads, even unregistered threads.",
  },
  {
    name: "No Marker Stacks",
    value: "nomarkerstacks",
    title: "Do not capture stacks when recording markers, to reduce overhead.",
  },
  {
    name: "Sequential Styling",
    value: "seqstyle",
    title: "Disable parallel traversal in styling.",
  },
  {
    name: "Screenshots",
    value: "screenshots",
    title: "Record screenshots of all browser windows.",
  },
  {
    name: "IPC Messages",
    value: "ipcmessages",
    title: "Track IPC messages.",
  },
  {
    name: "JS Allocations",
    value: "jsallocations",
    title: "Track JavaScript allocations",
  },
  {
    name: "Native Allocations",
    value: "nativeallocations",
    title: "Track native allocations",
  },
  {
    name: "Audio Callback Tracing",
    value: "audiocallbacktracing",
    title: "Trace real-time audio callbacks.",
  },
  {
    name: "Windows System Tick Sampling",
    value: "winsystemticksampling",
    title:
      "Keep sampling on the system timer tick, without high-resolution " +
      "timers or timer resolution changes, to minimize wakeups and avoid " +
      "affecting timer-sensitive code. Warning: The sampling interval " +
      "will be at least ~16ms.",
    disabledReason: "Windows only.",
  },
  {
    name: "CPU Utilization - All Threads",
    value: "cpuallthreads",
    title:
      "Record CPU usage of all known threads, even threads which are not being profiled.",
    experimental: true,
  },
  {
    name: "Periodic Sampling - All Threads",
    value: "samplingallthreads",
    title: "Capture stack samples in ALL registered thread.",
    experimental: true,
  },
  {
    name: "Markers - All Threads",
    value: "markersallthreads",
    title: "Record markers in ALL registered threads.",
    experimental: true,
  },
  {
    name: "Unregistered Threads",
    value: "unregisteredthreads",
    title:
      "Periodically discover unregistered threads and record them and their " +
      "CPU utilization as markers in the main thread -- Beware: expensive!",
    experimental: true,
  },
  {
    name: "Process CPU Utilization",
    value: "processcpu",
    title:
      "Record how much CPU has been used between samples by each process. " +
      "To see graphs: When viewing the profile, open the JS console and run: " +
      "experimental.enableProcessCPUTracks()",
    experimental: true,
  },
  {
    name: "Power Use",
    value: "power",
    title: (() => {
      switch (AppConstants.platform) {
        case "win":
          return (
            "Record the value of every energy meter available on the system with " +
            "each sample. Only available on Windows 11 with Intel CPUs."
          );
        case "linux":
          return (
            "Record the power used by the entire system with each sample. " +
            "Only available with Intel CPUs and requires setting the sysctl kernel.perf_event_paranoid to 0."
          );
        case "macosx":
          return "Record the power used by the entire system (Intel) or each process (Apple Silicon) with each sample.";
        default:
          return "Not supported on this platform.";
      }
    })(),
    experimental: true,
  },
  {
    name: "CPU Frequency",
    value: "cpufreq",
    title:
      "Record the clock frequency of every CPU core for every profiler sample.",
    experimental: true,
    disabledReason:
      "This feature is only available on Windows, Linux and Android.",
  },
  {
    name: "Network Bandwidth",
    value: "bandwidth",
    title: "Record the network bandwidth used between every profiler sample.",
  },
  {
    name: "JS Execution Tracing",
    value: "tracing",
    title:
      "Disable periodic stack sampling, and capture information about every JS function executed.",
    experimental: true,
  },
  {
    name: "Sandbox profiling",
    value: "sandbox",
    title: "Report sandbox syscalls and logs in the profiler.",
  },
  {
    name: "Flows",
    value: "flows",
    title:
      "Include all flow-related markers. These markers show the program flow better but " +
      "can cause more overhead in some places than normal.",
  },
  {
    name: "JavaScript Sources",
    value: "jssources",
    title: "Collect JavaScript source code information for profiled scripts.",
    experimental: true,
  },
];

module.exports = {
  formatFileSize,
  makeLinear10Scale,
  makePowerOf2Scale,
  scaleRangeWithClamping,
  calculateOverhead,
  withCommonPathPrefixRemoved,
  featureDescriptions,
};
