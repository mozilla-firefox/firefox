"use strict";

const { ctypes } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs"
);
const { ESE, KERNEL, gLibs, COLUMN_TYPES, declareESEFunction, loadLibraries } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/migration/ESEDBReader.sys.mjs"
  );
const { EdgeProfileMigrator } = ChromeUtils.importESModule(
  "moz-src:///browser/components/migration/EdgeProfileMigrator.sys.mjs"
);

let gESEInstanceCounter = 1;

ESE.JET_COLUMNCREATE_W = new ctypes.StructType("JET_COLUMNCREATE_W", [
  { cbStruct: ctypes.unsigned_long },
  { szColumnName: ESE.JET_PCWSTR },
  { coltyp: ESE.JET_COLTYP },
  { cbMax: ctypes.unsigned_long },
  { grbit: ESE.JET_GRBIT },
  { pvDefault: ctypes.voidptr_t },
  { cbDefault: ctypes.unsigned_long },
  { cp: ctypes.unsigned_long },
  { columnid: ESE.JET_COLUMNID },
  { err: ESE.JET_ERR },
]);

function createColumnCreationWrapper({ name, type, cbMax }) {
  // We use a wrapper object because we need to be sure the JS engine won't GC
  // data that we're "only" pointing to.
  let wrapper = {};
  wrapper.column = new ESE.JET_COLUMNCREATE_W();
  wrapper.column.cbStruct = ESE.JET_COLUMNCREATE_W.size;
  let wchar_tArray = ctypes.ArrayType(ctypes.char16_t);
  wrapper.name = new wchar_tArray(name.length + 1);
  wrapper.name.value = String(name);
  wrapper.column.szColumnName = wrapper.name;
  wrapper.column.coltyp = type;
  let fallback = 0;
  switch (type) {
    case COLUMN_TYPES.JET_coltypText:
      fallback = 255;
    // Intentional fall-through
    case COLUMN_TYPES.JET_coltypLongText:
      wrapper.column.cbMax = cbMax || fallback || 64 * 1024;
      break;
    case COLUMN_TYPES.JET_coltypGUID:
      wrapper.column.cbMax = 16;
      break;
    case COLUMN_TYPES.JET_coltypBit:
      wrapper.column.cbMax = 1;
      break;
    case COLUMN_TYPES.JET_coltypLongLong:
      wrapper.column.cbMax = 8;
      break;
    default:
      throw new Error("Unknown column type!");
  }

  wrapper.column.columnid = new ESE.JET_COLUMNID();
  wrapper.column.grbit = 0;
  wrapper.column.pvDefault = null;
  wrapper.column.cbDefault = 0;
  wrapper.column.cp = 0;

  return wrapper;
}

// "forward declarations" of indexcreate and setinfo structs, which we don't use.
ESE.JET_INDEXCREATE = new ctypes.StructType("JET_INDEXCREATE");
ESE.JET_SETINFO = new ctypes.StructType("JET_SETINFO");

ESE.JET_TABLECREATE_W = new ctypes.StructType("JET_TABLECREATE_W", [
  { cbStruct: ctypes.unsigned_long },
  { szTableName: ESE.JET_PCWSTR },
  { szTemplateTableName: ESE.JET_PCWSTR },
  { ulPages: ctypes.unsigned_long },
  { ulDensity: ctypes.unsigned_long },
  { rgcolumncreate: ESE.JET_COLUMNCREATE_W.ptr },
  { cColumns: ctypes.unsigned_long },
  { rgindexcreate: ESE.JET_INDEXCREATE.ptr },
  { cIndexes: ctypes.unsigned_long },
  { grbit: ESE.JET_GRBIT },
  { tableid: ESE.JET_TABLEID },
  { cCreated: ctypes.unsigned_long },
]);

