const defaultLocale = "en";
const defaultCalendar = new Intl.DateTimeFormat(defaultLocale).resolvedOptions().calendar;

function createWithLocale(locale, calendar) {
  return new Intl.DateTimeFormat(locale, {calendar});
}

function create(calendar) {
  return createWithLocale(defaultLocale, calendar);
}

// Empty string should throw.
assertThrowsInstanceOf(() => create(""), RangeError);

// Trailing \0 should throw.
assertThrowsInstanceOf(() => create("gregory\0"), RangeError);

// Too short or too long strings should throw.
assertThrowsInstanceOf(() => create("a"), RangeError);
assertThrowsInstanceOf(() => create("toolongstring"), RangeError);

// Throw even when prefix is valid.
assertThrowsInstanceOf(() => create("gregory-toolongstring"), RangeError);

// |calendar| can be set to |undefined|.
let dtf = create(undefined);
assertEq(dtf.resolvedOptions().calendar, defaultCalendar);

// Unsupported calendars are ignored.
dtf = create("xxxxxxxx");
assertEq(dtf.resolvedOptions().calendar, defaultCalendar);

// Calendars in options overwrite Unicode extension keyword.
dtf = createWithLocale(`${defaultLocale}-u-ca-iso8601`, "japanese");
assertEq(dtf.resolvedOptions().locale, defaultLocale);
assertEq(dtf.resolvedOptions().calendar, "japanese");

// |calendar| option ignores case.
dtf = create("CHINESE");
assertEq(dtf.resolvedOptions().locale, defaultLocale);
assertEq(dtf.resolvedOptions().calendar, "chinese");

const calendars = [
  "buddhist", "chinese", "coptic", "dangi", "ethioaa", "ethiopic-amete-alem",
  "ethiopic", "gregory", "hebrew", "indian", "islamic", "islamic-umalqura",
  "islamic-tbla", "islamic-civil", "islamic-rgsa", "iso8601", "japanese",
  "persian", "roc", "islamicc",
];

// https://github.com/tc39/proposal-intl-locale/issues/96
const canonical = {
  "islamicc": "islamic-civil",
  "ethiopic-amete-alem": "ethioaa",
  "islamic": "islamic-tbla",
  "islamic-rgsa": "gregory",
};

for (let calendar of calendars) {
  let dtf1 = new Intl.DateTimeFormat(`${defaultLocale}-u-ca-${calendar}`);
  let dtf2 = new Intl.DateTimeFormat(defaultLocale, {calendar});

  assertEq(dtf1.resolvedOptions().calendar, canonical[calendar] ?? calendar);
  assertEq(dtf2.resolvedOptions().calendar, canonical[calendar] ?? calendar);

  assertEq(dtf2.format(0), dtf1.format(0));
}

if (typeof reportCompare === "function")
  reportCompare(true, true);
