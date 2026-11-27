var obj = { 0: "a", 1: "b", 2: "c", length: 1 };
assertEq(Array.prototype.shift.call(obj), "a");
assertEq(JSON.stringify(obj), '{"1":"b","2":"c","length":0}');

obj = { 0: "a", 1: "b", 2: "c", length: 2 };
assertEq(Array.prototype.shift.call(obj), "a");
assertEq(JSON.stringify(obj), '{"0":"b","2":"c","length":1}');

obj = {};
for (var i = 0; i < 20; i++) obj[i] = i;
obj.length = 1;
assertEq(Array.prototype.unshift.call(obj, "x"), 2);
assertEq(obj[0], "x");
assertEq(obj[1], 0);
for (var i = 2; i < 20; i++) assertEq(obj[i], i);
assertEq(20 in obj, false);
