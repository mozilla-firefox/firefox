/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

import { Utils } from "./Utils.mjs";

export const UtilsParent = {
  async OnMessageReceived(worker, msg) {
    switch (msg.op) {
      case "getCachedOriginUsage": {
        const result = await Utils.getCachedOriginUsage();
        worker.postMessage(result);
        break;
      }

      case "isPrivateBrowsing": {
        const result = await Utils.isPrivateBrowsing();
        worker.postMessage(result);
        break;
      }

      default:
        throw new Error(`Unknown op ${msg.op}`);
    }
  },
};
