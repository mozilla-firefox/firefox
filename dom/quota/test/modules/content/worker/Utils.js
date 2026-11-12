/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

let UtilsChild;

async function ensureUtilsChild() {
  if (UtilsChild) {
    return;
  }

  const { UtilsChild: importedUtilsChild } =
    await import("/tests/dom/quota/test/modules/worker/UtilsChild.mjs");

  UtilsChild = importedUtilsChild;
}

const Utils = {
  async getCachedOriginUsage() {
    await ensureUtilsChild();

    const result = await UtilsChild.getCachedOriginUsage();
    return result;
  },

  async isPrivateBrowsing() {
    await ensureUtilsChild();

    const result = await UtilsChild.isPrivateBrowsing();
    return result;
  },
};
