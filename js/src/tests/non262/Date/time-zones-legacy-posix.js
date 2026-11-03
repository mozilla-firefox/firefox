// |reftest| skip-if(winWidget||!this.hasOwnProperty("Intl")) -- Windows doesn't accept IANA names for the TZ env variable; Requires ICU time zone support

// The legacy POSIX-style time zones are aliases for the corresponding city time
// zones when used as the default time zone.
//
// See <https://github.com/tc39/ecma402/issues/1103>.

const legacyZones = {
    "CST6CDT": "America/Chicago",
    "EST5EDT": "America/New_York",
    "MST7MDT": "America/Denver",
    "PST8PDT": "America/Los_Angeles",
};

const dates = [new Date(Date.UTC(1850, 0, 1))];
for (let year = 1900; year <= 2040; year += 4) {
    for (let month = 0; month < 12; month++) {
        dates.push(new Date(Date.UTC(year, month, 15, 12)));
    }
}

function describe(tz) {
    let result;
    inTimeZone(tz, () => {
        let dtf = new Intl.DateTimeFormat("en-US", {dateStyle: "full", timeStyle: "full"});
        result = {
            id: dtf.resolvedOptions().timeZone,
            now: typeof Temporal !== "undefined" ? Temporal.Now.timeZoneId() : undefined,
            dates: dates.map(date => [
                date.toString(),
                date.toTimeString(),
                date.getTimezoneOffset(),
                date.getHours(),
                date.toLocaleString("en-US", {timeZoneName: "long"}),
                dtf.format(date),
                new Date(date.getFullYear(), date.getMonth(), date.getDate()).getTime(),
            ]),
        };
    });
    return result;
}

for (let [legacy, city] of Object.entries(legacyZones)) {
    let legacyResult = describe(legacy);
    assertEq(legacyResult.id, city);
    if (typeof Temporal !== "undefined") {
        assertEq(legacyResult.now, city);
    }
    assertEqArray(legacyResult.dates.flat(), describe(city).dates.flat());
}

if (typeof reportCompare === "function")
    reportCompare(0, 0, "ok");
