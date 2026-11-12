/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

function _sendMessage(messageBody) {
  const messageHeader = {
    moduleName: "UtilsParent",
    objectName: "UtilsParent",
  };

  const message = { ...messageHeader, ...messageBody };

  postMessage(message);
}

function _recvMessage() {
  return new Promise(function (resolve) {
    addEventListener("message", async function onMessage(event) {
      removeEventListener("message", onMessage);
      const data = event.data;
      resolve(data);
    });
  });
}

export const UtilsChild = {
  async getCachedOriginUsage() {
    _sendMessage({
      op: "getCachedOriginUsage",
    });

    return _recvMessage();
  },

  async isPrivateBrowsing() {
    _sendMessage({
      op: "isPrivateBrowsing",
    });

    return _recvMessage();
  },

  async shrinkStorageSize(size) {
    _sendMessage({
      op: "shrinkStorageSize",
      size,
    });

    return _recvMessage();
  },

  async restoreStorageSize() {
    _sendMessage({
      op: "restoreStorageSize",
    });

    return _recvMessage();
  },
};
