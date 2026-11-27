/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Module for reading Property Lists (.plist) files
 * ------------------------------------------------
 * This module functions as a reader for Apple Property Lists (.plist files).
 * It supports both binary and xml formatted property lists.  It does not
 * support the legacy ASCII format.  Reading of Cocoa's Keyed Archives serialized
 * to binary property lists isn't supported either.
 *
 * Property Lists objects are represented by standard JS and Mozilla types,
 * namely:
 *
 * XML type            Cocoa Class    Returned type(s)
 * --------------------------------------------------------------------------
 * <true/> / <false/>  NSNumber       TYPE_PRIMITIVE    boolean
 * <integer> / <real>  NSNumber       TYPE_PRIMITIVE    number
 *                                    TYPE_INT64        String [1]
 * Not Available       NSNull         TYPE_PRIMITIVE    null   [2]
 *                                    TYPE_PRIMITIVE    undefined [3]
 * <date/>             NSDate         TYPE_DATE         Date
 * <data/>             NSData         TYPE_UINT8_ARRAY  Uint8Array
 * <array/>            NSArray        TYPE_ARRAY        Array
 * Not Available       NSSet          TYPE_ARRAY        Array  [2][4]
 * <dict/>             NSDictionary   TYPE_DICTIONARY   Map
 *
 * Use PropertyListUtils.getObjectType to detect the type of a Property list
 * object.
 *
 * -------------
 * 1) Property lists supports storing U/Int64 numbers, while JS can only handle
 *    numbers that are in this limits of float-64 (±2^53).  For numbers that
 *    do not outbound this limits, simple primitive number are always used.
 *    Otherwise, a String object.
 * 2) About NSNull and NSSet values: While the xml format has no support for
 *    representing null and set values, the documentation for the binary format
 *    states that it supports storing both types.  However, the Cocoa APIs for
 *    serializing property lists do not seem to support either types (test with
 *    NSPropertyListSerialization::propertyList:isValidForFormat). Furthermore,
 *    if an array or a dictionary (Map) contains a NSNull or a NSSet value, they cannot
 *    be serialized to a property list.
 *    As for usage within OS X, not surprisingly there's no known usage of
 *    storing either of these types in a property list.  It seems that, for now,
 *    Apple is keeping the features of binary and xml formats in sync, probably as
 *    long as the XML format is not officially deprecated.
 * 3) Not used anywhere.
 * 4) About NSSet representation: For the time being, we represent those
 *    theoretical NSSet objects the same way NSArray is represented.
 *    While this would most certainly work, it is not the right way to handle
 *    it.  A more correct representation for a set is a js generator, which would
 *    read the set lazily and has no indices semantics.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ctypes: "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs",
});

