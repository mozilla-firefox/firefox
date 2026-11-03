// |reftest| skip-if(!this.hasOwnProperty('Intl')||!xulRuntime.shell)

// Don't run in browser because `newGlobal` in the browser can't change the time
// zone of other Realms.

// The legacy POSIX-style time zones are aliases for the corresponding city time
// zones when used as the time zone override of a realm.
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

for (let [legacy, city] of Object.entries(legacyZones)) {
    let legacyGlobal = newGlobal({timeZone: legacy});
    let cityGlobal = newGlobal({timeZone: city});

    assertEq(legacyGlobal.Intl.DateTimeFormat().resolvedOptions().timeZone, city);
    if (typeof legacyGlobal.Temporal !== "undefined") {
        assertEq(legacyGlobal.Temporal.Now.timeZoneId(), city);
    }

    for (let date of dates) {
        let legacyDate = new legacyGlobal.Date(date.getTime());
        let cityDate = new cityGlobal.Date(date.getTime());
        assertEq(legacyDate.toString(), cityDate.toString());
        assertEq(legacyDate.getTimezoneOffset(), cityDate.getTimezoneOffset());
        assertEq(legacyDate.toLocaleString("en-US", {timeZoneName: "long"}),
                 cityDate.toLocaleString("en-US", {timeZoneName: "long"}));
        let options = {dateStyle: "full", timeStyle: "full"};
        assertEq(new legacyGlobal.Intl.DateTimeFormat("en-US", options).format(legacyDate),
                 new cityGlobal.Intl.DateTimeFormat("en-US", options).format(cityDate));
    }
}

if (typeof reportCompare === "function")
    reportCompare(0, 0, "ok");
