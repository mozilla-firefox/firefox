/**
 * This test is used to check hardware video decoding on different platforms.
 */
const FFVPX_HW = "media.ffvpx-hw.enabled";

// Software codecs always run in the content process. See bug 2062611.
const SOFTWARE_VIDEO_CODECS = {
  process: "content",
  hardwareAcceleration: "prefer-software",
  isVideo: true,
  encoder: "ffvpx software video encoder",
  codecConfigs: [
    { codec: "av01.0.04M.08" }, // av1
    { codec: "vp8" }, // vp8
    { codec: "vp09.00.10.08" }, // vp9
  ],
};

const SOFTWARE_AUDIO_CODECS = {
  process: "content",
  hardwareAcceleration: "no-preference",
  isVideo: false,
  encoder: "ffvpx audio encoder",
  codecConfigs: [
    { codec: "opus" }, // opus
  ],
};

const SOFTWARE_CODECS = [SOFTWARE_VIDEO_CODECS, SOFTWARE_AUDIO_CODECS];

const H264_VIDEO_CODEC = {
  process: "RDD",
  hardwareAcceleration: "prefer-hardware",
  requireGpu: true,
  isVideo: true,
  codecConfigs: [
    {
      codec: "avc1.42001E",
    },
  ],
};

const PLATFORMS = {
  WINNT: [
    ...SOFTWARE_CODECS,
    {
      ...H264_VIDEO_CODEC,
      hardwareAcceleration: "prefer-software",
      encoder: "Encoder MFT",
    },
    {
      ...H264_VIDEO_CODEC,
      process: "GPU",
      encoder: "Encoder MFT",
    },
  ],
  Darwin: [
    ...SOFTWARE_CODECS,
    {
      ...H264_VIDEO_CODEC,
      requireGpu: false,
      encoder: "apple hardware VT encoder",
    },
  ],
  Linux: SOFTWARE_CODECS,
};

function applyOverride(base, videoPlatform) {
  if (!base.isVideo) {
    return base;
  }
  return { ...base, ...videoPlatform };
}

async function testWebCodecsEncodingProcess(hasGpu) {
  // If we aren't remoting video, force the process to be content.
  const remoteVideoPlatform = SpecialPowers.getBoolPref(
    "media.use-remote-encoder.video.platform"
  );
  let configVideoPlatformOverride = {};
  if (!remoteVideoPlatform) {
    configVideoPlatformOverride.process = "content";
  }

  const platformName = SpecialPowers.Services.appinfo.OS;
  const platformTest = PLATFORMS[platformName];
  for (const baseTest of platformTest) {
    const test = applyOverride(baseTest, configVideoPlatformOverride);
    try {
      // Skip tests that require a GPU if we don't have one, or if we
      // disabled remoting platform encoders (on Windows we can't get
      // the hardware encoders then).
      if (
        test.requireGpu !== undefined &&
        test.requireGpu &&
        (!hasGpu || !remoteVideoPlatform)
      ) {
        continue;
      }
      // Disable ffvpx hw if needed
      if (test.disableFFVPXHW !== undefined) {
        await SpecialPowers.pushPrefEnv({
          set: [[FFVPX_HW, false]],
        });
      } else if (test.enableFFVPXHW !== undefined) {
        await SpecialPowers.pushPrefEnv({
          set: [[FFVPX_HW, true]],
        });
      }
      for (const codecConfig of test.codecConfigs) {
        info(
          `Testing ${codecConfig} for hwAccel ${test.hardwareAcceleration} on ${platformName} : expect ${test.encoder} in ${test.process}`
        );
        let encoder = await createEncoder(
          codecConfig,
          test.hardwareAcceleration,
          test.isVideo
        );
        await assertRunningProcessAndEncoderName(
          encoder,
          test.process,
          test.encoder
        );
      }
      // Reset ffvpx hw pref in order not to interfere next test case
      if (
        test.disableFFVPXHW !== undefined ||
        test.enableFFVPXHW !== undefined
      ) {
        await SpecialPowers.popPrefEnv();
      }
    } catch (e) {
      ok(false, `Testing '${test}' hit exception '${e}'`);
    }
  }
}

// Following are helper functions
function createEncoder(baseConfig, hardwareAcceleration, isVideo) {
  let encoder;
  let config;
  const init = { output: () => {}, error: () => {} };
  if (isVideo) {
    encoder = new VideoEncoder(init);
    config = {
      hardwareAcceleration,
      width: 640,
      height: 480,
      ...baseConfig,
    };
  } else {
    encoder = new AudioEncoder(init);
    config = {
      sampleRate: 48000,
      numberOfChannels: 1,
      ...baseConfig,
    };
  }
  try {
    encoder.configure(config);
    ok(true, `Encoder configuration '${config}'`);
  } catch (e) {
    ok(false, `Encoder configuration '${config}' hit exception '${e}'`);
  }
  return encoder;
}

async function assertRunningProcessAndEncoderName(
  encoder,
  expectedProcess,
  expectedEncoder
) {
  const debugInfo = await SpecialPowers.wrap(encoder).mozRequestDebugInfo();
  const encoderName = debugInfo.encoderName;

  const isExpectedEncoder = encoderName.includes(expectedEncoder);
  ok(
    isExpectedEncoder,
    `Playback running by encoder '${encoderName}', expected '${expectedEncoder}'`
  );

  // The content process does not include the process name in the encoder name.
  const processRegex = /(?<=\()[^)]+(?=\))/g;
  const process = encoderName.match(processRegex) ?? ["content"];
  const isExpectedProcess = process.at(-1) === expectedProcess;
  ok(
    isExpectedProcess,
    `Playback running in process '${encoderName}', expected '${expectedProcess}'`
  );
}
