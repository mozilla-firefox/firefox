import { assert, hasFeature } from '../util/util.js';

export type TestConfig = {
  /**
   * Enable debug-level logs (normally logged via `Fixture.debug()`).
   */
  enableDebugLogs: boolean;

  /**
   * Maximum number of subcases in flight at once, within a case. Once this many
   * are in flight, wait for a subcase to finish before starting the next one.
   */
  maxSubcasesInFlight: number;

  /**
   * After this many subcases run on the page, run `attemptGarbageCollection()`.
   * Setting to `Infinity` disables this. Setting to 1 attempts GC every time (slow!).
   */
  subcasesBetweenAttemptingGC: number;

  /**
   * After this many cases use a device, destroy and replace it to free GPU resources.
   * Setting to `Infinity` disables this. Setting to 1 gets a new device every time (slow!).
   */
  casesBetweenReplacingDevice: number;

  /**
   * Keep at most one device at a time, destroying the others before creating a new one.
   * Needed by tools that support only one device at a time, such as RenderDoc.
   */
  oneDevice: boolean;

  testHeartbeatCallback: () => void;

  /**
   * Called with a test case's query when the case starts running, and again when it has finished.
   */
  testCaseStartCallback: (query: string) => void;
  testCaseEndCallback: (query: string) => void;

  /**
   * Called with the device each time a test case acquires one from the device pool.
   */
  deviceAcquiredCallback: (device: GPUDevice) => void;

  noRaceWithRejectOnTimeout: boolean;

  /**
   * Logger for debug messages from the test framework
   * (that can't be captured in the logs of a test).
   */
  frameworkDebugLog?: (msg: string) => void;

  /**
   * Controls the emission of loops in constant-evaluation shaders under
   * 'webgpu:shader,execution,expression,*'
   * FXC is extremely slow to compile shaders with loops unrolled, where as the
   * MSL compiler is extremely slow to compile with loops rolled.
   */
  unrollConstEvalLoops: boolean;

  /**
   * Whether or not we're running in compatibility mode.
   */
  compatibility: boolean;

  /**
   * Whether or not to request a fallback adapter.
   */
  forceFallbackAdapter: boolean;

  /**
   * Enforce the default limits on the adapter
   */
  enforceDefaultLimits: boolean;

  /**
   * Block all features on the adapter
   */
  blockAllFeatures: boolean;

  /**
   * Whether to enable the `logToWebSocket` function used for out-of-band test logging.
   */
  logToWebSocket: boolean;
};

/** Test configuration options. Globally modifiable global state. */
export const globalTestConfig: TestConfig = {
  enableDebugLogs: false,
  maxSubcasesInFlight: 100,
  subcasesBetweenAttemptingGC: 5000,
  casesBetweenReplacingDevice: Infinity,
  oneDevice: false,
  testHeartbeatCallback: () => {},
  testCaseStartCallback: () => {},
  testCaseEndCallback: () => {},
  deviceAcquiredCallback: () => {},
  noRaceWithRejectOnTimeout: false,
  unrollConstEvalLoops: false,
  compatibility: false,
  forceFallbackAdapter: false,
  enforceDefaultLimits: false,
  blockAllFeatures: false,
  logToWebSocket: false,
};

// Check if features has 'core-features-and-limits'.
// Note: The CTS generally, requires that if globalTestConfig.compatibility
// is true then the device MUST be a compatibility device since the CTS
// is trying to test that compatibility devices have the correct validation.
export function isCompatibilityMode(features: GPUSupportedFeatures) {
  if (globalTestConfig.compatibility) {
    assert(!hasFeature(features, 'core-features-and-limits'));
  }
  return globalTestConfig.compatibility;
}

// Check if a device is a compatibility device.
// Note: The CTS generally, requires that if globalTestConfig.compatibility
// is true then the device MUST be a compatibility device since the CTS
// is trying to test that compatibility devices have the correct validation.
export function isCompatibilityDevice(device: GPUDevice) {
  return isCompatibilityMode(device.features);
}
