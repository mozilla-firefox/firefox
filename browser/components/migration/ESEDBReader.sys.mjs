/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { ctypes } from "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs";

const lazy = {};
ChromeUtils.defineLazyGetter(lazy, "log", () => {
  let { ConsoleAPI } = ChromeUtils.importESModule(
    "resource://gre/modules/Console.sys.mjs"
  );
  let consoleOptions = {
    maxLogLevelPref: "browser.esedbreader.loglevel",
    prefix: "ESEDBReader",
  };
  return new ConsoleAPI(consoleOptions);
});

// We have a globally unique identifier for ESE instances. A new one
// is used for each different database opened.
let gESEInstanceCounter = 0;

// We limit the length of strings that we read from databases.
const MAX_STR_LENGTH = 64 * 1024;

// Kernel-related types:
export const KERNEL = {};

KERNEL.FILETIME = new ctypes.StructType("FILETIME", [
  { dwLowDateTime: ctypes.uint32_t },
  { dwHighDateTime: ctypes.uint32_t },
]);
KERNEL.SYSTEMTIME = new ctypes.StructType("SYSTEMTIME", [
  { wYear: ctypes.uint16_t },
  { wMonth: ctypes.uint16_t },
  { wDayOfWeek: ctypes.uint16_t },
  { wDay: ctypes.uint16_t },
  { wHour: ctypes.uint16_t },
  { wMinute: ctypes.uint16_t },
  { wSecond: ctypes.uint16_t },
  { wMilliseconds: ctypes.uint16_t },
]);

// DB column types, cribbed from the ESE header
export var COLUMN_TYPES = {
  JET_coltypBit: 1 /* True, False, or NULL */,
  JET_coltypUnsignedByte: 2 /* 1-byte integer, unsigned */,
  JET_coltypShort: 3 /* 2-byte integer, signed */,
  JET_coltypLong: 4 /* 4-byte integer, signed */,
  JET_coltypCurrency: 5 /* 8 byte integer, signed */,
  JET_coltypIEEESingle: 6 /* 4-byte IEEE single precision */,
  JET_coltypIEEEDouble: 7 /* 8-byte IEEE double precision */,
  JET_coltypDateTime: 8 /* Integral date, fractional time */,
  JET_coltypBinary: 9 /* Binary data, < 255 bytes */,
  JET_coltypText: 10 /* ANSI text, case insensitive, < 255 bytes */,
  JET_coltypLongBinary: 11 /* Binary data, long value */,
  JET_coltypLongText: 12 /* ANSI text, long value */,

  JET_coltypUnsignedLong: 14 /* 4-byte unsigned integer */,
  JET_coltypLongLong: 15 /* 8-byte signed integer */,
  JET_coltypGUID: 16 /* 16-byte globally unique identifier */,
};

// Not very efficient, but only used for error messages
function getColTypeName(numericValue) {
  return (
    Object.keys(COLUMN_TYPES).find(t => COLUMN_TYPES[t] == numericValue) ||
    "unknown"
  );
}

// All type constants and method wrappers go on this object:
export const ESE = {};

ESE.JET_ERR = ctypes.long;
ESE.JET_PCWSTR = ctypes.char16_t.ptr;
// The ESE header calls this JET_API_PTR, but because it isn't ever used as a
// pointer, I opted for a different name.
// Note that this is defined differently on 32 vs. 64-bit in the header.
ESE.JET_API_ITEM =
  ctypes.voidptr_t.size == 4 ? ctypes.unsigned_long : ctypes.uint64_t;
ESE.JET_INSTANCE = ESE.JET_API_ITEM;
ESE.JET_SESID = ESE.JET_API_ITEM;
ESE.JET_TABLEID = ESE.JET_API_ITEM;
ESE.JET_COLUMNID = ctypes.unsigned_long;
ESE.JET_GRBIT = ctypes.unsigned_long;
ESE.JET_COLTYP = ctypes.unsigned_long;
ESE.JET_DBID = ctypes.unsigned_long;