export var PropertyListUtils = Object.freeze({
  /**
   * Asynchronously reads a file as a property list.
   *
   * @param aFile (Blob/nsIFile)
   *        the file to be read as a property list.
   * @param aCallback
   *        If the property list is read successfully, aPropertyListRoot is set
   *        to the root object of the property list.
   *        Use getPropertyListObjectType to detect its type.
   *        If it's not read successfully, aPropertyListRoot is set to null.
   *        The reaon for failure is reported to the Error Console.
   */
  read: function PLU_read(aFile, aCallback) {
    if (!(aFile instanceof Ci.nsIFile || File.isInstance(aFile))) {
      throw new Error("aFile is not a file object");
    }
    if (typeof aCallback != "function") {
      throw new Error("Invalid value for aCallback");
    }

    // We guarantee not to throw directly for any other exceptions, and always
    // call aCallback.
    Services.tm.dispatchToMainThread(() => {
      let self = this;
      function readDOMFile(aFile) {
        let fileReader = new FileReader();
        let onLoadEnd = function () {
          let root = null;
          try {
            fileReader.removeEventListener("loadend", onLoadEnd);
            if (fileReader.readyState != fileReader.DONE) {
              throw new Error(
                "Could not read file contents: " + fileReader.error
              );
            }

            root = self._readFromArrayBufferSync(fileReader.result);
          } finally {
            aCallback(root);
          }
        };
        fileReader.addEventListener("loadend", onLoadEnd);
        fileReader.readAsArrayBuffer(aFile);
      }

      try {
        if (aFile instanceof Ci.nsIFile) {
          if (!aFile.exists()) {
            throw new Error("The file pointed by aFile does not exist");
          }

          File.createFromNsIFile(aFile).then(function (aFile) {
            readDOMFile(aFile);
          });
          return;
        }
        readDOMFile(aFile);
      } catch (ex) {
        aCallback(null);
        throw ex;
      }
    });
  },

  /**
   * DO NOT USE ME.  Once Bug 718189 is fixed, this method won't be public.
   *
   * Synchronously read an ArrayBuffer contents as a property list.
   */
  _readFromArrayBufferSync: function PLU__readFromArrayBufferSync(aBuffer) {
    if (BinaryPropertyListReader.prototype.canProcess(aBuffer)) {
      return new BinaryPropertyListReader(aBuffer).root;
    }

    // Convert the buffer into an XML tree.
    let domParser = new DOMParser();
    let bytesView = new Uint8Array(aBuffer);
    try {
      let doc = domParser.parseFromBuffer(bytesView, "application/xml");
      return new XMLPropertyListReader(doc).root;
    } catch (ex) {
      throw new Error("aBuffer cannot be parsed as a DOM document: " + ex);
    }
  },

  TYPE_PRIMITIVE: 0,
  TYPE_DATE: 1,
  TYPE_UINT8_ARRAY: 2,
  TYPE_ARRAY: 3,
  TYPE_DICTIONARY: 4,
  TYPE_INT64: 5,

  /**
   * Get the type in which the given property list object is represented.
   * Check the header for the mapping between the TYPE* constants to js types
   * and objects.
   *
   * @return one of the TYPE_* constants listed above.
   * Note: this method is merely for convenience.  It has no magic to detect
   * that aObject is indeed a property list object created by this module.
   */
  getObjectType: function PLU_getObjectType(aObject) {
    if (aObject === null || typeof aObject != "object") {
      return this.TYPE_PRIMITIVE;
    }

    // Given current usage, we could assume that aObject was created in the
    // scope of this module, but in future, this util may be used as part of
    // serializing js objects to a property list - in which case the object
    // would most likely be created in the caller's scope.
    let global = Cu.getGlobalForObject(aObject);

    if (aObject instanceof global.Map) {
      return this.TYPE_DICTIONARY;
    }
    if (Array.isArray(aObject)) {
      return this.TYPE_ARRAY;
    }
    if (aObject instanceof global.Date) {
      return this.TYPE_DATE;
    }
    if (aObject instanceof global.Uint8Array) {
      return this.TYPE_UINT8_ARRAY;
    }
    if (aObject instanceof global.String && "__INT_64_WRAPPER__" in aObject) {
      return this.TYPE_INT64;
    }

    throw new Error("aObject is not as a property list object.");
  },

  /**
   * Wraps a 64-bit stored in the form of a string primitive as a String object,
   * which we can later distiguish from regular string values.
   *
   * @param aPrimitive
   *        a number in the form of either a primitive string or a primitive number.
   * @return a String wrapper around aNumberStr that can later be identified
   * as holding 64-bit number using getObjectType.
   */
  wrapInt64: function PLU_wrapInt64(aPrimitive) {
    if (typeof aPrimitive != "string" && typeof aPrimitive != "number") {
      throw new Error("aPrimitive should be a string primitive");
    }

    // The function converts string or number to object
    // So eslint rule is disabled
    // eslint-disable-next-line no-new-wrappers
    let wrapped = new String(aPrimitive);
    Object.defineProperty(wrapped, "__INT_64_WRAPPER__", { value: true });
    return wrapped;
  },
});

/**
 * Here's the base structure of binary-format property lists.
 * 1) Header - magic number
 *   - 6 bytes - "bplist"
 *   - 2 bytes - version number. This implementation only supports version 00.
 * 2) Objects Table
 *    Variable-sized objects, see _readObject for how various types of objects
 *    are constructed.
 * 3) Offsets Table
 *    The offset of each object in the objects table. The integer size is
 *    specified in the trailer.
 * 4) Trailer
 *    - 6 unused bytes
 *    - 1 byte:  the size of integers in the offsets table
 *    - 1 byte:  the size of object references for arrays, sets and
 *               dictionaries.
 *    - 8 bytes: the number of objects in the objects table
 *    - 8 bytes: the index of the root object's offset in the offsets table.
 *    - 8 bytes: the offset of the offsets table.
 *
 * Note: all integers are stored in big-endian form.
 */

