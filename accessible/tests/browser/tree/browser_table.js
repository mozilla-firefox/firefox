/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../../mochitest/role.js */
loadScripts({ name: "role.js", dir: MOCHITESTS_DIR });

/**
 * Verify that rows and cells retain their native roles when inside thead,
 * tbody and tfoot with an explicit rowgroup role, nested in a table which has
 * an explicit ARIA role="table".
 */
addAccessibleTask(
  `
<table id="table" role="table">
  <thead role="rowgroup">
    <tr><th>a</th></tr>
  </thead>
  <tbody role="rowgroup">
    <tr><td>b</td></tr>
  </tbody>
  <tfoot role="rowgroup">
    <tr><td>c</td></tr>
  </tfoot>
</table>
  `,
  async function testAriaRowgroup(browser, docAcc) {
    const table = findAccessibleChildByID(docAcc, "table");
    testAccessibleTree(table, {
      TABLE: [
        {
          ROWGROUP: [{ ROW: [{ COLUMNHEADER: [{ TEXT_LEAF: [] }] }] }],
        },
        { ROWGROUP: [{ ROW: [{ CELL: [{ TEXT_LEAF: [] }] }] }] },
        { ROWGROUP: [{ ROW: [{ CELL: [{ TEXT_LEAF: [] }] }] }] },
      ],
    });
  },
  { chrome: true, topLevel: true }
);

/**
 * Verify that rows and cells retain their native roles when inside a tbody
 * with an explicit rowgroup role, nested in a table without an explicit ARIA
 * role.
 */
addAccessibleTask(
  `
<table id="table">
  <tbody role="rowgroup">
    <tr><td>a</td></tr>
  </tbody>
</table>
  `,
  async function testAriaRowgroupNativeTable(browser, docAcc) {
    const table = findAccessibleChildByID(docAcc, "table");
    testAccessibleTree(table, {
      TABLE: [{ ROWGROUP: [{ ROW: [{ CELL: [{ TEXT_LEAF: [] }] }] }] }],
    });
  },
  { chrome: true, topLevel: true }
);

/**
 * Verify that rows and cells don't expose their native roles when inside a
 * tbody with an explicit rowgroup role in a table with role="none".
 */
addAccessibleTask(
  `
<table role="none">
  <tbody id="rowgroup" role="rowgroup">
    <tr><td>a</td></tr>
  </tbody>
</table>
  `,
  async function testAriaRowgroupNoneTable(browser, docAcc) {
    const rowgroup = findAccessibleChildByID(docAcc, "rowgroup");
    testAccessibleTree(rowgroup, {
      ROWGROUP: [{ TEXT_CONTAINER: [{ TEXT_LEAF: [] }] }],
    });
  },
  { chrome: true, topLevel: true }
);

/**
 * Verify that rows and cells don't expose their native roles when inside a
 * tbody with an explicit rowgroup role, nested in a table with a non-table role.
 */
addAccessibleTask(
  `
<table id="table" role="list">
  <tbody role="rowgroup">
    <tr><td>a</td></tr>
  </tbody>
</table>
  `,
  async function testAriaRowgroupListTable(browser, docAcc) {
    const table = findAccessibleChildByID(docAcc, "table");
    testAccessibleTree(table, {
      LIST: [
        {
          ROWGROUP: [
            { TEXT_CONTAINER: [{ TEXT_CONTAINER: [{ TEXT_LEAF: [] }] }] },
          ],
        },
      ],
    });
  },
  { chrome: true, topLevel: true }
);