function createTableCreationWrapper(tableName, columns) {
  let wrapper = {};
  let wchar_tArray = ctypes.ArrayType(ctypes.char16_t);
  wrapper.name = new wchar_tArray(tableName.length + 1);
  wrapper.name.value = String(tableName);
  wrapper.table = new ESE.JET_TABLECREATE_W();
  wrapper.table.cbStruct = ESE.JET_TABLECREATE_W.size;
  wrapper.table.szTableName = wrapper.name;
  wrapper.table.szTemplateTableName = null;
  wrapper.table.ulPages = 1;
  wrapper.table.ulDensity = 0;
  let columnArrayType = ESE.JET_COLUMNCREATE_W.array(columns.length);
  wrapper.columnAry = new columnArrayType();
  wrapper.table.rgcolumncreate = wrapper.columnAry.addressOfElement(0);
  wrapper.table.cColumns = columns.length;
  wrapper.columns = [];
  for (let i = 0; i < columns.length; i++) {
    let column = columns[i];
    let columnWrapper = createColumnCreationWrapper(column);
    wrapper.columnAry.addressOfElement(i).contents = columnWrapper.column;
    wrapper.columns.push(columnWrapper);
  }
  wrapper.table.rgindexcreate = null;
  wrapper.table.cIndexes = 0;
  return wrapper;
}

function convertValueForWriting(value, valueType) {
  let buffer;
  let valueOfValueType = ctypes.UInt64.lo(valueType);
  switch (valueOfValueType) {
    case COLUMN_TYPES.JET_coltypLongLong:
      if (value instanceof Date) {
        buffer = new KERNEL.FILETIME();
        let sysTime = new KERNEL.SYSTEMTIME();
        sysTime.wYear = value.getUTCFullYear();
        sysTime.wMonth = value.getUTCMonth() + 1;
        sysTime.wDay = value.getUTCDate();
        sysTime.wHour = value.getUTCHours();
        sysTime.wMinute = value.getUTCMinutes();
        sysTime.wSecond = value.getUTCSeconds();
        sysTime.wMilliseconds = value.getUTCMilliseconds();
        let rv = KERNEL.SystemTimeToFileTime(
          sysTime.address(),
          buffer.address()
        );
        if (!rv) {
          throw new Error("Failed to get FileTime.");
        }
        return [buffer, KERNEL.FILETIME.size];
      }
      throw new Error("Unrecognized value for longlong column");
    case COLUMN_TYPES.JET_coltypLongText: {
      let wchar_tArray = ctypes.ArrayType(ctypes.char16_t);
      buffer = new wchar_tArray(value.length + 1);
      buffer.value = String(value);
      return [buffer, buffer.length * 2];
    }
    case COLUMN_TYPES.JET_coltypBit:
      buffer = new ctypes.uint8_t();
      // Bizarre boolean values, but whatever:
      buffer.value = value ? 255 : 0;
      return [buffer, 1];
    case COLUMN_TYPES.JET_coltypGUID: {
      let byteArray = ctypes.ArrayType(ctypes.uint8_t);
      buffer = new byteArray(16);
      let j = 0;
      for (let i = 0; i < value.length; i++) {
        if (!/[0-9a-f]/i.test(value[i])) {
          continue;
        }
        let byteAsHex = value.substr(i, 2);
        buffer[j++] = parseInt(byteAsHex, 16);
        i++;
      }
      return [buffer, 16];
    }
  }

  throw new Error("Unknown type " + valueType);
}

let initializedESE = false;

