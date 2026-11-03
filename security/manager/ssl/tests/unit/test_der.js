/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests DER.sys.mjs functionality.

// Until DER.sys.mjs is actually used in production code, this is where we have to
// import it from.
var { DER } = ChromeUtils.importESModule(
  "moz-src:///security/manager/ssl/DER.sys.mjs"
);

function run_simple_tests() {
  throws(
    () => new DER.DERDecoder("this is not an array"),
    /invalid input/,
    "should throw given non-array input"
  );

  let testReadByte = new DER.DERDecoder([0x0a, 0x0b]);
  equal(testReadByte.readByte(), 0x0a, "should read 0x0a");
  equal(testReadByte.readByte(), 0x0b, "should read 0x0b");
  throws(
    () => testReadByte.readByte(),
    /data truncated/,
    "reading more data than is available should fail"
  );

  let testReadBytes = new DER.DERDecoder([0x0c, 0x0d, 0x0e]);
  deepEqual(
    testReadBytes.readBytes(3),
    [0x0c, 0x0d, 0x0e],
    "should read correct sequence of bytes"
  );

  let testReadNegativeBytes = new DER.DERDecoder([0xff, 0xaf]);
  throws(
    () => testReadNegativeBytes.readBytes(-4),
    /invalid length/,
    "reading a negative number of bytes should fail"
  );

  let testReadZeroBytes = new DER.DERDecoder([]);
  equal(
    testReadZeroBytes.readBytes(0).length,
    0,
    "reading zero bytes should result in a zero-length array"
  );

  let testReadTooManyBytes = new DER.DERDecoder([0xab, 0xcd, 0xef]);
  throws(
    () => testReadTooManyBytes.readBytes(4),
    /data truncated/,
    "reading too many bytes should fail"
  );

  let testSEQUENCE = new DER.DERDecoder([0x30, 0x01, 0x01]);
  let content = testSEQUENCE.readTagAndGetContents(DER.SEQUENCE);
  equal(content.length, 1, "content should have length 1");
  equal(content[0], 1, "value of content should be [1]");
  ok(testSEQUENCE.atEnd(), "testSEQUENCE should be at the end of its input");
  testSEQUENCE.assertAtEnd();

  // The length purports to be 4 bytes, but there are only 2 available.
  let truncatedSEQUENCE = new DER.DERDecoder([0x30, 0x04, 0x00, 0x00]);
  throws(
    () => truncatedSEQUENCE.readTagAndGetContents(DER.SEQUENCE),
    /data truncated/,
    "should get 'data truncated' error"
  );

  // With 2 bytes of content, there is 1 remaining after reading the content.
  let extraDataSEQUENCE = new DER.DERDecoder([0x30, 0x02, 0xab, 0xcd, 0xef]);
  content = extraDataSEQUENCE.readTagAndGetContents(DER.SEQUENCE);
  equal(content.length, 2, "content should have length 2");
  deepEqual(content, [0xab, 0xcd], "value of content should be [0xab, 0xcd]");
  ok(
    !extraDataSEQUENCE.atEnd(),
    "extraDataSEQUENCE should not be at the end of its input"
  );
  throws(
    () => extraDataSEQUENCE.assertAtEnd(),
    /extra data/,
    "should get 'extra data' error"
  );

  // The length of 0x81 0x01 is invalid because it could be encoded as just
  // 0x01, which is shorter.
  let invalidLengthSEQUENCE1 = new DER.DERDecoder([0x30, 0x81, 0x01, 0x00]);
  throws(
    () => invalidLengthSEQUENCE1.readTagAndGetContents(DER.SEQUENCE),
    /invalid length/,
    "should get 'invalid length' error"
  );

  // Similarly, 0x82 0x00 0x01 could be encoded as just 0x01, which is shorter.
  let invalidLengthSEQUENCE2 = new DER.DERDecoder([
    0x30, 0x82, 0x00, 0x01, 0x00,
  ]);
  throws(
    () => invalidLengthSEQUENCE2.readTagAndGetContents(DER.SEQUENCE),
    /invalid length/,
    "should get 'invalid length' error"
  );

  // Lengths requiring 4 bytes to encode are not supported.
  let unsupportedLengthSEQUENCE = new DER.DERDecoder([
    0x30, 0x83, 0x01, 0x01, 0x01,
  ]);
  throws(
    () => unsupportedLengthSEQUENCE.readTagAndGetContents(DER.SEQUENCE),
    /unsupported length/,
    "should get 'unsupported length' error"
  );

  // Indefinite lengths are not supported (and aren't DER anyway).
  let unsupportedASN1SEQUENCE = new DER.DERDecoder([
    0x30, 0x80, 0x01, 0x00, 0x00,
  ]);
  throws(
    () => unsupportedASN1SEQUENCE.readTagAndGetContents(DER.SEQUENCE),
    /unsupported asn.1/,
    "should get 'unsupported asn.1' error"
  );

  let unexpectedTag = new DER.DERDecoder([0x31, 0x01, 0x00]);
  throws(
    () => unexpectedTag.readTagAndGetContents(DER.SEQUENCE),
    /unexpected tag/,
    "should get 'unexpected tag' error"
  );

  let readTLVTestcase = new DER.DERDecoder([0x02, 0x03, 0x45, 0x67, 0x89]);
  let bytes = readTLVTestcase.readTLV();
  deepEqual(
    bytes,
    [0x02, 0x03, 0x45, 0x67, 0x89],
    "bytes read with readTLV should be equal to expected value"
  );

  let peekTagTestcase = new DER.DERDecoder([0x30, 0x01, 0x00]);
  ok(
    peekTagTestcase.peekTag(DER.SEQUENCE),
    "peekTag should return true for peeking with a SEQUENCE at a SEQUENCE"
  );
  ok(
    !peekTagTestcase.peekTag(DER.SET),
    "peekTag should return false for peeking with a SET at a SEQUENCE"
  );
  peekTagTestcase.readTLV();
  ok(
    !peekTagTestcase.peekTag(DER.SEQUENCE),
    "peekTag should return false for peeking at a DER with no more data"
  );

  let tlvChoiceTestcase = new DER.DERDecoder([0x31, 0x02, 0xaa, 0xbb]);
  let tlvChoiceContents = tlvChoiceTestcase.readTLVChoice([DER.NULL, DER.SET]);
  deepEqual(
    tlvChoiceContents,
    [0x31, 0x02, 0xaa, 0xbb],
    "readTLVChoice should return expected bytes"
  );

  let tlvChoiceNoMatchTestcase = new DER.DERDecoder([0x30, 0x01, 0xff]);
  throws(
    () => tlvChoiceNoMatchTestcase.readTLVChoice([DER.NULL, DER.SET]),
    /unexpected tag/,
    "readTLVChoice should throw if no matching tag is found"
  );
}

