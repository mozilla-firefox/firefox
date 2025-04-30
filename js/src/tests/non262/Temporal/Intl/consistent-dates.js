// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty("Temporal")||!this.hasOwnProperty("Intl"))

// Ensure Intl.DateTimeFormat and Temporal return consistent year-month-day values
// for reasonable dates.

const weekdays = [
  "Monday",
  "Tuesday",
  "Wednesday",
  "Thursday",
  "Friday",
  "Saturday",
  "Sunday",
];

// Map Hebrew months to their corresponding month code.
const hebrewMonths = {
  "Tishri": "M01",
  "Heshvan": "M02",
  "Kislev": "M03",
  "Tevet": "M04",
  "Shevat": "M05",
  "Adar I": "M05L",
  "Adar II": "M06",
  "Adar": "M06",
  "Nisan": "M07",
  "Iyar": "M08",
  "Sivan": "M09",
  "Tamuz": "M10",
  "Av": "M11",
  "Elul": "M12",
};

// Extract date information from a to parts formatted date.
function dateFromParts(parts) {
  let relatedYear = undefined;
  let year = undefined;
  let monthCode = "";
  let day = 0;
  let dayOfWeek = -1;

  for (let {type, value} of parts) {
    switch (type) {
      case "weekday":
        dayOfWeek = weekdays.indexOf(value);
        break;
      case "year":
        year = Number(value);
        break;
      case "relatedYear":
        relatedYear = Number(value);
        break;
      case "month": {
        if (value in hebrewMonths) {
          monthCode = hebrewMonths[value];
        } else {
          // Chinese/Dangi leap months end with "bis", from Latin "bis" = "twice".
          let leapMonth = value.endsWith("bis");
          if (leapMonth) {
            value = value.slice(0, -"bis".length);
          }
          monthCode = "M" + value.padStart(2, "0") + (leapMonth ? "L" : "");
        }
        break;
      }
      case "day":
        day = Number(value);
        break;
      case "era":
      case "literal":
        continue;
      default: throw new Error("bad part: " + type);
    }
  }
  assertEq(dayOfWeek >= 0, true);
  assertEq(monthCode.length > 0, true, JSON.stringify(parts));
  assertEq(day > 0, true);

  dayOfWeek += 1;

  return {
    relatedYear,
    year,
    monthCode,
    day,
    dayOfWeek,
  };
}

