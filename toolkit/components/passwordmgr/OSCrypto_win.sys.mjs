/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { ctypes } from "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs";

const FLAGS_NOT_SET = 0;

const wintypes = {
  BOOL: ctypes.bool,
  BYTE: ctypes.uint8_t,
  DWORD: ctypes.uint32_t,
  PBYTE: ctypes.unsigned_char.ptr,
  PCHAR: ctypes.char.ptr,
  PDWORD: ctypes.uint32_t.ptr,
  PVOID: ctypes.voidptr_t,
  WORD: ctypes.uint16_t,
};

export function OSCrypto() {
  this._structs = {};
  this._functions = new Map();
  this._libs = new Map();
  this._structs.DATA_BLOB = new ctypes.StructType("DATA_BLOB", [
    { cbData: wintypes.DWORD },
    { pbData: wintypes.PVOID },
  ]);

  try {
    this._libs.set("crypt32", ctypes.open("Crypt32"));
    this._libs.set("kernel32", ctypes.open("Kernel32"));

    this._functions.set(
      "CryptProtectData",
      this._libs
        .get("crypt32")
        .declare(
          "CryptProtectData",
          ctypes.winapi_abi,
          wintypes.DWORD,
          this._structs.DATA_BLOB.ptr,
          wintypes.PVOID,
          wintypes.PVOID,
          wintypes.PVOID,
          wintypes.PVOID,
          wintypes.DWORD,
          this._structs.DATA_BLOB.ptr
        )
    );
    this._functions.set(
      "CryptUnprotectData",
      this._libs
        .get("crypt32")
        .declare(
          "CryptUnprotectData",
          ctypes.winapi_abi,
          wintypes.DWORD,
          this._structs.DATA_BLOB.ptr,
          wintypes.PVOID,
          wintypes.PVOID,
          wintypes.PVOID,
          wintypes.PVOID,
          wintypes.DWORD,
          this._structs.DATA_BLOB.ptr
        )
    );
    this._functions.set(
      "LocalFree",
      this._libs
        .get("kernel32")
        .declare("LocalFree", ctypes.winapi_abi, wintypes.DWORD, wintypes.PVOID)
    );
  } catch (ex) {
    console.error(ex);
    this.finalize();
    throw ex;
  }
}