/**
 * Reader for binary-format property lists.
 *
 * @param aBuffer
 *        ArrayBuffer object from which the binary plist should be read.
 */
function BinaryPropertyListReader(aBuffer) {
  this._dataView = new DataView(aBuffer);

  const JS_MAX_INT = Math.pow(2, 53);
  this._JS_MAX_INT_SIGNED = lazy.ctypes.Int64(JS_MAX_INT);
  this._JS_MAX_INT_UNSIGNED = lazy.ctypes.UInt64(JS_MAX_INT);
  this._JS_MIN_INT = lazy.ctypes.Int64(-JS_MAX_INT);

  try {
    this._readTrailerInfo();
    this._readObjectsOffsets();
  } catch (ex) {
    throw new Error("Could not read aBuffer as a binary property list");
  }
  this._objects = [];
}

BinaryPropertyListReader.prototype = {
  /**
   * Checks if the given ArrayBuffer can be read as a binary property list.
   * It can be called on the prototype.
   */
  canProcess: function BPLR_canProcess(aBuffer) {
    return (
      Array.from(new Uint8Array(aBuffer, 0, 8))
        .map(c => String.fromCharCode(c))
        .join("") == "bplist00"
    );
  },

  get root() {
    return this._readObject(this._rootObjectIndex);
  },

  _readTrailerInfo: function BPLR__readTrailer() {
    // The first 6 bytes of the 32-bytes trailer are unused
    let trailerOffset = this._dataView.byteLength - 26;
    [this._offsetTableIntegerSize, this._objectRefSize] =
      this._readUnsignedInts(trailerOffset, 1, 2);

    [this._numberOfObjects, this._rootObjectIndex, this._offsetTableOffset] =
      this._readUnsignedInts(trailerOffset + 2, 8, 3);
  },

  _readObjectsOffsets: function BPLR__readObjectsOffsets() {
    this._offsetTable = this._readUnsignedInts(
      this._offsetTableOffset,
      this._offsetTableIntegerSize,
      this._numberOfObjects
    );
  },

  _readSignedInt64: function BPLR__readSignedInt64(aByteOffset) {
    let lo = this._dataView.getUint32(aByteOffset + 4);
    let hi = this._dataView.getInt32(aByteOffset);
    let int64 = lazy.ctypes.Int64.join(hi, lo);
    if (
      lazy.ctypes.Int64.compare(int64, this._JS_MAX_INT_SIGNED) == 1 ||
      lazy.ctypes.Int64.compare(int64, this._JS_MIN_INT) == -1
    ) {
      return PropertyListUtils.wrapInt64(int64.toString());
    }

    return parseInt(int64.toString(), 10);
  },

  _readReal: function BPLR__readReal(aByteOffset, aRealSize) {
    if (aRealSize == 4) {
      return this._dataView.getFloat32(aByteOffset);
    }
    if (aRealSize == 8) {
      return this._dataView.getFloat64(aByteOffset);
    }

    throw new Error("Unsupported real size: " + aRealSize);
  },

  OBJECT_TYPE_BITS: {
    SIMPLE: parseInt("0000", 2),
    INTEGER: parseInt("0001", 2),
    REAL: parseInt("0010", 2),
    DATE: parseInt("0011", 2),
    DATA: parseInt("0100", 2),
    ASCII_STRING: parseInt("0101", 2),
    UNICODE_STRING: parseInt("0110", 2),
    UID: parseInt("1000", 2),
    ARRAY: parseInt("1010", 2),
    SET: parseInt("1100", 2),
    DICTIONARY: parseInt("1101", 2),
  },

  ADDITIONAL_INFO_BITS: {
    // Applies to OBJECT_TYPE_BITS.SIMPLE
    NULL: parseInt("0000", 2),
    FALSE: parseInt("1000", 2),
    TRUE: parseInt("1001", 2),
    FILL_BYTE: parseInt("1111", 2),
    // Applies to OBJECT_TYPE_BITS.DATE
    DATE: parseInt("0011", 2),
    // Applies to OBJECT_TYPE_BITS.DATA, ASCII_STRING, UNICODE_STRING, ARRAY,
    // SET and DICTIONARY.
    LENGTH_INT_SIZE_FOLLOWS: parseInt("1111", 2),
  },

  /**
   * Returns an object descriptor in the form of two integers: object type and
   * additional info.
   *
   * @param aByteOffset
   *        the descriptor's offset.
   * @return [objType, additionalInfo] - the object type and additional info.
   * @see OBJECT_TYPE_BITS and ADDITIONAL_INFO_BITS
   */
  _readObjectDescriptor: function BPLR__readObjectDescriptor(aByteOffset) {
    // The first four bits hold the object type.  For some types, additional
    // info is held in the other 4 bits.
    let byte = this._readUnsignedInts(aByteOffset, 1, 1)[0];
    return [(byte & 0xf0) >> 4, byte & 0x0f];
  },

  _readDate: function BPLR__readDate(aByteOffset) {
    // That's the reference date of NSDate.
    let date = new Date("1 January 2001, GMT");

    // NSDate values are float values, but setSeconds takes an integer.
    date.setMilliseconds(this._readReal(aByteOffset, 8) * 1000);
    return date;
  },

  /**
   * Reads a portion of the buffer as a string.
   *
   * @param aByteOffset
   *        The offset in the buffer at which the string starts
   * @param aNumberOfChars
   *        The length of the string to be read (that is the number of
   *        characters, not bytes).
   * @param aUnicode
   *        Whether or not it is a unicode string.
   * @return the string read.
   *
   * Note: this is tested to work well with unicode surrogate pairs.  Because
   * all unicode characters are read as 2-byte integers, unicode surrogate
   * pairs are read from the buffer in the form of two integers, as required
   * by String.fromCharCode.
   */
  _readString: function BPLR__readString(
    aByteOffset,
    aNumberOfChars,
    aUnicode
  ) {
    let codes = this._readUnsignedInts(
      aByteOffset,
      aUnicode ? 2 : 1,
      aNumberOfChars
    );
    return codes.map(c => String.fromCharCode(c)).join("");
  },

  /**
   * Reads an array of unsigned integers from the buffer.  Integers larger than
   * one byte are read in big endian form.
   *
   * @param aByteOffset
   *        The offset in the buffer at which the array starts.
   * @param aIntSize
   *        The size of each int in the array.
   * @param aLength
   *        The number of ints in the array.
   * @param [optional] aBigIntAllowed (default: false)
   *        Whether or not to accept integers which outbounds JS limits for
   *        numbers (±2^53) in the form of a String.
   * @return an array of integers (number primitive and/or Strings for large
   * numbers (see header)).
   * @throws if aBigIntAllowed is false and one of the integers in the array
   * cannot be represented by a primitive js number.
   */
  _readUnsignedInts: function BPLR__readUnsignedInts(
    aByteOffset,
    aIntSize,
    aLength,
    aBigIntAllowed
  ) {
    let uints = [];
    for (
      let offset = aByteOffset;
      offset < aByteOffset + aIntSize * aLength;
      offset += aIntSize
    ) {
      if (aIntSize == 1) {
        uints.push(this._dataView.getUint8(offset));
      } else if (aIntSize == 2) {
        uints.push(this._dataView.getUint16(offset));
      } else if (aIntSize == 3) {
        let int24 = Uint8Array(4);
        int24[3] = 0;
        int24[2] = this._dataView.getUint8(offset);
        int24[1] = this._dataView.getUint8(offset + 1);
        int24[0] = this._dataView.getUint8(offset + 2);
        uints.push(Uint32Array(int24.buffer)[0]);
      } else if (aIntSize == 4) {
        uints.push(this._dataView.getUint32(offset));
      } else if (aIntSize == 8) {
        let lo = this._dataView.getUint32(offset + 4);
        let hi = this._dataView.getUint32(offset);
        let uint64 = lazy.ctypes.UInt64.join(hi, lo);
        if (
          lazy.ctypes.UInt64.compare(uint64, this._JS_MAX_INT_UNSIGNED) == 1
        ) {
          if (aBigIntAllowed === true) {
            uints.push(PropertyListUtils.wrapInt64(uint64.toString()));
          } else {
            throw new Error("Integer too big to be read as float 64");
          }
        } else {
          uints.push(parseInt(uint64, 10));
        }
      } else {
        throw new Error("Unsupported size: " + aIntSize);
      }
    }

    return uints;
  },

  /**
   * Reads from the buffer the data object-count and the offset at which the
   * first object starts.
   *
   * @param aObjectOffset
   *        the object's offset.
   * @return [offset, count] - the offset in the buffer at which the first
   * object in data starts, and the number of objects.
   */
  _readDataOffsetAndCount: function BPLR__readDataOffsetAndCount(
    aObjectOffset
  ) {
    // The length of some objects in the data can be stored in two ways:
    // * If it is small enough, it is stored in the second four bits of the
    //   object descriptors.
    // * Otherwise, those bits are set to 1111, indicating that the next byte
    //   consists of the integer size of the data-length (also stored in the form
    //   of an object descriptor).  The length follows this byte.
    let [, maybeLength] = this._readObjectDescriptor(aObjectOffset);
    if (maybeLength != this.ADDITIONAL_INFO_BITS.LENGTH_INT_SIZE_FOLLOWS) {
      return [aObjectOffset + 1, maybeLength];
    }

    let [, intSizeInfo] = this._readObjectDescriptor(aObjectOffset + 1);

    // The int size is 2^intSizeInfo.
    let intSize = Math.pow(2, intSizeInfo);
    let dataLength = this._readUnsignedInts(aObjectOffset + 2, intSize, 1)[0];
    return [aObjectOffset + 2 + intSize, dataLength];
  },

  /**
   * Read array from the buffer and wrap it as a js array.
   *
   * @param aObjectOffset
   *        the offset in the buffer at which the array starts.
   * @param aNumberOfObjects
   *        the number of objects in the array.
   * @return a js array.
   */
  _wrapArray: function BPLR__wrapArray(aObjectOffset, aNumberOfObjects) {
    let refs = this._readUnsignedInts(
      aObjectOffset,
      this._objectRefSize,
      aNumberOfObjects
    );

    let array = new Array(aNumberOfObjects);
    let readObjectBound = this._readObject.bind(this);

    // Each index in the returned array is a lazy getter for its object.
    Array.prototype.forEach.call(
      refs,
      function (ref, objIndex) {
        Object.defineProperty(array, objIndex, {
          get() {
            delete array[objIndex];
            return (array[objIndex] = readObjectBound(ref));
          },
          configurable: true,
          enumerable: true,
        });
      },
      this
    );
    return array;
  },

  /**
   * Reads dictionary from the buffer and wraps it as a Map object.
   *
   * @param aObjectOffset
   *        the offset in the buffer at which the dictionary starts
   * @param aNumberOfObjects
   *        the number of keys in the dictionary
   * @return Map-style dictionary.
   */
  _wrapDictionary(aObjectOffset, aNumberOfObjects) {
    // A dictionary in the binary format is stored as a list of references to
    // key-objects, followed by a list of references to the value-objects for
    // those keys. The size of each list is aNumberOfObjects * this._objectRefSize.
    let dict = new Proxy(new Map(), LazyMapProxyHandler());
    if (aNumberOfObjects == 0) {
      return dict;
    }

    let keyObjsRefs = this._readUnsignedInts(
      aObjectOffset,
      this._objectRefSize,
      aNumberOfObjects
    );
    let valObjsRefs = this._readUnsignedInts(
      aObjectOffset + aNumberOfObjects * this._objectRefSize,
      this._objectRefSize,
      aNumberOfObjects
    );
    for (let i = 0; i < aNumberOfObjects; i++) {
      let key = this._readObject(keyObjsRefs[i]);
      let readBound = this._readObject.bind(this, valObjsRefs[i]);

      dict.setAsLazyGetter(key, readBound);
    }
    return dict;
  },

  /**
   * Reads an object at the spcified index in the object table
   *
   * @param aObjectIndex
   *        index at the object table
   * @return the property list object at the given index.
   */
  _readObject: function BPLR__readObject(aObjectIndex) {
    // If the object was previously read, return the cached object.
    if (this._objects[aObjectIndex] !== undefined) {
      return this._objects[aObjectIndex];
    }

    let objOffset = this._offsetTable[aObjectIndex];
    let [objType, additionalInfo] = this._readObjectDescriptor(objOffset);
    let value;
    switch (objType) {
      case this.OBJECT_TYPE_BITS.SIMPLE: {
        switch (additionalInfo) {
          case this.ADDITIONAL_INFO_BITS.NULL:
            value = null;
            break;
          case this.ADDITIONAL_INFO_BITS.FILL_BYTE:
            value = undefined;
            break;
          case this.ADDITIONAL_INFO_BITS.FALSE:
            value = false;
            break;
          case this.ADDITIONAL_INFO_BITS.TRUE:
            value = true;
            break;
          default:
            throw new Error("Unexpected value!");
        }
        break;
      }

      case this.OBJECT_TYPE_BITS.INTEGER: {
        // The integer is sized 2^additionalInfo.
        let intSize = Math.pow(2, additionalInfo);

        // For objects, 64-bit integers are always signed.  Negative integers
        // are always represented by a 64-bit integer.
        if (intSize == 8) {
          value = this._readSignedInt64(objOffset + 1);
        } else {
          value = this._readUnsignedInts(objOffset + 1, intSize, 1, true)[0];
        }
        break;
      }

      case this.OBJECT_TYPE_BITS.REAL: {
        // The real is sized 2^additionalInfo.
        value = this._readReal(objOffset + 1, Math.pow(2, additionalInfo));
        break;
      }

      case this.OBJECT_TYPE_BITS.DATE: {
        if (additionalInfo != this.ADDITIONAL_INFO_BITS.DATE) {
          throw new Error("Unexpected value");
        }

        value = this._readDate(objOffset + 1);
        break;
      }

      case this.OBJECT_TYPE_BITS.DATA: {
        let [offset, bytesCount] = this._readDataOffsetAndCount(objOffset);
        value = new Uint8Array(this._readUnsignedInts(offset, 1, bytesCount));
        break;
      }

      case this.OBJECT_TYPE_BITS.ASCII_STRING: {
        let [offset, charsCount] = this._readDataOffsetAndCount(objOffset);
        value = this._readString(offset, charsCount, false);
        break;
      }

      case this.OBJECT_TYPE_BITS.UNICODE_STRING: {
        let [offset, unicharsCount] = this._readDataOffsetAndCount(objOffset);
        value = this._readString(offset, unicharsCount, true);
        break;
      }

      case this.OBJECT_TYPE_BITS.UID: {
        // UIDs are only used in Keyed Archives, which are not yet supported.
        throw new Error("Keyed Archives are not supported");
      }

      case this.OBJECT_TYPE_BITS.ARRAY:
      case this.OBJECT_TYPE_BITS.SET: {
        // Note: For now, we fallback to handle sets the same way we handle
        // arrays.  See comments in the header of this file.

        // The bytes following the count are references to objects (indices).
        // Each reference is an unsigned int with size=this._objectRefSize.
        let [offset, objectsCount] = this._readDataOffsetAndCount(objOffset);
        value = this._wrapArray(offset, objectsCount);
        break;
      }

      case this.OBJECT_TYPE_BITS.DICTIONARY: {
        let [offset, objectsCount] = this._readDataOffsetAndCount(objOffset);
        value = this._wrapDictionary(offset, objectsCount);
        break;
      }

      default: {
        throw new Error("Unknown object type: " + objType);
      }
    }

    return (this._objects[aObjectIndex] = value);
  },
};

