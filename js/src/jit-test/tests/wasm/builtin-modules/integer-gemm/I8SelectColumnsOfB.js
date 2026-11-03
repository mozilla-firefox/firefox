// This file contains all the tests for int8_select_columns_of_b intrinsic. It depends
// on the CommonTestSetup.js script which contains the common functionality
// that is required for testing all the intrinsics.
const COMMON_TEST_SETUP_SCRIPT = "./CommonTestSetup.js"

// All tests for this intrinsic as a string
const ALL_TESTS_AS_STRING =`
let {int8_select_columns_of_b} = instance.exports;

const VALID = {input: 0, rows: ROWS_B_MULTIPLIER, cols: COLUMNS_B_MULTIPLIER, colIndexList: ARRAY_ALIGNMENT << 3, sizeColIndexList: SELECTED_COLUMNS_B_MULTIPLIER, output: ARRAY_ALIGNMENT << 5};

function testInvalidSize() {
  let invalidSize;

  // row: 0
  invalidSize = 0;
  assertErrorMessage(() => int8_select_columns_of_b(VALID.input, invalidSize, VALID.cols, VALID.colIndexList, VALID.sizeColIndexList, VALID.output), WebAssembly.RuntimeError, /index out of bounds/);

  // row: Not an integral multiple of ROWS_B_MULTIPLIER
  invalidSize = ROWS_B_MULTIPLIER + 1;
  assertErrorMessage(() => int8_select_columns_of_b(VALID.input, invalidSize, VALID.cols, VALID.colIndexList, VALID.sizeColIndexList, VALID.output), WebAssembly.RuntimeError, /index out of bounds/);

  // col: 0
  invalidSize = 0;
  assertErrorMessage(() => int8_select_columns_of_b(VALID.input, VALID.rows, invalidSize, VALID.colIndexList, VALID.sizeColIndexList, VALID.output), WebAssembly.RuntimeError, /index out of bounds/);

  // col: Not an integral multiple of COLUMNS_B_MULTIPLIER
  invalidSize = COLUMNS_B_MULTIPLIER + 1;
  assertErrorMessage(() => int8_select_columns_of_b(VALID.input, VALID.rows, invalidSize, VALID.colIndexList, VALID.sizeColIndexList, VALID.output), WebAssembly.RuntimeError, /index out of bounds/);

  // sizeColIndexList: 0
  invalidSize = 0;
  assertErrorMessage(() => int8_select_columns_of_b(VALID.input, VALID.rows, VALID.cols, VALID.colIndexList, invalidSize, VALID.output), WebAssembly.RuntimeError, /index out of bounds/);

  // sizeColIndexList: Not an integral multiple of SELECTED_COLUMNS_B_MULTIPLIER
  invalidSize = SELECTED_COLUMNS_B_MULTIPLIER + 1;
  assertErrorMessage(() => int8_select_columns_of_b(VALID.input, VALID.rows, VALID.cols, VALID.colIndexList, invalidSize, VALID.output), WebAssembly.RuntimeError, /index out of bounds/);
}

function testInvalidAlignment() {
  let invalidAlignment = ARRAY_ALIGNMENT + 1;

  // input: Not an integral multiple of ARRAY_ALIGNMENT
  assertErrorMessage(() => int8_select_columns_of_b(invalidAlignment, VALID.rows, VALID.cols, VALID.colIndexList, VALID.sizeColIndexList, VALID.output), WebAssembly.RuntimeError, /index out of bounds/);

  // output: Not an integral multiple of ARRAY_ALIGNMENT. SelectColumnsB writes
  // the output with aligned SIMD stores, so an unaligned output must be
  // rejected rather than faulting on the store (bug 2075984).
  assertErrorMessage(() => int8_select_columns_of_b(VALID.input, VALID.rows, VALID.cols, VALID.colIndexList, VALID.sizeColIndexList, VALID.output + 1), WebAssembly.RuntimeError, /index out of bounds/);

  // colIndexList: Not aligned for its uint32_t elements.
  for (let offset of [1, 2, 3]) {
    assertErrorMessage(() => int8_select_columns_of_b(VALID.input, VALID.rows, VALID.cols, VALID.colIndexList + offset, VALID.sizeColIndexList, VALID.output), WebAssembly.RuntimeError, /index out of bounds/);
  }
}

function testOutOfBounds() {
  let outOfBound;

  // input: Out of Bounds
  outOfBound = PageSizeInBytes - ARRAY_ALIGNMENT;
  assertErrorMessage(() => int8_select_columns_of_b(outOfBound, VALID.rows, VALID.cols, VALID.colIndexList, VALID.sizeColIndexList, VALID.output), WebAssembly.RuntimeError, /index out of bounds/);

  // colIndexList: Out of Bounds
  outOfBound = PageSizeInBytes - VALID.sizeColIndexList;
  assertErrorMessage(() => int8_select_columns_of_b(outOfBound, VALID.rows, VALID.cols, outOfBound, VALID.sizeColIndexList, VALID.output), WebAssembly.RuntimeError, /index out of bounds/);

  // output: Out of Bounds
  outOfBound = PageSizeInBytes - (VALID.rows * VALID.sizeColIndexList);
  assertErrorMessage(() => int8_select_columns_of_b(VALID.input, VALID.rows, VALID.cols, VALID.colIndexList, VALID.sizeColIndexList, outOfBound), WebAssembly.RuntimeError, /index out of bounds/);
}

function testInvalidColumnIndex() {
  // The bound checks above only cover the location of the colIndexList array,
  // not the column indices it contains. Each index must be a valid column of B
  // (< colsB); otherwise SelectColumnsB reads out of bounds of the input matrix
  // and copies host memory into the output buffer.
  let colIndex = new Uint32Array(memory.buffer);
  let base = VALID.colIndexList >> 2;

  for (let wild of [VALID.cols, 0x20000000, 0xffffffff]) {
    for (let i = 0; i < VALID.sizeColIndexList; i++) colIndex[base + i] = 0;
    colIndex[base + 3] = wild;
    assertErrorMessage(() => int8_select_columns_of_b(VALID.input, VALID.rows, VALID.cols, VALID.colIndexList, VALID.sizeColIndexList, VALID.output), WebAssembly.RuntimeError, /index out of bounds/);
  }

  // Restore valid indices so the successful-call test below still passes.
  for (let i = 0; i < VALID.sizeColIndexList; i++) colIndex[base + i] = 0;
}

function testOverlapCheck() {
  // The colIndexList region must not overlap the output region. gemmology reads
  // indices in groups of 8 while writing output incrementally; overlap lets
  // output writes corrupt not-yet-read indices, bypassing the column-index
  // validation and enabling out-of-bounds reads.
  //
  // Place colIndexList at the start of the output region (full overlap).
  assertErrorMessage(() => int8_select_columns_of_b(VALID.input, VALID.rows, VALID.cols, VALID.output, VALID.sizeColIndexList, VALID.output), WebAssembly.RuntimeError, /index out of bounds/);

  // Partial overlap: colIndexList starts inside the output region.
  let outputSize = VALID.rows * VALID.sizeColIndexList;
  let partialOverlap = VALID.output + outputSize - 4;
  assertErrorMessage(() => int8_select_columns_of_b(VALID.input, VALID.rows, VALID.cols, partialOverlap, VALID.sizeColIndexList, VALID.output), WebAssembly.RuntimeError, /index out of bounds/);
}

function testSuccessfulCall() {
  // We just test that with valid arguments the intrinsic executes without any error
  int8_select_columns_of_b(VALID.input, VALID.rows, VALID.cols, VALID.colIndexList, VALID.sizeColIndexList, VALID.output);
  // colIndexList only needs uint32_t alignment.
  int8_select_columns_of_b(VALID.input, VALID.rows, VALID.cols, VALID.colIndexList + 4, VALID.sizeColIndexList, VALID.output);
}

testInvalidSize();
testInvalidAlignment();
testOutOfBounds();
testInvalidColumnIndex();
testOverlapCheck();
testSuccessfulCall();
`

// Run all the tests
import(COMMON_TEST_SETUP_SCRIPT).then((importedModule) => {
  importedModule.runTest(importedModule.COMMON_TEST_SETUP_AS_STRING + ALL_TESTS_AS_STRING);
});