OSCrypto.prototype = {
  /**
   * Convert an array containing only two bytes unsigned numbers to a string.
   *
   * @param {number[]} arr - the array that needs to be converted.
   * @returns {string} the string representation of the array.
   */
  arrayToString(arr) {
    let str = "";
    for (let i = 0; i < arr.length; i++) {
      str += String.fromCharCode(arr[i]);
    }
    return str;
  },

  /**
   * Convert a string to an array.
   *
   * @param {string} str - the string that needs to be converted.
   * @returns {number[]} the array representation of the string.
   */
  stringToArray(str) {
    let arr = [];
    for (let i = 0; i < str.length; i++) {
      arr.push(str.charCodeAt(i));
    }
    return arr;
  },

  /**
   * Calculate the hash value used by IE as the name of the registry value where login details are
   * stored.
   *
   * @param {string} data - the string value that needs to be hashed.
   * @returns {string} the hash value of the string.
   */
  getIELoginHash(data) {
    // return the two-digit hexadecimal code for a byte
    function toHexString(charCode) {
      return ("00" + charCode.toString(16)).slice(-2);
    }

    // the data needs to be encoded in null terminated UTF-16
    data += "\0";

    // dataArray is an array of bytes
    let dataArray = new Array(data.length * 2);
    for (let i = 0; i < data.length; i++) {
      let c = data.charCodeAt(i);
      let lo = c & 0xff;
      let hi = (c & 0xff00) >> 8;
      dataArray[i * 2] = lo;
      dataArray[i * 2 + 1] = hi;
    }

    // calculation of SHA1 hash value
    let cryptoHash = Cc["@mozilla.org/security/hash;1"].createInstance(
      Ci.nsICryptoHash
    );
    cryptoHash.init(cryptoHash.SHA1);
    cryptoHash.update(dataArray, dataArray.length);
    let hash = cryptoHash.finish(false);

    let tail = 0; // variable to calculate value for the last 2 bytes
    // convert to a character string in hexadecimal notation
    for (let c of hash) {
      tail += c.charCodeAt(0);
    }
    hash += String.fromCharCode(tail % 256);

    // convert the binary hash data to a hex string.
    let hashStr = Array.from(hash, (c, i) =>
      toHexString(hash.charCodeAt(i))
    ).join("");
    return hashStr.toUpperCase();
  },

  _getEntropyParam(entropy) {
    if (!entropy) {
      return null;
    }
    let entropyArray = this.stringToArray(entropy);
    entropyArray.push(0); // Null-terminate the data.
    let entropyData = wintypes.WORD.array(entropyArray.length)(entropyArray);
    let optionalEntropy = new this._structs.DATA_BLOB(
      entropyData.length * wintypes.WORD.size,
      entropyData
    );
    return optionalEntropy.address();
  },

  _convertWinArrayToJSArray(dataBlob) {
    // Convert DATA_BLOB to JS accessible array
    return ctypes.cast(
      dataBlob.pbData,
      wintypes.BYTE.array(dataBlob.cbData).ptr
    ).contents;
  },

  /**
   * Decrypt a string using the windows CryptUnprotectData API.
   *
   * @param {string} data - the encrypted string that needs to be decrypted.
   * @param {?string} entropy - the entropy value of the decryption (could be null). Its value must
   * be the same as the one used when the data was encrypted.
   * @param {?string} output - how to return the decrypted data default string
   * @returns {string|Uint8Array} the decryption of the string.
   */
  decryptData(data, entropy = null, output = "string") {
    let array = this.stringToArray(data);
    let encryptedData = wintypes.BYTE.array(array.length)(array);
    let inData = new this._structs.DATA_BLOB(
      encryptedData.length,
      encryptedData
    );
    let outData = new this._structs.DATA_BLOB();
    let entropyParam = this._getEntropyParam(entropy);

    try {
      let status = this._functions.get("CryptUnprotectData")(
        inData.address(),
        null,
        entropyParam,
        null,
        null,
        FLAGS_NOT_SET,
        outData.address()
      );
      if (status === 0) {
        throw new Error("decryptData failed: " + ctypes.winLastError);
      }

      let decrypted = this._convertWinArrayToJSArray(outData);

      // Return raw bytes to handle non-string results
      const bytes = new Uint8Array(decrypted);
      if (output === "bytes") {
        return bytes;
      }

      // Output that may include characters outside of the 0-255 (byte) range needs to use TextDecoder.
      return new TextDecoder().decode(bytes);
    } finally {
      if (outData.pbData) {
        this._functions.get("LocalFree")(outData.pbData);
      }
    }
  },

  /**
   * Encrypt a string using the windows CryptProtectData API.
   *
   * @param {string} data - the string that is going to be encrypted.
   * @param {?string} entropy - the entropy value of the encryption (could be null). Its value must
   * be the same as the one that is going to be used for the decryption.
   * @returns {string} the encrypted string.
   */
  encryptData(data, entropy = null) {
    // Input that may include characters outside of the 0-255 (byte) range needs to use TextEncoder.
    let decryptedByteData = [...new TextEncoder().encode(data)];
    let decryptedData = wintypes.BYTE.array(decryptedByteData.length)(
      decryptedByteData
    );

    let inData = new this._structs.DATA_BLOB(
      decryptedData.length,
      decryptedData
    );
    let outData = new this._structs.DATA_BLOB();
    let entropyParam = this._getEntropyParam(entropy);

    try {
      let status = this._functions.get("CryptProtectData")(
        inData.address(),
        null,
        entropyParam,
        null,
        null,
        FLAGS_NOT_SET,
        outData.address()
      );
      if (status === 0) {
        throw new Error("encryptData failed: " + ctypes.winLastError);
      }

      let encrypted = this._convertWinArrayToJSArray(outData);
      return this.arrayToString(encrypted);
    } finally {
      if (outData.pbData) {
        this._functions.get("LocalFree")(outData.pbData);
      }
    }
  },

  /**
   * Must be invoked once after last use of any of the provided helpers.
   */
  finalize() {
    this._structs = {};
    this._functions.clear();
    for (let lib of this._libs.values()) {
      try {
        lib.close();
      } catch (ex) {
        console.error(ex);
      }
    }
    this._libs.clear();
  },
};
