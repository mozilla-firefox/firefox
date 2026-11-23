// META: global=window,dedicatedworker
// META: script=/common/media.js
// META: script=/webcodecs/utils.js
// META: script=/webcodecs/video-encoder-utils.js
// META: variant=?baseline
// META: variant=?main
// META: variant=?high
// META: variant=?high-6.2

promise_test(async t => {
  const codecString = {
    '?baseline': 'avc1.42001e',
    '?main': 'avc1.4d001e',
    '?high': 'avc1.64001e',
    '?high-6.2': 'avc1.64003e',
  }[location.search];

  var encoderConfig;
  if (location.search != "?high-6.2") {
    encoderConfig = {
      codec: codecString,
      width: 640,
      height: 480,
      displayWidth: 800,
      displayHeight: 600,
      avc: {format: 'avc'},  // AVC makes it easy to check the profile.
    };
  } else {
    // high profile + level 6.2
    encoderConfig = {
      codec: codecString,
      width: 7680,
      height: 4320,
      displayWidth: 7680,
      displayHeight: 4320,
      avc: {format: 'avc'},  // AVC makes it easy to check the profile.
    };
  }

  let supported = false;
  try {
    const support = await VideoEncoder.isConfigSupported(encoderConfig);
    supported = support.supported;
  } catch (e) {
  }
  assert_implements_optional(
      supported, `H264 ${location.search.substring(1)} profile unsupported`);

  let description = null;
  let codecInit = {
    error: t.unreached_func('Unexpected encoding error'),
    output: (_, metadata) => {
      assert_not_equals(metadata, null);
      if (metadata.decoderConfig)
        description = metadata.decoderConfig.description;
    },
  };

  let encoder = new VideoEncoder(codecInit);
  encoder.configure(encoderConfig);

  let frame1 = createFrame(encoderConfig.width, encoderConfig.height, 0);
  let frame2 = createFrame(encoderConfig.width, encoderConfig.height, 33333);
  t.add_cleanup(() => {
    frame1.close();
    frame2.close();
  });

  encoder.encode(frame1);
  encoder.encode(frame2);

  await encoder.flush();

  assert_not_equals(description, null);
  assert_not_equals(description.length, 0);

  // Profile is a hex code at xx in a codec string of form "avc1.xxyyzz".
  let expectedProfileIndication = parseInt(codecString.substring(5, 7), 16);

  // See AVCDecoderConfigurationRecord in ISO/IEC 14496-15 for details.
  // https://www.w3.org/TR/webcodecs-avc-codec-registration/#dom-avcbitstreamformat-avc
  let profileIndication = new Uint8Array(description)[1];
  assert_equals(profileIndication, expectedProfileIndication);
}, 'Test that encoding with a specific H264 profile actually produces that profile.');

const timestampCodec = {
  '?baseline': 'avc1.42001f',
  '?main': 'avc1.4d001f',
  '?high': 'avc1.64001f',
}[location.search];