ESE.JET_COLUMNDEF = new ctypes.StructType("JET_COLUMNDEF", [
  { cbStruct: ctypes.unsigned_long },
  { columnid: ESE.JET_COLUMNID },
  { coltyp: ESE.JET_COLTYP },
  { wCountry: ctypes.unsigned_short }, // sepcifies the country/region for the column definition
  { langid: ctypes.unsigned_short },
  { cp: ctypes.unsigned_short },
  { wCollate: ctypes.unsigned_short } /* Must be 0 */,
  { cbMax: ctypes.unsigned_long },
  { grbit: ESE.JET_GRBIT },
]);

// Track open databases
let gOpenDBs = new Map();

// Track open libraries
export let gLibs = {};

function convertESEError(errorCode) {
  switch (errorCode) {
    case -1213 /* JET_errPageSizeMismatch */:
    case -1002 /* JET_errInvalidName*/:
    case -1507 /* JET_errColumnNotFound */:
      // The DB format has changed and we haven't updated this migration code:
      return "The database format has changed, error code: " + errorCode;
    case -1032 /* JET_errFileAccessDenied */:
    case -1207 /* JET_errDatabaseLocked */:
    case -1302 /* JET_errTableLocked */:
      return "The database or table is locked, error code: " + errorCode;
    case -1305 /* JET_errObjectNotFound */:
      return "The table/object was not found.";
    case -1809 /* JET_errPermissionDenied*/:
    case -1907 /* JET_errAccessDenied */:
      return "Access or permission denied, error code: " + errorCode;
    case -1044 /* JET_errInvalidFilename */:
      return "Invalid file name";
    case -1811 /* JET_errFileNotFound */:
      return "File not found";
    case -550 /* JET_errDatabaseDirtyShutdown */:
      return "Database in dirty shutdown state (without the requisite logs?)";
    case -514 /* JET_errBadLogVersion */:
      return "Database log version does not match the version of ESE in use.";
    default:
      return "Unknown error: " + errorCode;
  }
}

function handleESEError(
  method,
  methodName,
  shouldThrow = true,
  errorLog = true
) {
  return function () {
    let rv;
    try {
      rv = method.apply(null, arguments);
    } catch (ex) {
      lazy.log.error("Error calling into ctypes method", methodName, ex);
      throw ex;
    }
    let resultCode = parseInt(rv.toString(10), 10);
    if (resultCode < 0) {
      if (errorLog) {
        lazy.log.error("Got error " + resultCode + " calling " + methodName);
      }
      if (shouldThrow) {
        throw new Error(convertESEError(rv));
      }
    } else if (resultCode > 0 && errorLog) {
      lazy.log.warn("Got warning " + resultCode + " calling " + methodName);
    }
    return resultCode;
  };
}

export function declareESEFunction(methodName, ...args) {
  let declaration = ["Jet" + methodName, ctypes.winapi_abi, ESE.JET_ERR].concat(
    args
  );
  let ctypeMethod = gLibs.ese.declare.apply(gLibs.ese, declaration);
  ESE[methodName] = handleESEError(ctypeMethod, methodName);
  ESE["FailSafe" + methodName] = handleESEError(ctypeMethod, methodName, false);
  ESE["Manual" + methodName] = handleESEError(
    ctypeMethod,
    methodName,
    false,
    false
  );
}