/**
 * Reader for XML property lists.
 *
 * @param aDOMDoc
 *        the DOM document to be read as a property list.
 */
function XMLPropertyListReader(aDOMDoc) {
  let docElt = aDOMDoc.documentElement;
  if (!docElt || docElt.localName != "plist" || !docElt.firstElementChild) {
    throw new Error("aDoc is not a property list document");
  }

  this._plistRootElement = docElt.firstElementChild;
}

XMLPropertyListReader.prototype = {
  get root() {
    return this._readObject(this._plistRootElement);
  },

  /**
   * Convert a dom element to a property list object.
   *
   * @param aDOMElt
   *        a dom element in a xml tree of a property list.
   * @return a js object representing the property list object.
   */
  _readObject: function XPLR__readObject(aDOMElt) {
    switch (aDOMElt.localName) {
      case "true":
        return true;
      case "false":
        return false;
      case "string":
      case "key":
        return aDOMElt.textContent;
      case "integer":
        return this._readInteger(aDOMElt);
      case "real": {
        let number = parseFloat(aDOMElt.textContent.trim());
        if (isNaN(number)) {
          throw new Error("Could not parse float value");
        }
        return number;
      }
      case "date":
        return new Date(aDOMElt.textContent);
      case "data": {
        // Strip spaces and new lines.
        let base64str = aDOMElt.textContent.replace(/\s*/g, "");
        let decoded = atob(base64str);
        return new Uint8Array(Array.from(decoded, c => c.charCodeAt(0)));
      }
      case "dict":
        return this._wrapDictionary(aDOMElt);
      case "array":
        return this._wrapArray(aDOMElt);
      default:
        throw new Error("Unexpected tagname");
    }
  },

  _readInteger: function XPLR__readInteger(aDOMElt) {
    // The integer may outbound js's max/min integer value.  We recognize this
    // case by comparing the parsed number to the original string value.
    // In case of an outbound, we fallback to return the number as a string.
    let numberAsString = aDOMElt.textContent.toString();
    let parsedNumber = parseInt(numberAsString, 10);
    if (isNaN(parsedNumber)) {
      throw new Error("Could not parse integer value");
    }

    if (parsedNumber.toString() == numberAsString) {
      return parsedNumber;
    }

    return PropertyListUtils.wrapInt64(numberAsString);
  },

  _wrapDictionary: function XPLR__wrapDictionary(aDOMElt) {
    // <dict>
    //   <key>my true bool</key>
    //   <true/>
    //   <key>my string key</key>
    //   <string>My String Key</string>
    // </dict>
    if (aDOMElt.children.length % 2 != 0) {
      throw new Error("Invalid dictionary");
    }
    let dict = new Proxy(new Map(), LazyMapProxyHandler());
    for (let i = 0; i < aDOMElt.children.length; i += 2) {
      let keyElem = aDOMElt.children[i];
      let valElem = aDOMElt.children[i + 1];

      if (keyElem.localName != "key") {
        throw new Error("Invalid dictionary");
      }

      let keyName = this._readObject(keyElem);
      let readBound = this._readObject.bind(this, valElem);

      dict.setAsLazyGetter(keyName, readBound);
    }
    return dict;
  },

  _wrapArray: function XPLR__wrapArray(aDOMElt) {
    // <array>
    //   <string>...</string>
    //   <integer></integer>
    //   <dict>
    //     ....
    //   </dict>
    // </array>

    // Each element in the array is a lazy getter for its property list object.
    let array = [];
    let readObjectBound = this._readObject.bind(this);
    Array.prototype.forEach.call(aDOMElt.children, function (elem, elemIndex) {
      Object.defineProperty(array, elemIndex, {
        get() {
          delete array[elemIndex];
          return (array[elemIndex] = readObjectBound(elem));
        },
        configurable: true,
        enumerable: true,
      });
    });
    return array;
  },
};

