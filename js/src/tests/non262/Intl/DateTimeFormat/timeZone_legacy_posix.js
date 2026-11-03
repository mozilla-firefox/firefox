// |reftest| skip-if(!this.hasOwnProperty("Intl")) -- Requires ICU time zone support

// The legacy POSIX-style time zones are aliases for the corresponding city time
// zones. They aren't exposed as canonical time zone identifiers, and use the
// time zone rules and localized names of the city time zones.
//
// See <https://github.com/tc39/ecma402/issues/1103>.

const legacyZones = {
    "CST6CDT": "America/Chicago",
    "EST5EDT": "America/New_York",
    "MST7MDT": "America/Denver",
    "PST8PDT": "America/Los_Angeles",
};

const supported = Intl.supportedValuesOf("timeZone");

// Dates before and after the transition to the modern US time zone rules.
const dates = [new Date(Date.UTC(1850, 0, 1))];
for (let year = 1900; year <= 2040; year += 4) {
    for (let month = 0; month < 12; month++) {
        dates.push(new Date(Date.UTC(year, month, 15, 12)));
    }
}

for (let [legacy, city] of Object.entries(legacyZones)) {
    // Canonicalization of the time zone identifier.
    for (let id of [legacy, legacy.toLowerCase()]) {
        let options = new Intl.DateTimeFormat("en-US", {timeZone: id}).resolvedOptions();
        assertEq(options.timeZone, city);
    }

    // Not exposed as supported time zone identifiers.
    assertEq(supported.includes(legacy), false);
    assertEq(supported.includes(city), true);

    // Localized time zone names and time zone offsets match the city.
    for (let timeZoneName of ["long", "short", "longGeneric", "longOffset"]) {
        let legacyDTF = new Intl.DateTimeFormat("en-US", {timeZone: legacy, timeZoneName});
        let cityDTF = new Intl.DateTimeFormat("en-US", {timeZone: city, timeZoneName});
        for (let date of dates) {
            assertEq(legacyDTF.format(date), cityDTF.format(date));
        }
    }

    // Date and time styles.
    for (let style of ["full", "long", "medium", "short"]) {
        let options = {dateStyle: style, timeStyle: style};
        let legacyDTF = new Intl.DateTimeFormat("en-US", {...options, timeZone: legacy});
        let cityDTF = new Intl.DateTimeFormat("en-US", {...options, timeZone: city});
        for (let date of dates) {
            assertEq(legacyDTF.format(date), cityDTF.format(date));
        }
    }

    // formatToParts, formatRange, and formatRangeToParts.
    {
        let options = {dateStyle: "full", timeStyle: "full"};
        let legacyDTF = new Intl.DateTimeFormat("en-US", {...options, timeZone: legacy});
        let cityDTF = new Intl.DateTimeFormat("en-US", {...options, timeZone: city});
        for (let i = 0; i < dates.length; i++) {
            let start = dates[i];
            let end = dates[Math.min(i + 1, dates.length - 1)];
            assertDeepEq(legacyDTF.formatToParts(start), cityDTF.formatToParts(start));
            assertEq(legacyDTF.formatRange(start, end), cityDTF.formatRange(start, end));
            assertDeepEq(legacyDTF.formatRangeToParts(start, end),
                         cityDTF.formatRangeToParts(start, end));
        }
    }

    // Date.prototype.toLocale{,Date,Time}String.
    for (let date of dates) {
        for (let method of ["toLocaleString", "toLocaleDateString", "toLocaleTimeString"]) {
            assertEq(date[method]("en-US", {timeZone: legacy, timeZoneName: "long"}),
                     date[method]("en-US", {timeZone: city, timeZoneName: "long"}));
        }
    }

    // Mozilla-only pattern option.
    if (typeof addIntlExtras === "function") {
        let mozIntl = {};
        addIntlExtras(mozIntl);
        let pattern = "yyyy-MM-dd HH:mm:ss zzzz";
        let legacyDTF = new mozIntl.DateTimeFormat("en-US", {pattern, timeZone: legacy});
        let cityDTF = new mozIntl.DateTimeFormat("en-US", {pattern, timeZone: city});
        for (let date of dates) {
            assertEq(legacyDTF.format(date), cityDTF.format(date));
        }
    }

    if (typeof Temporal !== "undefined") {
        for (let date of dates) {
            let iso = date.toISOString().slice(0, 19);
            let legacyZDT = Temporal.ZonedDateTime.from(`${iso}[${legacy}]`);
            let cityZDT = Temporal.ZonedDateTime.from(`${iso}[${city}]`);
            assertEq(legacyZDT.offset, cityZDT.offset);
            assertEq(legacyZDT.epochNanoseconds, cityZDT.epochNanoseconds);
            assertEq(legacyZDT.equals(cityZDT), true);
            for (let timeZoneName of ["long", "short", "longGeneric"]) {
                assertEq(legacyZDT.toLocaleString("en-US", {timeZoneName}),
                         cityZDT.toLocaleString("en-US", {timeZoneName}));
            }
            for (let style of ["full", "long", "medium", "short"]) {
                let options = {dateStyle: style, timeStyle: style};
                assertEq(legacyZDT.toLocaleString("en-US", options),
                         cityZDT.toLocaleString("en-US", options));
            }

            // Operations which depend on the time zone rules.
            assertEq(legacyZDT.hoursInDay, cityZDT.hoursInDay);
            assertEq(legacyZDT.startOfDay().epochNanoseconds,
                     cityZDT.startOfDay().epochNanoseconds);
            for (let direction of ["next", "previous"]) {
                let legacyTransition = legacyZDT.getTimeZoneTransition(direction);
                let cityTransition = cityZDT.getTimeZoneTransition(direction);
                assertEq(legacyTransition?.epochNanoseconds,
                         cityTransition?.epochNanoseconds);
            }
            let plain = cityZDT.toPlainDateTime();
            assertEq(plain.toZonedDateTime(legacy).epochNanoseconds,
                     plain.toZonedDateTime(city).epochNanoseconds);
            assertEq(cityZDT.withTimeZone(legacy).toPlainDateTime().toString(),
                     cityZDT.toPlainDateTime().toString());

            // The identifier itself is preserved.
            assertEq(legacyZDT.timeZoneId, legacy);
        }
    }
}

if (typeof reportCompare === "function")
    reportCompare(0, 0, "ok");
