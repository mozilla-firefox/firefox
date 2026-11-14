/* exported assertBlocked */

async function assertThrowsSecurityError(label, read) {
  let name = null;
  try {
    await read();
  } catch (e) {
    name = e.name;
  }
  is(name, "SecurityError", `${label}: read blocked with SecurityError`);
}

// Every read path must throw SecurityError when the element is not origin-clean.
async function assertBlocked(v, label) {
  ok(
    v.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA,
    `${label}: element has a current frame to read`
  );
  await assertThrowsSecurityError(`${label} canvas.getImageData`, () => {
    const c = document.createElement("canvas");
    c.width = c.height = 16;
    const ctx = c.getContext("2d");
    ctx.drawImage(v, 0, 0, 16, 16);
    ctx.getImageData(0, 0, 16, 16);
  });
  await assertThrowsSecurityError(`${label} new VideoFrame()`, () => {
    const f = new VideoFrame(v);
    f.close();
  });
  await assertThrowsSecurityError(`${label} createImageBitmap`, async () => {
    const bmp = await createImageBitmap(v);
    const c = document.createElement("canvas");
    c.width = bmp.width;
    c.height = bmp.height;
    const ctx = c.getContext("2d");
    ctx.drawImage(bmp, 0, 0);
    ctx.getImageData(0, 0, bmp.width, bmp.height);
  });
}