/**
 * Simple handler method to proxy calls to dict/Map objects to implement the
 * setAsLazyGetter API. With this, a value can be set as a function that will
 * evaluate its value and only be called when it's first retrieved.
 *
 * @member _lazyGetters
 *         Set() object to hold keys invoking LazyGetter.
 * @function get
 *         Trap for getting property values. Ensures that if a lazyGetter is present
 *         as value for key, then the function is evaluated, the value is cached,
 *         and its value will be returned.
 * @param  target
 *         Target object. (dict/Map)
 * @param  name
 *         Name of operation to be invoked on target.
 * @param  key
 *         Key to be set, retrieved or deleted. Keys are checked for laziness.
 * @return Returns value of "name" property of target by default. Otherwise returns
 *         updated target.
 */
function LazyMapProxyHandler() {
  return {
    _lazyGetters: new Set(),
    get(target, name) {
      switch (name) {
        case "setAsLazyGetter":
          return (key, value) => {
            this._lazyGetters.add(key);
            target.set(key, value);
          };
        case "get":
          return key => {
            if (this._lazyGetters.has(key)) {
              target.set(key, target.get(key)());
              this._lazyGetters.delete(key);
            }
            return target.get(key);
          };
        case "delete":
          return key => {
            if (this._lazyGetters.has(key)) {
              this._lazyGetters.delete(key);
            }
            return target.delete(key);
          };
        case "has":
          return key => target.has(key);
        default:
          return target[name];
      }
    },
  };
}