function run_bit_string_tests() {
  let bitstringDER = new DER.DERDecoder([0x03, 0x04, 0x03, 0x01, 0x02, 0xf8]);
  let bitstring = bitstringDER.readBIT_STRING();
  equal(bitstring.unusedBits, 3, "BIT STRING should have 3 unused bits");
  deepEqual(
    bitstring.contents,
    [0x01, 0x02, 0xf8],
    "BIT STRING should have expected contents"
  );

  let bitstringTooManyUnusedBits = new DER.DERDecoder([0x03, 0x02, 0x08, 0x00]);
  throws(
    () => bitstringTooManyUnusedBits.readBIT_STRING(),
    /invalid BIT STRING encoding/,
    "BIT STRING with too many unused bits should throw"
  );

  // A BIT STRING must have the unused bits byte, and so its length must be at
  // least one.
  let bitstringMissingUnusedBits = new DER.DERDecoder([0x03, 0x00]);
  throws(
    () => bitstringMissingUnusedBits.readBIT_STRING(),
    /invalid BIT STRING encoding/,
    "BIT STRING with missing unused bits (and no contents) should throw"
  );

  // The minimal BIT STRING is 03 01 00 (zero bits of padding and zero bytes of
  // content).
  let minimalBitstringDER = new DER.DERDecoder([0x03, 0x01, 0x00]);
  let minimalBitstring = minimalBitstringDER.readBIT_STRING();
  equal(
    minimalBitstring.unusedBits,
    0,
    "minimal BIT STRING should have 0 unused bits"
  );
  equal(
    minimalBitstring.contents.length,
    0,
    "minimal BIT STRING should have empty contents"
  );

  // However, a BIT STRING with zero bytes of content can't have any padding,
  // because that makes no sense.
  let noContentsPaddedBitstringDER = new DER.DERDecoder([0x03, 0x01, 0x03]);
  throws(
    () => noContentsPaddedBitstringDER.readBIT_STRING(),
    /invalid BIT STRING encoding/,
    "BIT STRING with no contents with non-zero padding should throw"
  );
}