const tests = {
  buddhist: [
    // Date ranges in 1500..2500 where ICU4C and ICU4X compute different results.
    //
    // NOTE: These are dates before the Gregorian change date October 15, 1582.
    {
      start: {iso: "1500-01-01", year: 2043, monthCode: "M01", day: 1},
      end:   {iso: "1582-10-14", year: 2125, monthCode: "M10", day: 14},
    },
  ],
  chinese: [
    // Date ranges in 1900..2100 where ICU4C and ICU4X compute different results.
    {
      start: {iso: "1906-04-23", relatedYear: 1906, monthCode: "M04", day: 1},
      end:   {iso: "1906-05-22", relatedYear: 1906, monthCode: "M04", day: 30},
    },
    {
      start: {iso: "1917-03-23", relatedYear: 1917, monthCode: "M02L", day: 1},
      end:   {iso: "1917-05-20", relatedYear: 1917, monthCode: "M03", day: 30},
    },
    {
      start: {iso: "1922-06-25", relatedYear: 1922, monthCode: "M05L", day: 1},
      end:   {iso: "1922-08-22", relatedYear: 1922, monthCode: "M06", day: 30},
    },
    {
      start: {iso: "1954-02-03", relatedYear: 1954, monthCode: "M01", day: 1},
      end:   {iso: "1954-03-04", relatedYear: 1954, monthCode: "M01", day: 30},
    },
    {
      start: {iso: "1955-02-22", relatedYear: 1955, monthCode: "M02", day: 1},
      end:   {iso: "1955-03-23", relatedYear: 1955, monthCode: "M02", day: 30},
    },
    {
      start: {iso: "1987-07-26", relatedYear: 1987, monthCode: "M06L", day: 1},
      end:   {iso: "1987-09-22", relatedYear: 1987, monthCode: "M07", day: 30},
    },
    {
      start: {iso: "1999-01-17", relatedYear: 1998, monthCode: "M12", day: 1},
      end:   {iso: "1999-02-15", relatedYear: 1998, monthCode: "M12", day: 30},
    },
    {
      start: {iso: "2012-08-17", relatedYear: 2012, monthCode: "M07", day: 1},
      end:   {iso: "2012-09-15", relatedYear: 2012, monthCode: "M07", day: 30},
    },
    {
      start: {iso: "2018-11-07", relatedYear: 2018, monthCode: "M09", day: 30},
      end:   {iso: "2018-12-06", relatedYear: 2018, monthCode: "M10", day: 29},
    },
    {
      start: {iso: "2027-02-06", relatedYear: 2027, monthCode: "M01", day: 1},
      end:   {iso: "2027-03-07", relatedYear: 2027, monthCode: "M01", day: 30},
    },
    {
      start: {iso: "2030-02-02", relatedYear: 2029, monthCode: "M12", day: 30},
      end:   {iso: "2030-03-03", relatedYear: 2030, monthCode: "M01", day: 29},
    },
    {
      start: {iso: "2057-09-28", relatedYear: 2057, monthCode: "M09", day: 1},
      end:   {iso: "2057-10-27", relatedYear: 2057, monthCode: "M09", day: 30},
    },
    {
      start: {iso: "2070-03-12", relatedYear: 2070, monthCode: "M02", day: 1},
      end:   {iso: "2070-04-10", relatedYear: 2070, monthCode: "M02", day: 30},
    },
  ],
  dangi: [
    // Date ranges in 1900..2100 where ICU4C and ICU4X compute different results.
    {
      start: {iso: "1904-01-17", relatedYear: 1903, monthCode: "M11", day: 30},
      end:   {iso: "1904-02-15", relatedYear: 1903, monthCode: "M12", day: 29},
    },
    {
      start: {iso: "1904-11-07", relatedYear: 1904, monthCode: "M09", day: 30},
      end:   {iso: "1904-12-06", relatedYear: 1904, monthCode: "M10", day: 29},
    },
    {
      start: {iso: "1905-05-04", relatedYear: 1905, monthCode: "M03", day: 30},
      end:   {iso: "1905-06-02", relatedYear: 1905, monthCode: "M04", day: 29},
    },
    {
      start: {iso: "1908-04-30", relatedYear: 1908, monthCode: "M03", day: 30},
      end:   {iso: "1908-05-29", relatedYear: 1908, monthCode: "M04", day: 29},
    },
    {
      start: {iso: "1911-12-20", relatedYear: 1911, monthCode: "M10", day: 30},
      end:   {iso: "1912-01-18", relatedYear: 1911, monthCode: "M11", day: 29},
    },
    {
      start: {iso: "2017-02-26", relatedYear: 2017, monthCode: "M02", day: 1},
      end:   {iso: "2017-03-27", relatedYear: 2017, monthCode: "M02", day: 30},
    },
    {
      start: {iso: "2051-08-06", relatedYear: 2051, monthCode: "M06", day: 30},
      end:   {iso: "2051-09-04", relatedYear: 2051, monthCode: "M07", day: 29},
    },
    {
      start: {iso: "2051-11-03", relatedYear: 2051, monthCode: "M10", day: 1},
      end:   {iso: "2051-12-02", relatedYear: 2051, monthCode: "M10", day: 30},
    },
    {
      start: {iso: "2097-01-13", relatedYear: 2096, monthCode: "M12", day: 1},
      end:   {iso: "2097-02-11", relatedYear: 2096, monthCode: "M12", day: 30},
    },
  ],
  hebrew: [
    // Date ranges in 1500..2500 where ICU4C and ICU4X compute different results.
    // ICU bug report: <https://unicode-org.atlassian.net/browse/ICU-23069>.
    {
      start: {iso: "1700-11-12", year: 5461, monthCode: "M03", day: 1},
      end:   {iso: "1701-12-01", year: 5462, monthCode: "M02", day: 30},
    },
    {
      start: {iso: "1798-11-09", year: 5559, monthCode: "M03", day: 1},
      end:   {iso: "1799-11-28", year: 5560, monthCode: "M02", day: 30},
    },
    {
      start: {iso: "2045-11-10", year: 5806, monthCode: "M03", day: 1},
      end:   {iso: "2046-11-29", year: 5807, monthCode: "M02", day: 30},
    },
    {
      start: {iso: "2292-11-11", year: 6053, monthCode: "M03", day: 1},
      end:   {iso: "2293-11-30", year: 6054, monthCode: "M02", day: 30},
    },
  ],
  "islamic": [
    // TODO: Not yet supported.

    // Date ranges in 2000..2030 where ICU4C and ICU4X compute different results.
    // {
    //   start: {iso: "2000-01-01", year: 1420, monthCode: "M09", day: 23},
    //   end:   {iso: "2029-12-31", year: 1451, monthCode: "M08", day: 25},
    // },
  ],
  "islamic-umalqura": [
    // TODO: Not yet supported.

    // Date ranges in 2000..2030 where ICU4C and ICU4X compute different results.
    // {
    //   start: {iso: "2000-02-06", year: 1420, monthCode: "M11", day: 1},
    //   end:   {iso: "2000-03-06", year: 1420, monthCode: "M11", day: 30},
    // },
    // {
    //   start: {iso: "2000-09-28", year: 1421, monthCode: "M06", day: 30},
    //   end:   {iso: "2000-10-27", year: 1421, monthCode: "M07", day: 29},
    // },
    // {
    //   start: {iso: "2001-10-17", year: 1422, monthCode: "M07", day: 30},
    //   end:   {iso: "2001-11-15", year: 1422, monthCode: "M08", day: 29},
    // },
    // {
    //   start: {iso: "2006-06-26", year: 1427, monthCode: "M06", day: 1},
    //   end:   {iso: "2006-07-25", year: 1427, monthCode: "M06", day: 30},
    // },
    // {
    //   start: {iso: "2024-12-02", year: 1446, monthCode: "M05", day: 30},
    //   end:   {iso: "2024-12-31", year: 1446, monthCode: "M06", day: 29},
    // },
    // {
    //   start: {iso: "2025-01-30", year: 1446, monthCode: "M08", day: 1},
    //   end:   {iso: "2025-02-28", year: 1446, monthCode: "M08", day: 30},
    // },
    // {
    //   start: {iso: "2029-08-11", year: 1451, monthCode: "M04", day: 1},
    //   end:   {iso: "2029-09-09", year: 1451, monthCode: "M04", day: 30},
    // },
    // {
    //   start: {iso: "2029-11-07", year: 1451, monthCode: "M07", day: 1},
    //   end:   {iso: "2029-12-06", year: 1451, monthCode: "M07", day: 30},
    // },
  ],
  japanese: [
    // Date ranges in 1500..2500 where ICU4C and ICU4X compute different results.
    //
    // NOTE: These are dates before the Gregorian change date October 15, 1582.
    {
      start: {iso: "1500-01-01", monthCode: "M01", day: 1},
      end:   {iso: "1582-10-14", monthCode: "M10", day: 14},
    },
  ],
  persian: [
    // Date ranges in 1500..2500 where ICU4C and ICU4X compute different results.
    // More info: https://github.com/unicode-org/icu4x/issues/4713
    {
      start: {iso: "2124-03-20", year: 1503, monthCode: "M01", day: 1},
      end:   {iso: "2125-03-20", year: 1503, monthCode: "M12", day: 30},
    },
    {
      start: {iso: "2223-03-21", year: 1602, monthCode: "M01", day: 1},
      end:   {iso: "2224-03-20", year: 1602, monthCode: "M12", day: 30},
    },
    {
      start: {iso: "2256-03-20", year: 1635, monthCode: "M01", day: 1},
      end:   {iso: "2257-03-20", year: 1635, monthCode: "M12", day: 30},
    },
    {
      start: {iso: "2289-03-20", year: 1668, monthCode: "M01", day: 1},
      end:   {iso: "2290-03-20", year: 1668, monthCode: "M12", day: 30},
    },
    {
      start: {iso: "2322-03-21", year: 1701, monthCode: "M01", day: 1},
      end:   {iso: "2323-03-21", year: 1701, monthCode: "M12", day: 30},
    },
    {
      start: {iso: "2355-03-21", year: 1734, monthCode: "M01", day: 1},
      end:   {iso: "2356-03-20", year: 1734, monthCode: "M12", day: 30},
    },
    {
      start: {iso: "2388-03-20", year: 1767, monthCode: "M01", day: 1},
      end:   {iso: "2389-03-20", year: 1767, monthCode: "M12", day: 30},
    },
    {
      start: {iso: "2421-03-20", year: 1800, monthCode: "M01", day: 1},
      end:   {iso: "2422-03-20", year: 1800, monthCode: "M12", day: 30},
    },
    {
      start: {iso: "2454-03-20", year: 1833, monthCode: "M01", day: 1},
      end:   {iso: "2455-03-20", year: 1833, monthCode: "M12", day: 30},
    },
    {
      start: {iso: "2487-03-20", year: 1866, monthCode: "M01", day: 1},
      end:   {iso: "2488-03-19", year: 1866, monthCode: "M12", day: 30},
    },
  ],
  roc: [
    // Date ranges in 1500..2500 where ICU4C and ICU4X compute different results.
    //
    // NOTE: These are dates before the Gregorian change date October 15, 1582.
    {
      start: {iso: "1500-01-01", eraYear: 412, monthCode: "M01", day: 1},
      end:   {iso: "1582-10-14", eraYear: 330, monthCode: "M10", day: 14},
    },
  ],
};

