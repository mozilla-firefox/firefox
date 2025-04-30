/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_autofill_heuristic_tests([
  {
    description:
      "Form containing an address lookup field with no other address fields",
    fixtureData: `<form>
                  <label>First Name: <input id="firstname"></label>
                  <label>Last Name: <input id="lastname"></label>
                  <label>Address Lookup: <input id="addr-l"></label>
                  <label>Postal Code: <input id="postalcode"></label>
                 </form>`,
    profile: TEST_ADDRESS_1,
    expectedResult: [
      {
        default: {
          reason: "regex-heuristic",
        },
        fields: [
          { fieldName: "given-name", autofill: TEST_ADDRESS_1["given-name"] },
          { fieldName: "family-name", autofill: TEST_ADDRESS_1["family-name"] },
          {
            fieldName: "address-line1",
            autofill:
              TEST_ADDRESS_1["street-address"].replace("\n", " ") +
              " " +
              TEST_ADDRESS_1["street-address"].split(
                "\n"
              )[1] /* extra apartment is due to bug 1930008 */,
          },
          { fieldName: "postal-code", autofill: TEST_ADDRESS_1["postal-code"] },
        ],
      },
    ],
  },
  {
    description:
      "Form containing an address lookup field with other address fields present",
    fixtureData: `<form>
                  <label>First Name: <input id="firstname"></label>
                  <label>Last Name: <input id="lastname"></label>
                  <label>Address Lookup: <input id="addr-l"></label>
                  <label>Street: <input id="street"></label>
                  <label>Apartment: <input id="apt"></label>
                  <label>Postal Code: <input id="postalcode"></label>
                 </form>`,
    profile: TEST_ADDRESS_1,
    expectedResult: [
      {
        default: {
          reason: "regex-heuristic",
        },
        fields: [
          { fieldName: "given-name", autofill: TEST_ADDRESS_1["given-name"] },
          { fieldName: "family-name", autofill: TEST_ADDRESS_1["family-name"] },
          {
            fieldName: "address-line1",
            autofill: "",
          },
          {
            fieldName: "address-line1",
            autofill: TEST_ADDRESS_1["street-address"].split("\n")[0],
            reason: "update-heuristic",
          },
          {
            fieldName: "address-line2",
            autofill: TEST_ADDRESS_1["street-address"].split("\n")[1],
          },
          { fieldName: "postal-code", autofill: TEST_ADDRESS_1["postal-code"] },
        ],
      },
    ],
  },
]);
