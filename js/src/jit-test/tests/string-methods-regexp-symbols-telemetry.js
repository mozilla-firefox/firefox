// Test the telemetry added in Bug 1950211
assertEq(getUseCounterResults().RegExpSymbolProtocolOnPrimitive, 0);

"abc".match(/b/);
"abc".match({ [Symbol.match]: () => [] });
"abc".match("b");
"abc".match(42);
assertEq(getUseCounterResults().RegExpSymbolProtocolOnPrimitive, 0);
Number.prototype[Symbol.match] = () => [];
"abc".match(42);
assertEq(getUseCounterResults().RegExpSymbolProtocolOnPrimitive, 1);
delete Number.prototype[Symbol.match];

"abc".matchAll(/b/g);
"abc".matchAll({ [Symbol.matchAll]: () => [] });
"abc".matchAll("b");
"abc".matchAll(42);
assertEq(getUseCounterResults().RegExpSymbolProtocolOnPrimitive, 1);
Number.prototype[Symbol.matchAll] = () => [];
"abc".matchAll(42);
assertEq(getUseCounterResults().RegExpSymbolProtocolOnPrimitive, 2);
delete Number.prototype[Symbol.matchAll];

"abc".replace(/b/, "d");
"abc".replace({ [Symbol.replace]: () => "" });
"abc".replace("b", "d");
"abc".replace(42, "d");
assertEq(getUseCounterResults().RegExpSymbolProtocolOnPrimitive, 2);
Number.prototype[Symbol.replace] = () => "";
"abc".replace(42, "d");
assertEq(getUseCounterResults().RegExpSymbolProtocolOnPrimitive, 3);
delete Number.prototype[Symbol.replace];

"abc".replaceAll(/b/g, "d");
"abc".replaceAll({ [Symbol.replace]: () => "" });
"abc".replaceAll("b", "d");
"abc".replaceAll(42, "d");
assertEq(getUseCounterResults().RegExpSymbolProtocolOnPrimitive, 3);
Number.prototype[Symbol.replace] = () => "";
"abc".replaceAll(42, "d");
assertEq(getUseCounterResults().RegExpSymbolProtocolOnPrimitive, 4);
delete Number.prototype[Symbol.replace];

"abc".search(/b/);
"abc".search({ [Symbol.search]: () => -1 });
"abc".search("b");
"abc".search(42);
assertEq(getUseCounterResults().RegExpSymbolProtocolOnPrimitive, 4);
Number.prototype[Symbol.search] = () => -1;
"abc".search(42);
assertEq(getUseCounterResults().RegExpSymbolProtocolOnPrimitive, 5);
delete Number.prototype[Symbol.search];

"abc".split(/b/);
"abc".split({ [Symbol.split]: () => [] });
"abc".split("b");
"abc".split(42);
assertEq(getUseCounterResults().RegExpSymbolProtocolOnPrimitive, 5);
Number.prototype[Symbol.split] = () => [];
"abc".split(42);
assertEq(getUseCounterResults().RegExpSymbolProtocolOnPrimitive, 6);
delete Number.prototype[Symbol.split];
