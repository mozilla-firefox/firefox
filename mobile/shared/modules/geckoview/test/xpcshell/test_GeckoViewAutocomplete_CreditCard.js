/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { CreditCard } = ChromeUtils.importESModule(
  "resource://gre/modules/GeckoViewAutocomplete.sys.mjs"
);

const GECKO_CREDIT_CARD = {
  guid: "abc123",
  "cc-name": "Jon Doe",
  "cc-number": "4111111111111111",
  "cc-exp-month": 12,
  "cc-exp-year": 2028,
  "cc-type": "visa",
};

add_task(function test_fromGecko_without_cvv() {
  const creditCard = CreditCard.fromGecko(GECKO_CREDIT_CARD);

  Assert.equal(creditCard.cvv, "", "A card without cc-csc has no CVV");
  Assert.strictEqual(
    creditCard.toGecko()["cc-csc"],
    "",
    "toGecko passes an empty cc-csc when there is no CVV"
  );
});

add_task(function test_fromGecko_with_cvv() {
  const creditCard = CreditCard.fromGecko({
    ...GECKO_CREDIT_CARD,
    "cc-csc": "123",
  });

  Assert.equal(creditCard.cvv, "123", "cc-csc is mapped to cvv");
  Assert.equal(
    creditCard.toGecko()["cc-csc"],
    "123",
    "cvv is mapped back to cc-csc"
  );
});

add_task(function test_fromGecko_with_numeric_cvv() {
  const creditCard = CreditCard.fromGecko({
    ...GECKO_CREDIT_CARD,
    "cc-csc": 456,
  });

  Assert.strictEqual(creditCard.cvv, "456", "A numeric cc-csc is a string");
});

add_task(function test_parse_with_cvv() {
  const creditCard = CreditCard.parse({
    guid: "abc123",
    name: "Jon Doe",
    number: "4111111111111111",
    expMonth: "12",
    expYear: "2028",
    cvv: "7890",
  });

  Assert.equal(creditCard.cvv, "7890", "parse keeps the cvv");
  Assert.equal(
    creditCard.toGecko()["cc-csc"],
    "7890",
    "A parsed cvv is mapped to cc-csc"
  );
});

add_task(function test_parse_without_cvv() {
  const creditCard = CreditCard.parse({
    guid: "abc123",
    name: "Jon Doe",
    number: "4111111111111111",
    expMonth: "12",
    expYear: "2028",
  });

  Assert.equal(creditCard.cvv, "", "A parsed card without cvv has no CVV");
  Assert.strictEqual(
    creditCard.toGecko()["cc-csc"],
    "",
    "toGecko passes an empty cc-csc for a parsed card without cvv"
  );
});