for (let [calendar, dates] of Object.entries(tests)) {
  let dtf = new Intl.DateTimeFormat("en", {
    timeZone: "UTC",
    calendar,
    year: "numeric",
    month: "numeric",
    day: "numeric",
    weekday: "long",
  });

  for (let {start, end} of dates) {
    // Compute from start date.
    let isoStartDate = Temporal.PlainDate.from(start.iso);
    let startDate = isoStartDate.withCalendar(calendar);
    let startDateParts = dtf.formatToParts(startDate);

    // Compute from end date.
    let isoEndDate = Temporal.PlainDate.from(end.iso);
    let endDate = isoEndDate.withCalendar(calendar);
    let endDateParts = dtf.formatToParts(endDate);

    // Compute from ranges.
    let rangeParts = dtf.formatRangeToParts(startDate, endDate);
    let startRangeDateParts = rangeParts.filter(({source}) => source !== "endRange");
    let endRangeDateParts = rangeParts.filter(({source}) => source !== "startRange");

    // Entries to check.
    let entries = [
      {
        date: startDate,
        parts: startDateParts,
        expected: start,
      },
      {
        date: endDate,
        parts: endDateParts,
        expected: end,
      },
      {
        date: startDate,
        parts: startRangeDateParts,
        expected: start,
      },
      {
        date: endDate,
        parts: endRangeDateParts,
        expected: end,
      },
    ];

    for (let {date, parts, expected} of entries) {
      // Ensure Temporal matches |expected|.
      if (expected.year !== undefined) {
        assertEq(date.year, expected.year);
      }
      if (expected.eraYear !== undefined) {
        assertEq(date.eraYear, expected.eraYear);
      }
      if (expected.relatedYear !== undefined) {
        assertEq(date.with({month: 1}).withCalendar("iso8601").year, expected.relatedYear);
      }
      assertEq(date.monthCode, expected.monthCode);
      assertEq(date.day, expected.day);

      // Ensure Intl.DateTimeFormat matches |expected|.
      let partsDate = dateFromParts(parts);

      if (expected.year !== undefined) {
        assertEq(partsDate.year, expected.year);
      }
      if (expected.eraYear !== undefined) {
        assertEq(partsDate.year, expected.eraYear);
      }
      if (partsDate.relatedYear !== undefined) {
        // NB: relatedYear isn't used for range formats with chinese/dangi calendars.
        assertEq(partsDate.relatedYear, expected.relatedYear);
      }
      assertEq(partsDate.monthCode, expected.monthCode);
      assertEq(partsDate.day, expected.day);
    }
  }
}

if (typeof reportCompare === "function")
  reportCompare(true, true);