function declareESEFunctions() {
  declareESEFunction(
    "GetDatabaseFileInfoW",
    ESE.JET_PCWSTR,
    ctypes.voidptr_t,
    ctypes.unsigned_long,
    ctypes.unsigned_long
  );

  declareESEFunction(
    "GetSystemParameterW",
    ESE.JET_INSTANCE,
    ESE.JET_SESID,
    ctypes.unsigned_long,
    ESE.JET_API_ITEM.ptr,
    ESE.JET_PCWSTR,
    ctypes.unsigned_long
  );
  declareESEFunction(
    "SetSystemParameterW",
    ESE.JET_INSTANCE.ptr,
    ESE.JET_SESID,
    ctypes.unsigned_long,
    ESE.JET_API_ITEM,
    ESE.JET_PCWSTR
  );
  declareESEFunction("CreateInstanceW", ESE.JET_INSTANCE.ptr, ESE.JET_PCWSTR);
  declareESEFunction("Init", ESE.JET_INSTANCE.ptr);

  declareESEFunction(
    "BeginSessionW",
    ESE.JET_INSTANCE,
    ESE.JET_SESID.ptr,
    ESE.JET_PCWSTR,
    ESE.JET_PCWSTR
  );
  declareESEFunction(
    "AttachDatabaseW",
    ESE.JET_SESID,
    ESE.JET_PCWSTR,
    ESE.JET_GRBIT
  );
  declareESEFunction("DetachDatabaseW", ESE.JET_SESID, ESE.JET_PCWSTR);
  declareESEFunction(
    "OpenDatabaseW",
    ESE.JET_SESID,
    ESE.JET_PCWSTR,
    ESE.JET_PCWSTR,
    ESE.JET_DBID.ptr,
    ESE.JET_GRBIT
  );
  declareESEFunction(
    "OpenTableW",
    ESE.JET_SESID,
    ESE.JET_DBID,
    ESE.JET_PCWSTR,
    ctypes.voidptr_t,
    ctypes.unsigned_long,
    ESE.JET_GRBIT,
    ESE.JET_TABLEID.ptr
  );

  declareESEFunction(
    "GetColumnInfoW",
    ESE.JET_SESID,
    ESE.JET_DBID,
    ESE.JET_PCWSTR,
    ESE.JET_PCWSTR,
    ctypes.voidptr_t,
    ctypes.unsigned_long,
    ctypes.unsigned_long
  );

  declareESEFunction(
    "Move",
    ESE.JET_SESID,
    ESE.JET_TABLEID,
    ctypes.long,
    ESE.JET_GRBIT
  );

  declareESEFunction(
    "RetrieveColumn",
    ESE.JET_SESID,
    ESE.JET_TABLEID,
    ESE.JET_COLUMNID,
    ctypes.voidptr_t,
    ctypes.unsigned_long,
    ctypes.unsigned_long.ptr,
    ESE.JET_GRBIT,
    ctypes.voidptr_t
  );

  declareESEFunction("CloseTable", ESE.JET_SESID, ESE.JET_TABLEID);
  declareESEFunction(
    "CloseDatabase",
    ESE.JET_SESID,
    ESE.JET_DBID,
    ESE.JET_GRBIT
  );

  declareESEFunction("EndSession", ESE.JET_SESID, ESE.JET_GRBIT);

  declareESEFunction("Term", ESE.JET_INSTANCE);
}

function unloadLibraries() {
  lazy.log.debug("Unloading");
  if (gOpenDBs.size) {
    lazy.log.error("Shouldn't unload libraries before DBs are closed!");
    for (let db of gOpenDBs.values()) {
      db._close();
    }
  }
  for (let k of Object.keys(ESE)) {
    delete ESE[k];
  }
  gLibs.ese.close();
  gLibs.kernel.close();
  delete gLibs.ese;
  delete gLibs.kernel;
}

export function loadLibraries() {
  Services.obs.addObserver(unloadLibraries, "xpcom-shutdown");
  gLibs.ese = ctypes.open("esent.dll");
  gLibs.kernel = ctypes.open("kernel32.dll");
  KERNEL.FileTimeToSystemTime = gLibs.kernel.declare(
    "FileTimeToSystemTime",
    ctypes.winapi_abi,
    ctypes.int,
    KERNEL.FILETIME.ptr,
    KERNEL.SYSTEMTIME.ptr
  );

  declareESEFunctions();
}

function ESEDB(rootPath, dbPath, logPath) {
  lazy.log.info("Created db");
  this.rootPath = rootPath;
  this.dbPath = dbPath;
  this.logPath = logPath;
  this._references = 0;
  this._init();
}