function run_compound_tests() {
  let derBytes = [
    0x30,
    0x1a, // SEQUENCE
    0x02,
    0x02,
    0x77,
    0xff, //   INTEGER
    0x06,
    0x03,
    0x2b,
    0x01,
    0x01, //   OBJECT IDENTIFIER
    0x30,
    0x07, //   SEQUENCE
    0x05,
    0x00, //     NULL
    0x02,
    0x03,
    0x45,
    0x46,
    0x47, //     INTEGER
    0x30,
    0x06, //   SEQUENCE
    0x02,
    0x02,
    0x00,
    0xff, //     INTEGER
    0x05,
    0x00,
  ]; //     NULL
  let der = new DER.DERDecoder(derBytes);
  let contents = new DER.DERDecoder(der.readTagAndGetContents(DER.SEQUENCE));
  let firstINTEGER = contents.readTagAndGetContents(DER.INTEGER);
  deepEqual(
    firstINTEGER,
    [0x77, 0xff],
    "first INTEGER should have expected value"
  );
  let oid = contents.readTagAndGetContents(DER.OBJECT_IDENTIFIER);
  deepEqual(
    oid,
    [0x2b, 0x01, 0x01],
    "OBJECT IDENTIFIER should have expected value"
  );

  let firstNested = new DER.DERDecoder(
    contents.readTagAndGetContents(DER.SEQUENCE)
  );
  let firstNestedNULL = firstNested.readTagAndGetContents(DER.NULL);
  equal(
    firstNestedNULL.length,
    0,
    "first nested NULL should have expected value (empty array)"
  );
  let firstNestedINTEGER = firstNested.readTagAndGetContents(DER.INTEGER);
  deepEqual(
    firstNestedINTEGER,
    [0x45, 0x46, 0x47],
    "first nested INTEGER should have expected value"
  );
  firstNested.assertAtEnd();

  let secondNested = new DER.DERDecoder(
    contents.readTagAndGetContents(DER.SEQUENCE)
  );
  let secondNestedINTEGER = secondNested.readTagAndGetContents(DER.INTEGER);
  deepEqual(
    secondNestedINTEGER,
    [0x00, 0xff],
    "second nested INTEGER should have expected value"
  );
  let secondNestedNULL = secondNested.readTagAndGetContents(DER.NULL);
  equal(
    secondNestedNULL.length,
    0,
    "second nested NULL should have expected value (empty array)"
  );
  secondNested.assertAtEnd();

  contents.assertAtEnd();
  der.assertAtEnd();

  let invalidDERBytes = [
    0x30,
    0x06, // SEQUENCE
    0x30,
    0x02, //   SEQUENCE
    0x02,
    0x01, //     INTEGER (missing data)
    0x05,
    0x00, //   NULL
    0x00,
    0x00,
  ]; // (extra data)
  let invalidDER = new DER.DERDecoder(invalidDERBytes);
  let invalidContents = new DER.DERDecoder(
    invalidDER.readTagAndGetContents(DER.SEQUENCE)
  );
  let invalidContentsContents = new DER.DERDecoder(
    invalidContents.readTagAndGetContents(DER.SEQUENCE)
  );
  throws(
    () => invalidContentsContents.readTagAndGetContents(DER.INTEGER),
    /data truncated/,
    "should throw due to missing data"
  );
  let nestedNULL = invalidContents.readTagAndGetContents(DER.NULL);
  equal(nestedNULL.length, 0, "nested NULL should have expected value");
  invalidContents.assertAtEnd();
  throws(
    () => invalidDER.assertAtEnd(),
    /extra data/,
    "should throw due to extra data"
  );
}

function run_test() {
  run_simple_tests();
  run_bit_string_tests();
  run_compound_tests();
}
