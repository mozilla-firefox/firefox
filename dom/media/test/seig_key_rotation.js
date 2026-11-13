/* import-globals-from eme.js */

// Each fixture has four 1 s fragments. Fragments 1-2 use the tenc defaults
// with KID A. Fragments 3-4 are encrypted with key B and map every sample to
// a traf-level seig sample group entry with KID B.
const kSeigRotationTime = 2;
const kSeigKeyIdA = "aaaaaaaa111111110000000000000001";
const kSeigKeyIdB = "bbbbbbbb222222220000000000000002";
const kSeigKeys = {
  [kSeigKeyIdA]: "3a7f2c19d84e6b05a1c3e7f9024b68d1",
  [kSeigKeyIdB]: "c5e81d4b7a2f9036e4d1b8c7f50a2639",
};
const kSeigFixtures = [
  { scheme: "cenc", file: "seig-key-rotation-cenc.mp4" },
  { scheme: "cbcs", file: "seig-key-rotation-cbcs.mp4" },
];
const kSeigVideoType = 'video/mp4; codecs="avc1.4d400d"';

// Provisions the keys in aKeyStore before any media is appended, so that a
// waitingforkey event can only come from a sample whose key was not given.
// The returned firstEvent resolves with the first of aEvents fired after the
// setup.
async function startSeigRotationPlayback(aFixture, aKeyStore, aEvents) {
  const access = await navigator.requestMediaKeySystemAccess(
    "org.w3.clearkey",
    [
      {
        initDataTypes: ["cenc"],
        videoCapabilities: [{ contentType: kSeigVideoType }],
      },
    ]
  );
  const mediaKeys = await access.createMediaKeys();
  const video = document.createElement("video");
  document.body.appendChild(video);
  await video.setMediaKeys(mediaKeys);

  const session = mediaKeys.createSession("temporary");
  const keysUsable = once(session, "keystatuseschange");
  const message = once(session, "message");
  await session.generateRequest(
    "cenc",
    commonSystemPssh(Object.keys(aKeyStore))
  );
  await session.update(
    GenerateClearKeyLicense((await message).message, aKeyStore)
  );
  await keysUsable;

  const mediaSource = new MediaSource();
  video.src = URL.createObjectURL(mediaSource);
  await once(mediaSource, "sourceopen");
  const sourceBuffer = mediaSource.addSourceBuffer(kSeigVideoType);
  const data = await fetchWithXHR(aFixture.file);
  sourceBuffer.appendBuffer(new Uint8Array(data));
  await once(sourceBuffer, "updateend");
  mediaSource.endOfStream();
  const firstEvent = firstEventOf(video, aEvents);
  await video.play();
  return { video, session, firstEvent };
}

// A version 1 pssh box of the Common system that lists aKeyIds.
function commonSystemPssh(aKeyIds) {
  const systemId = "1077efecc0b24d02ace33c1e52e2fb4b";
  const size = 32 + 16 * aKeyIds.length + 4;
  const box = new Uint8Array(size);
  const view = new DataView(box.buffer);
  view.setUint32(0, size);
  box.set(new TextEncoder().encode("pssh"), 4);
  view.setUint32(8, 0x01000000);
  box.set(hexToBytes(systemId), 12);
  view.setUint32(28, aKeyIds.length);
  aKeyIds.forEach((kid, i) => box.set(hexToBytes(kid), 32 + 16 * i));
  return box;
}

function hexToBytes(aHex) {
  return Uint8Array.from(aHex.match(/../g), byte => parseInt(byte, 16));
}

function addKeyToSession(aSession, aKeyId) {
  const license = {
    keys: [
      {
        kty: "oct",
        kid: HexToBase64(aKeyId),
        k: HexToBase64(kSeigKeys[aKeyId]),
      },
    ],
    type: "temporary",
  };
  return aSession.update(new TextEncoder().encode(JSON.stringify(license)));
}

function removeSeigVideo(aVideo) {
  aVideo.removeAttribute("src");
  aVideo.load();
  aVideo.remove();
}

// Resolves with the name of the first of aEvents that aVideo fires.
function firstEventOf(aVideo, aEvents) {
  return Promise.race(aEvents.map(name => once(aVideo, name).then(_ => name)));
}