ESEDB.prototype = {
  rootPath: null,
  dbPath: null,
  logPath: null,
  _opened: false,
  _attached: false,
  _sessionCreated: false,
  _instanceCreated: false,
  _dbId: null,
  _sessionId: null,
  _instanceId: null,

  _init() {
    if (!gLibs.ese) {
      loadLibraries();
    }
    this.incrementReferenceCounter();
    this._internalOpen();
  },

  _internalOpen() {
    try {
      let dbinfo = new ctypes.unsigned_long();
      ESE.GetDatabaseFileInfoW(
        this.dbPath,
        dbinfo.address(),
        ctypes.unsigned_long.size,
        17
      );

      let pageSize = ctypes.UInt64.lo(dbinfo.value);
      ESE.SetSystemParameterW(
        null,
        0,
        64 /* JET_paramDatabasePageSize*/,
        pageSize,
        null
      );

      this._instanceId = new ESE.JET_INSTANCE();
      ESE.CreateInstanceW(
        this._instanceId.address(),
        "firefox-dbreader-" + gESEInstanceCounter++
      );
      this._instanceCreated = true;

      ESE.SetSystemParameterW(
        this._instanceId.address(),
        0,
        0 /* JET_paramSystemPath*/,
        0,
        this.rootPath
      );
      ESE.SetSystemParameterW(
        this._instanceId.address(),
        0,
        1 /* JET_paramTempPath */,
        0,
        this.rootPath
      );
      ESE.SetSystemParameterW(
        this._instanceId.address(),
        0,
        2 /* JET_paramLogFilePath*/,
        0,
        this.logPath
      );

      // Shouldn't try to call JetTerm if the following call fails.
      this._instanceCreated = false;
      ESE.Init(this._instanceId.address());
      this._instanceCreated = true;
      this._sessionId = new ESE.JET_SESID();
      ESE.BeginSessionW(
        this._instanceId,
        this._sessionId.address(),
        null,
        null
      );
      this._sessionCreated = true;

      const JET_bitDbReadOnly = 1;
      ESE.AttachDatabaseW(this._sessionId, this.dbPath, JET_bitDbReadOnly);
      this._attached = true;
      this._dbId = new ESE.JET_DBID();
      ESE.OpenDatabaseW(
        this._sessionId,
        this.dbPath,
        null,
        this._dbId.address(),
        JET_bitDbReadOnly
      );
      this._opened = true;
    } catch (ex) {
      try {
        this._close();
      } catch (innerException) {
        console.error(innerException);
      }
      // Make sure caller knows we failed.
      throw ex;
    }
    gOpenDBs.set(this.dbPath, this);
  },

  checkForColumn(tableName, columnName) {
    if (!this._opened) {
      throw new Error("The database was closed!");
    }

    let columnInfo;
    try {
      columnInfo = this._getColumnInfo(tableName, [{ name: columnName }]);
    } catch (ex) {
      return null;
    }
    return columnInfo[0];
  },

  tableExists(tableName) {
    if (!this._opened) {
      throw new Error("The database was closed!");
    }

    let tableId = new ESE.JET_TABLEID();
    let rv = ESE.ManualOpenTableW(
      this._sessionId,
      this._dbId,
      tableName,
      null,
      0,
      4 /* JET_bitTableReadOnly */,
      tableId.address()
    );
    if (rv == -1305 /* JET_errObjectNotFound */) {
      return false;
    }
    if (rv < 0) {
      lazy.log.error("Got error " + rv + " calling OpenTableW");
      throw new Error(convertESEError(rv));
    }

    if (rv > 0) {
      lazy.log.error("Got warning " + rv + " calling OpenTableW");
    }
    ESE.FailSafeCloseTable(this._sessionId, tableId);
    return true;
  },

  *tableItems(tableName, columns) {
    if (!this._opened) {
      throw new Error("The database was closed!");
    }

    let tableOpened = false;
    let tableId;
    try {
      tableId = this._openTable(tableName);
      tableOpened = true;

      let columnInfo = this._getColumnInfo(tableName, columns);

      let rv = ESE.ManualMove(
        this._sessionId,
        tableId,
        -2147483648 /* JET_MoveFirst */,
        0
      );
      if (rv == -1603 /* JET_errNoCurrentRecord */) {
        // There are no rows in the table.
        this._closeTable(tableId);
        return;
      }
      if (rv != 0) {
        throw new Error(convertESEError(rv));
      }

      do {
        let rowContents = {};
        for (let column of columnInfo) {
          let [buffer, bufferSize] = this._getBufferForColumn(column);
          // We handle errors manually so we accurately deal with NULL values.
          let err = ESE.ManualRetrieveColumn(
            this._sessionId,
            tableId,
            column.id,
            buffer.address(),
            bufferSize,
            null,
            0,
            null
          );
          rowContents[column.name] = this._convertResult(column, buffer, err);
        }
        yield rowContents;
      } while (
        ESE.ManualMove(this._sessionId, tableId, 1 /* JET_MoveNext */, 0) === 0
      );
    } catch (ex) {
      if (tableOpened) {
        this._closeTable(tableId);
      }
      throw ex;
    }
    this._closeTable(tableId);
  },

  _openTable(tableName) {
    let tableId = new ESE.JET_TABLEID();
    ESE.OpenTableW(
      this._sessionId,
      this._dbId,
      tableName,
      null,
      0,
      4 /* JET_bitTableReadOnly */,
      tableId.address()
    );
    return tableId;
  },

  _getBufferForColumn(column) {
    let buffer;
    if (column.type == "string") {
      let wchar_tArray = ctypes.ArrayType(ctypes.char16_t);
      // size on the column is in bytes, 2 bytes to a wchar, so:
      let charCount = column.dbSize >> 1;
      buffer = new wchar_tArray(charCount);
    } else if (column.type == "boolean") {
      buffer = new ctypes.uint8_t();
    } else if (column.type == "date") {
      buffer = new KERNEL.FILETIME();
    } else if (column.type == "guid") {
      let byteArray = ctypes.ArrayType(ctypes.uint8_t);
      buffer = new byteArray(column.dbSize);
    } else {
      throw new Error("Unknown type " + column.type);
    }
    return [buffer, buffer.constructor.size];
  },

  _convertResult(column, buffer, err) {
    if (err != 0) {
      if (err == 1004) {
        // Deal with null values:
        buffer = null;
      } else {
        console.error(
          "Unexpected JET error: ",
          err,
          "; retrieving value for column ",
          column.name
        );
        throw new Error(convertESEError(err));
      }
    }
    if (column.type == "string") {
      return buffer ? buffer.readString() : "";
    }
    if (column.type == "boolean") {
      return buffer ? buffer.value == 255 : false;
    }
    if (column.type == "guid") {
      if (buffer.length != 16) {
        console.error(
          "Buffer size for guid field ",
          column.id,
          " should have been 16!"
        );
        return "";
      }
      let rv = "{";
      for (let i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
          rv += "-";
        }
        let byteValue = buffer.addressOfElement(i).contents;
        // Ensure there's a leading 0
        rv += ("0" + byteValue.toString(16)).substr(-2);
      }
      return rv + "}";
    }
    if (column.type == "date") {
      if (!buffer) {
        return null;
      }
      let systemTime = new KERNEL.SYSTEMTIME();
      let result = KERNEL.FileTimeToSystemTime(
        buffer.address(),
        systemTime.address()
      );
      if (result == 0) {
        throw new Error(ctypes.winLastError);
      }

      // System time is in UTC, so we use Date.UTC to get milliseconds from epoch,
      // then divide by 1000 to get seconds, and round down:
      return new Date(
        Date.UTC(
          systemTime.wYear,
          systemTime.wMonth - 1,
          systemTime.wDay,
          systemTime.wHour,
          systemTime.wMinute,
          systemTime.wSecond,
          systemTime.wMilliseconds
        )
      );
    }
    return undefined;
  },

  _getColumnInfo(tableName, columns) {
    let rv = [];
    for (let column of columns) {
      let columnInfoFromDB = new ESE.JET_COLUMNDEF();
      ESE.GetColumnInfoW(
        this._sessionId,
        this._dbId,
        tableName,
        column.name,
        columnInfoFromDB.address(),
        ESE.JET_COLUMNDEF.size,
        0 /* JET_ColInfo */
      );
      let dbType = parseInt(columnInfoFromDB.coltyp.toString(10), 10);
      let dbSize = parseInt(columnInfoFromDB.cbMax.toString(10), 10);
      if (column.type == "string") {
        if (
          dbType != COLUMN_TYPES.JET_coltypLongText &&
          dbType != COLUMN_TYPES.JET_coltypText
        ) {
          throw new Error(
            "Invalid column type for column " +
              column.name +
              "; expected text type, got type " +
              getColTypeName(dbType)
          );
        }
        if (dbSize > MAX_STR_LENGTH) {
          throw new Error(
            "Column " +
              column.name +
              " has more than 64k data in it. This API is not designed to handle data that large."
          );
        }
      } else if (column.type == "boolean") {
        if (dbType != COLUMN_TYPES.JET_coltypBit) {
          throw new Error(
            "Invalid column type for column " +
              column.name +
              "; expected bit type, got type " +
              getColTypeName(dbType)
          );
        }
      } else if (column.type == "date") {
        if (dbType != COLUMN_TYPES.JET_coltypLongLong) {
          throw new Error(
            "Invalid column type for column " +
              column.name +
              "; expected long long type, got type " +
              getColTypeName(dbType)
          );
        }
      } else if (column.type == "guid") {
        if (dbType != COLUMN_TYPES.JET_coltypGUID) {
          throw new Error(
            "Invalid column type for column " +
              column.name +
              "; expected guid type, got type " +
              getColTypeName(dbType)
          );
        }
      } else if (column.type) {
        throw new Error(
          "Unknown column type " +
            column.type +
            " requested for column " +
            column.name +
            ", don't know what to do."
        );
      }

      rv.push({
        name: column.name,
        id: columnInfoFromDB.columnid,
        type: column.type,
        dbSize,
        dbType,
      });
    }
    return rv;
  },

  _closeTable(tableId) {
    ESE.FailSafeCloseTable(this._sessionId, tableId);
  },

  _close() {
    this._internalClose();
    gOpenDBs.delete(this.dbPath);
  },

  _internalClose() {
    if (this._opened) {
      lazy.log.debug("close db");
      ESE.FailSafeCloseDatabase(this._sessionId, this._dbId, 0);
      lazy.log.debug("finished close db");
      this._opened = false;
    }
    if (this._attached) {
      lazy.log.debug("detach db");
      ESE.FailSafeDetachDatabaseW(this._sessionId, this.dbPath);
      this._attached = false;
    }
    if (this._sessionCreated) {
      lazy.log.debug("end session");
      ESE.FailSafeEndSession(this._sessionId, 0);
      this._sessionCreated = false;
    }
    if (this._instanceCreated) {
      lazy.log.debug("term");
      ESE.FailSafeTerm(this._instanceId);
      this._instanceCreated = false;
    }
  },

  incrementReferenceCounter() {
    this._references++;
  },

  decrementReferenceCounter() {
    this._references--;
    if (this._references <= 0) {
      this._close();
    }
  },
};

export let ESEDBReader = {
  openDB(rootDir, dbFile, logDir) {
    let dbFilePath = dbFile.path;
    if (gOpenDBs.has(dbFilePath)) {
      let db = gOpenDBs.get(dbFilePath);
      db.incrementReferenceCounter();
      return db;
    }
    // ESE is really picky about the trailing slashes according to the docs,
    // so we do as we're told and ensure those are there:
    return new ESEDB(rootDir.path + "\\", dbFilePath, logDir.path + "\\");
  },

  async dbLocked(dbFile) {
    const utils = Cc[
      "@mozilla.org/profile/migrator/edgemigrationutils;1"
    ].createInstance(Ci.nsIEdgeMigrationUtils);

    const locked = await utils.isDbLocked(dbFile);

    if (locked) {
      console.error(`ESE DB at ${dbFile.path} is locked.`);
    }

    return locked;
  },

  closeDB(db) {
    db.decrementReferenceCounter();
  },

  COLUMN_TYPES,
};