let eseDBWritingHelpers = {
  setupDB(dbFile, tables) {
    if (!initializedESE) {
      initializedESE = true;
      loadLibraries();

      KERNEL.SystemTimeToFileTime = gLibs.kernel.declare(
        "SystemTimeToFileTime",
        ctypes.winapi_abi,
        ctypes.bool,
        KERNEL.SYSTEMTIME.ptr,
        KERNEL.FILETIME.ptr
      );

      declareESEFunction(
        "CreateDatabaseW",
        ESE.JET_SESID,
        ESE.JET_PCWSTR,
        ESE.JET_PCWSTR,
        ESE.JET_DBID.ptr,
        ESE.JET_GRBIT
      );
      declareESEFunction(
        "CreateTableColumnIndexW",
        ESE.JET_SESID,
        ESE.JET_DBID,
        ESE.JET_TABLECREATE_W.ptr
      );
      declareESEFunction("BeginTransaction", ESE.JET_SESID);
      declareESEFunction("CommitTransaction", ESE.JET_SESID, ESE.JET_GRBIT);
      declareESEFunction(
        "PrepareUpdate",
        ESE.JET_SESID,
        ESE.JET_TABLEID,
        ctypes.unsigned_long
      );
      declareESEFunction(
        "Update",
        ESE.JET_SESID,
        ESE.JET_TABLEID,
        ctypes.voidptr_t,
        ctypes.unsigned_long,
        ctypes.unsigned_long.ptr
      );
      declareESEFunction(
        "SetColumn",
        ESE.JET_SESID,
        ESE.JET_TABLEID,
        ESE.JET_COLUMNID,
        ctypes.voidptr_t,
        ctypes.unsigned_long,
        ESE.JET_GRBIT,
        ESE.JET_SETINFO.ptr
      );
      ESE.SetSystemParameterW(
        null,
        0,
        64 /* JET_paramDatabasePageSize*/,
        8192,
        null
      );
    }

    let rootPath = dbFile.parent.path + "\\";
    let logPath = rootPath + "LogFiles\\";

    try {
      this._instanceId = new ESE.JET_INSTANCE();
      ESE.CreateInstanceW(
        this._instanceId.address(),
        "firefox-dbwriter-" + gESEInstanceCounter++
      );
      this._instanceCreated = true;

      ESE.SetSystemParameterW(
        this._instanceId.address(),
        0,
        0 /* JET_paramSystemPath*/,
        0,
        rootPath
      );
      ESE.SetSystemParameterW(
        this._instanceId.address(),
        0,
        1 /* JET_paramTempPath */,
        0,
        rootPath
      );
      ESE.SetSystemParameterW(
        this._instanceId.address(),
        0,
        2 /* JET_paramLogFilePath*/,
        0,
        logPath
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

      this._dbId = new ESE.JET_DBID();
      this._dbPath = rootPath + "spartan.edb";
      ESE.CreateDatabaseW(
        this._sessionId,
        this._dbPath,
        null,
        this._dbId.address(),
        0
      );
      this._opened = this._attached = true;

      for (let [tableName, data] of tables) {
        let { rows, columns } = data;
        let tableCreationWrapper = createTableCreationWrapper(
          tableName,
          columns
        );
        ESE.CreateTableColumnIndexW(
          this._sessionId,
          this._dbId,
          tableCreationWrapper.table.address()
        );
        this._tableId = tableCreationWrapper.table.tableid;

        let columnIdMap = new Map();
        if (rows.length) {
          // Iterate over the struct we passed into ESENT because they have the
          // created column ids.
          let columnCount = ctypes.UInt64.lo(
            tableCreationWrapper.table.cColumns
          );
          let columnsPassed = tableCreationWrapper.table.rgcolumncreate;
          for (let i = 0; i < columnCount; i++) {
            let column = columnsPassed.contents;
            columnIdMap.set(column.szColumnName.readString(), column);
            columnsPassed = columnsPassed.increment();
          }
          ESE.ManualMove(
            this._sessionId,
            this._tableId,
            -2147483648 /* JET_MoveFirst */,
            0
          );
          ESE.BeginTransaction(this._sessionId);
          for (let row of rows) {
            ESE.PrepareUpdate(
              this._sessionId,
              this._tableId,
              0 /* JET_prepInsert */
            );
            for (let columnName in row) {
              let col = columnIdMap.get(columnName);
              let colId = col.columnid;
              let [val, valSize] = convertValueForWriting(
                row[columnName],
                col.coltyp
              );
              /* JET_bitSetOverwriteLV */
              ESE.SetColumn(
                this._sessionId,
                this._tableId,
                colId,
                val.address(),
                valSize,
                4,
                null
              );
            }
            let actualBookmarkSize = new ctypes.unsigned_long();
            ESE.Update(
              this._sessionId,
              this._tableId,
              null,
              0,
              actualBookmarkSize.address()
            );
          }
          ESE.CommitTransaction(
            this._sessionId,
            0 /* JET_bitWaitLastLevel0Commit */
          );
        }
      }
    } finally {
      try {
        this._close();
      } catch (ex) {
        console.error(ex);
      }
    }
  },

  _close() {
    if (this._tableId) {
      ESE.FailSafeCloseTable(this._sessionId, this._tableId);
      delete this._tableId;
    }
    if (this._opened) {
      ESE.FailSafeCloseDatabase(this._sessionId, this._dbId, 0);
      this._opened = false;
    }
    if (this._attached) {
      ESE.FailSafeDetachDatabaseW(this._sessionId, this._dbPath);
      this._attached = false;
    }
    if (this._sessionCreated) {
      ESE.FailSafeEndSession(this._sessionId, 0);
      this._sessionCreated = false;
    }
    if (this._instanceCreated) {
      ESE.FailSafeTerm(this._instanceId);
      this._instanceCreated = false;
    }
  },
};

add_task(async function () {
  let tempFile = Services.dirsvc.get("TmpD", Ci.nsIFile);
  tempFile.append("fx-xpcshell-edge-db");
  tempFile.createUnique(tempFile.DIRECTORY_TYPE, 0o600);

  let db = tempFile.clone();
  db.append("spartan.edb");

  let logs = tempFile.clone();
  logs.append("LogFiles");
  logs.create(tempFile.DIRECTORY_TYPE, 0o600);

  let creationDate = new Date(Date.now() - 5000);
  const kEdgeMenuParent = "62d07e2b-5f0d-4e41-8426-5f5ec9717beb";
  let bookmarkReferenceItems = [
    {
      URL: "http://www.mozilla.org/",
      Title: "Mozilla",
      DateUpdated: new Date(creationDate.valueOf() + 100),
      ItemId: "1c00c10a-15f6-4618-92dd-22575102a4da",
      ParentId: kEdgeMenuParent,
      IsFolder: false,
      IsDeleted: false,
    },
    {
      Title: "Folder",
      DateUpdated: new Date(creationDate.valueOf() + 200),
      ItemId: "564b21f2-05d6-4f7d-8499-304d00ccc3aa",
      ParentId: kEdgeMenuParent,
      IsFolder: true,
      IsDeleted: false,
    },
    {
      Title: "Item in folder",
      URL: "http://www.iteminfolder.org/",
      DateUpdated: new Date(creationDate.valueOf() + 300),
      ItemId: "c295ddaf-04a1-424a-866c-0ebde011e7c8",
      ParentId: "564b21f2-05d6-4f7d-8499-304d00ccc3aa",
      IsFolder: false,
      IsDeleted: false,
    },
    {
      Title: "Deleted folder",
      DateUpdated: new Date(creationDate.valueOf() + 400),
      ItemId: "a547573c-4d4d-4406-a736-5b5462d93bca",
      ParentId: kEdgeMenuParent,
      IsFolder: true,
      IsDeleted: true,
    },
    {
      Title: "Deleted item",
      URL: "http://www.deleteditem.org/",
      DateUpdated: new Date(creationDate.valueOf() + 500),
      ItemId: "37a574bb-b44b-4bbc-a414-908615536435",
      ParentId: kEdgeMenuParent,
      IsFolder: false,
      IsDeleted: true,
    },
    {
      Title: "Item in deleted folder (should be in root)",
      URL: "http://www.itemindeletedfolder.org/",
      DateUpdated: new Date(creationDate.valueOf() + 600),
      ItemId: "74dd1cc3-4c5d-471f-bccc-7bc7c72fa621",
      ParentId: "a547573c-4d4d-4406-a736-5b5462d93bca",
      IsFolder: false,
      IsDeleted: false,
    },
    {
      Title: "_Favorites_Bar_",
      DateUpdated: new Date(creationDate.valueOf() + 700),
      ItemId: "921dc8a0-6c83-40ef-8df1-9bd1c5c56aaf",
      ParentId: kEdgeMenuParent,
      IsFolder: true,
      IsDeleted: false,
    },
    {
      Title: "Item in favorites bar",
      URL: "http://www.iteminfavoritesbar.org/",
      DateUpdated: new Date(creationDate.valueOf() + 800),
      ItemId: "9f2b1ff8-b651-46cf-8f41-16da8bcb6791",
      ParentId: "921dc8a0-6c83-40ef-8df1-9bd1c5c56aaf",
      IsFolder: false,
      IsDeleted: false,
    },
  ];

  let readingListReferenceItems = [
    {
      Title: "Some mozilla page",
      URL: "http://www.mozilla.org/somepage/",
      AddedDate: new Date(creationDate.valueOf() + 900),
      ItemId: "c88426fd-52a7-419d-acbc-d2310e8afebe",
      IsDeleted: false,
    },
    {
      Title: "Some other page",
      URL: "https://www.example.org/somepage/",
      AddedDate: new Date(creationDate.valueOf() + 1000),
      ItemId: "a35fc843-5d5a-4d1e-9be8-45214be24b5c",
      IsDeleted: false,
    },
  ];

  // The following entries are expected to be skipped as being too old to
  // migrate.
  let expiredTypedURLsReferenceItems = [
    {
      URL: "https://expired1.invalid/",
      AccessDateTimeUTC: dateDaysAgo(500),
    },
    {
      URL: "https://expired2.invalid/",
      AccessDateTimeUTC: dateDaysAgo(300),
    },
    {
      URL: "https://expired3.invalid/",
      AccessDateTimeUTC: dateDaysAgo(190),
    },
  ];

  // The following entries should be new enough to migrate.
  let unexpiredTypedURLsReferenceItems = [
    {
      URL: "https://unexpired1.invalid/",
      AccessDateTimeUTC: dateDaysAgo(179),
    },
    {
      URL: "https://unexpired2.invalid/",
      AccessDateTimeUTC: dateDaysAgo(50),
    },
    {
      URL: "https://unexpired3.invalid/",
    },
  ];

  let typedURLsReferenceItems = [
    ...expiredTypedURLsReferenceItems,
    ...unexpiredTypedURLsReferenceItems,
  ];

  Assert.less(
    MigrationUtils.HISTORY_MAX_AGE_IN_DAYS,
    300,
    "This test expects the current pref to be less than the youngest expired visit."
  );
  Assert.greater(
    MigrationUtils.HISTORY_MAX_AGE_IN_DAYS,
    160,
    "This test expects the current pref to be greater than the oldest unexpired visit."
  );

  eseDBWritingHelpers.setupDB(
    db,
    new Map([
      [
        "Favorites",
        {
          columns: [
            { type: COLUMN_TYPES.JET_coltypLongText, name: "URL", cbMax: 4096 },
            {
              type: COLUMN_TYPES.JET_coltypLongText,
              name: "Title",
              cbMax: 4096,
            },
            { type: COLUMN_TYPES.JET_coltypLongLong, name: "DateUpdated" },
            { type: COLUMN_TYPES.JET_coltypGUID, name: "ItemId" },
            { type: COLUMN_TYPES.JET_coltypBit, name: "IsDeleted" },
            { type: COLUMN_TYPES.JET_coltypBit, name: "IsFolder" },
            { type: COLUMN_TYPES.JET_coltypGUID, name: "ParentId" },
          ],
          rows: bookmarkReferenceItems,
        },
      ],
      [
        "ReadingList",
        {
          columns: [
            { type: COLUMN_TYPES.JET_coltypLongText, name: "URL", cbMax: 4096 },
            {
              type: COLUMN_TYPES.JET_coltypLongText,
              name: "Title",
              cbMax: 4096,
            },
            { type: COLUMN_TYPES.JET_coltypLongLong, name: "AddedDate" },
            { type: COLUMN_TYPES.JET_coltypGUID, name: "ItemId" },
            { type: COLUMN_TYPES.JET_coltypBit, name: "IsDeleted" },
          ],
          rows: readingListReferenceItems,
        },
      ],
      [
        "TypedURLs",
        {
          columns: [
            { type: COLUMN_TYPES.JET_coltypLongText, name: "URL", cbMax: 4096 },
            {
              type: COLUMN_TYPES.JET_coltypLongLong,
              name: "AccessDateTimeUTC",
            },
          ],
          rows: typedURLsReferenceItems,
        },
      ],
    ])
  );

  // Manually create an EdgeProfileMigrator rather than going through
  // MigrationUtils.getMigrator to avoid the user data availability check, since
  // we're mocking out that stuff.
  let migrator = new EdgeProfileMigrator();
  let bookmarksMigrator = migrator.getBookmarksMigratorForTesting(db);
  Assert.ok(bookmarksMigrator.exists, "Should recognize db we just created");

  let seenBookmarks = [];
  let listener = events => {
    for (let event of events) {
      let {
        id,
        itemType,
        url,
        title,
        dateAdded,
        guid,
        index,
        parentGuid,
        parentId,
      } = event;
      if (title.startsWith("Deleted")) {
        ok(false, "Should not see deleted items being bookmarked!");
      }
      seenBookmarks.push({
        id,
        parentId,
        index,
        itemType,
        url,
        title,
        dateAdded,
        guid,
        parentGuid,
      });
    }
  };
  PlacesUtils.observers.addListener(["bookmark-added"], listener);

  let migrateResult = await new Promise(resolve =>
    bookmarksMigrator.migrate(resolve)
  ).catch(ex => {
    console.error(ex);
    Assert.ok(false, "Got an exception trying to migrate data! " + ex);
    return false;
  });
  PlacesUtils.observers.removeListener(["bookmark-added"], listener);
  Assert.ok(migrateResult, "Migration should succeed");
  Assert.equal(
    seenBookmarks.length,
    5,
    "Should have seen 5 items being bookmarked."
  );
  Assert.equal(
    seenBookmarks.length,
    MigrationUtils._importQuantities.bookmarks,
    "Telemetry should have items"
  );

  let menuParents = seenBookmarks.filter(
    item => item.parentGuid == PlacesUtils.bookmarks.menuGuid
  );
  Assert.equal(
    menuParents.length,
    3,
    "Bookmarks are added to the menu without a folder"
  );
  let toolbarParents = seenBookmarks.filter(
    item => item.parentGuid == PlacesUtils.bookmarks.toolbarGuid
  );
  Assert.equal(
    toolbarParents.length,
    1,
    "Should have a single item added to the toolbar"
  );
  let menuParentGuid = PlacesUtils.bookmarks.menuGuid;
  let toolbarParentGuid = PlacesUtils.bookmarks.toolbarGuid;

  let expectedTitlesInMenu = bookmarkReferenceItems
    .filter(item => item.ParentId == kEdgeMenuParent)
    .map(item => item.Title);
  // Hacky, but seems like much the simplest way:
  expectedTitlesInMenu.push("Item in deleted folder (should be in root)");
  let expectedTitlesInToolbar = bookmarkReferenceItems
    .filter(item => item.ParentId == "921dc8a0-6c83-40ef-8df1-9bd1c5c56aaf")
    .map(item => item.Title);

  for (let bookmark of seenBookmarks) {
    let shouldBeInMenu = expectedTitlesInMenu.includes(bookmark.title);
    let shouldBeInToolbar = expectedTitlesInToolbar.includes(bookmark.title);
    if (bookmark.title == "Folder") {
      Assert.equal(
        bookmark.itemType,
        PlacesUtils.bookmarks.TYPE_FOLDER,
        "Bookmark " + bookmark.title + " should be a folder"
      );
    } else {
      Assert.notEqual(
        bookmark.itemType,
        PlacesUtils.bookmarks.TYPE_FOLDER,
        "Bookmark " + bookmark.title + " should not be a folder"
      );
    }

    if (shouldBeInMenu) {
      Assert.equal(
        bookmark.parentGuid,
        menuParentGuid,
        "Item '" + bookmark.title + "' should be in menu"
      );
    } else if (shouldBeInToolbar) {
      Assert.equal(
        bookmark.parentGuid,
        toolbarParentGuid,
        "Item '" + bookmark.title + "' should be in toolbar"
      );
    } else if (
      bookmark.guid == menuParentGuid ||
      bookmark.guid == toolbarParentGuid
    ) {
      Assert.ok(
        true,
        "Expect toolbar and menu folders to not be in menu or toolbar"
      );
    } else {
      // Bit hacky, but we do need to check this.
      Assert.equal(
        bookmark.title,
        "Item in folder",
        "Subfoldered item shouldn't be in menu or toolbar"
      );
      let parent = seenBookmarks.find(
        maybeParent => maybeParent.guid == bookmark.parentGuid
      );
      Assert.equal(
        parent && parent.title,
        "Folder",
        "Subfoldered item should be in subfolder labeled 'Folder'"
      );
    }

    let dbItem = bookmarkReferenceItems.find(
      someItem => bookmark.title == someItem.Title
    );
    if (!dbItem) {
      Assert.ok(
        [menuParentGuid, toolbarParentGuid].includes(bookmark.guid),
        "This item should be one of the containers"
      );
    } else {
      Assert.equal(dbItem.URL || "", bookmark.url, "URL is correct");
      Assert.equal(
        dbItem.DateUpdated.valueOf(),
        new Date(bookmark.dateAdded).valueOf(),
        "Date added is correct"
      );
    }
  }

  MigrationUtils._importQuantities.bookmarks = 0;
  seenBookmarks = [];
  listener = events => {
    for (let event of events) {
      let {
        id,
        itemType,
        url,
        title,
        dateAdded,
        guid,
        index,
        parentGuid,
        parentId,
      } = event;
      seenBookmarks.push({
        id,
        parentId,
        index,
        itemType,
        url,
        title,
        dateAdded,
        guid,
        parentGuid,
      });
    }
  };
  PlacesUtils.observers.addListener(["bookmark-added"], listener);

  let readingListMigrator = migrator.getReadingListMigratorForTesting(db);
  Assert.ok(readingListMigrator.exists, "Should recognize db we just created");
  migrateResult = await new Promise(resolve =>
    readingListMigrator.migrate(resolve)
  ).catch(ex => {
    console.error(ex);
    Assert.ok(false, "Got an exception trying to migrate data! " + ex);
    return false;
  });
  PlacesUtils.observers.removeListener(["bookmark-added"], listener);
  Assert.ok(migrateResult, "Migration should succeed");
  Assert.equal(
    seenBookmarks.length,
    3,
    "Should have seen 3 items being bookmarked (2 items + 1 folder)."
  );
  Assert.equal(
    seenBookmarks.length,
    MigrationUtils._importQuantities.bookmarks,
    "Telemetry should have items"
  );
  let readingListContainerLabel = await MigrationUtils.getLocalizedString(
    "migration-imported-edge-reading-list"
  );

  for (let bookmark of seenBookmarks) {
    if (readingListContainerLabel == bookmark.title) {
      continue;
    }
    let referenceItem = readingListReferenceItems.find(
      item => item.Title == bookmark.title
    );
    Assert.ok(referenceItem, "Should have imported what we expected");
    Assert.equal(referenceItem.URL, bookmark.url, "Should have the right URL");
    readingListReferenceItems.splice(
      readingListReferenceItems.findIndex(item => item.Title == bookmark.title),
      1
    );
  }
  Assert.ok(
    !readingListReferenceItems.length,
    "Should have seen all expected items."
  );

  let historyDBMigrator = migrator.getHistoryDBMigratorForTesting(db);
  await new Promise(resolve => {
    historyDBMigrator.migrate(resolve);
  });
  Assert.ok(true, "History DB migration done!");
  for (let expiredEntry of expiredTypedURLsReferenceItems) {
    let entry = await PlacesUtils.history.fetch(expiredEntry.URL, {
      includeVisits: true,
    });
    Assert.equal(entry, null, "Should not have found an entry.");
  }

  for (let unexpiredEntry of unexpiredTypedURLsReferenceItems) {
    let entry = await PlacesUtils.history.fetch(unexpiredEntry.URL, {
      includeVisits: true,
    });
    Assert.equal(entry.url, unexpiredEntry.URL, "Should have the correct URL");
    Assert.ok(!!entry.visits.length, "Should have some visits");
  }
});