if (timestampCodec) {
  promise_test(async t => {
    const config = {
      codec: timestampCodec,
      width: 640,
      height: 480,
      bitrate: 1000000,
      latencyMode: 'quality',
      hardwareAcceleration: 'prefer-software',
    };
    const support = await VideoEncoder.isConfigSupported(config);
    assert_implements_optional(support.supported, 'H264 configuration unsupported');
    const input = [
      {timestamp: 10000, duration: 500},
      {timestamp: 12000, duration: 250},
      {timestamp: 17000, duration: 750},
      {timestamp: 24000, duration: 33333, keyFrame: true},
      {timestamp: 60000, duration: 16667},
      {timestamp: 80000, duration: 50000},
      {timestamp: 140000, duration: 1000},
      {timestamp: 145000, duration: 2000},
    ];
    const expected = new Map(input.map((frame, index) =>
      [frame.timestamp, {...frame, dots: index + 1}]));
    const decoded = [];
    const chunks = [];
    const diagnostics = () => JSON.stringify({config, input, output: chunks});
    const decoder = new VideoDecoder({
      error: t.unreached_func('Unexpected decoding error'),
      output: t.step_func(frame => {
        try {
          const source = expected.get(frame.timestamp);
          assert_true(!!source, 'Decoded timestamp identifies an input: ' + diagnostics());
          assert_true(validateBlackDots(frame, source.dots),
                      'Decoded picture matches its timestamp: ' + diagnostics());
          assert_false(validateBlackDots(frame, source.dots + 1),
                       'Decoded picture has no extra dots: ' + diagnostics());
          decoded.push(frame.timestamp);
        } finally {
          frame.close();
        }
      }),
    });
    const encoder = new VideoEncoder({
      error: t.unreached_func('Unexpected encoding error'),
      output: t.step_func((chunk, metadata) => {
        chunks.push({timestamp: chunk.timestamp, duration: chunk.duration});
        const source = expected.get(chunk.timestamp);
        assert_true(!!source, 'Encoded timestamp identifies an input: ' + diagnostics());
        assert_equals(chunk.duration, source.duration,
                      'Encoded duration belongs to its timestamp: ' + diagnostics());
        if (metadata.decoderConfig) {
          decoder.configure(metadata.decoderConfig);
        }
        decoder.decode(chunk);
      }),
    });
    t.add_cleanup(() => {
      if (encoder.state !== 'closed') encoder.close();
      if (decoder.state !== 'closed') decoder.close();
    });
    encoder.configure(config);
    for (let index = 0; index < input.length; ++index) {
      const source = input[index];
      const dotted = createDottedFrame(config.width, config.height, index + 1,
                                      source.timestamp);
      const frame = new VideoFrame(dotted, {duration: source.duration});
      dotted.close();
      try {
        encoder.encode(frame, {keyFrame: source.keyFrame ?? false});
      } finally {
        frame.close();
      }
    }
    await encoder.flush();
    await decoder.flush();
    assert_array_equals(decoded.sort((a, b) => a - b), input.map(frame => frame.timestamp),
                        'All pictures retain their timestamps: ' + diagnostics());
  }, 'H264 encoding preserves timestamp-to-picture association with reordered frames');
  const timingCases = [
    {
      name: 'zero start',
      frames: [{timestamp: 0, duration: 500}, {timestamp: 500, duration: 250}],
    },
    {
      name: 'nonzero start and gaps',
      frames: [
        {timestamp: 10000, duration: 500},
        {timestamp: 12000, duration: 250},
        {timestamp: 17000, duration: 750},
      ],
    },
    {
      name: 'variable durations and a forced keyframe',
      frames: [
        {timestamp: 10000, duration: 500},
        {timestamp: 12000, duration: 250},
        {timestamp: 17000, duration: 750},
        {timestamp: 24000, duration: 33333, keyFrame: true},
        {timestamp: 60000, duration: 16667},
        {timestamp: 80000, duration: 50000},
        {timestamp: 140000, duration: 1000},
        {timestamp: 145000, duration: 2000},
      ],
    },
    {
      name: 'equal frame end timestamps',
      frames: [
        {timestamp: 10000, duration: 1000},
        {timestamp: 10500, duration: 500},
        {timestamp: 17000, duration: 750},
      ],
    },
    {
      name: 'zero durations',
      frames: [
        {timestamp: 10000, duration: 0},
        {timestamp: 12000, duration: 0},
        {timestamp: 17000, duration: 0},
      ],
    },
    {
      name: 'unset durations',
      frames: [{timestamp: 10000}, {timestamp: 12000}, {timestamp: 17000}],
    },
    {
      name: 'an initial zero duration',
      frames: [
        {timestamp: 10000, duration: 0},
        {timestamp: 12000, duration: 250},
        {timestamp: 17000, duration: 750},
      ],
    },
    {
      name: 'negative start and gaps',
      frames: [
        {timestamp: -10000, duration: 500},
        {timestamp: -8000, duration: 250},
        {timestamp: -3000, duration: 750},
      ],
    },
    {
      name: 'nonzero start and gaps in realtime',
      latencyMode: 'realtime',
      frames: [
        {timestamp: 10000, duration: 500},
        {timestamp: 12000, duration: 250},
        {timestamp: 17000, duration: 750},
      ],
    },
  ];
  for (const hardwareAcceleration of ['no-preference', 'prefer-software']) {
    for (const timing of timingCases) {
      promise_test(async t => {
        const config = {
          codec: timestampCodec,
          width: 1280,
          height: 720,
          latencyMode: timing.latencyMode ?? 'quality',
          hardwareAcceleration,
        };
        const support = await VideoEncoder.isConfigSupported(config);
        assert_implements_optional(support.supported, 'H264 configuration unsupported');

        const chunks = [];
        const encoder = new VideoEncoder({
          error: t.unreached_func('Unexpected encoding error'),
          output: chunk => chunks.push({
            timestamp: chunk.timestamp,
            duration: chunk.duration,
          }),
        });
        t.add_cleanup(() => {
          if (encoder.state !== 'closed') {
            encoder.close();
          }
        });
        encoder.configure(config);
        for (const input of timing.frames) {
          const frame = createFrame(config.width, config.height, input.timestamp,
                                    {duration: input.duration});
          try {
            encoder.encode(frame, {keyFrame: input.keyFrame ?? false});
          } finally {
            frame.close();
          }
        }
        await encoder.flush();
        const timestamps = chunks.map(chunk => chunk.timestamp).sort((a, b) => a - b);
        let expected = timing.frames.map(frame => frame.timestamp);
        if (config.latencyMode === 'realtime') {
          assert_greater_than(chunks.length, 0, 'At least one frame is encoded');
          expected = expected.filter(timestamp => timestamps.includes(timestamp));
        }
        assert_array_equals(timestamps, expected,
                            'Encoded chunks preserve input timestamps: ' +
                            JSON.stringify({config, input: timing.frames, output: chunks}));
      }, `H264 encoding preserves timestamps with ${timing.name}, ${hardwareAcceleration}`);
    }
  }
}
