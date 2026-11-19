"use strict";
var __extends = (this && this.__extends) || (function () {
    var extendStatics = function (d, b) {
        extendStatics = Object.setPrototypeOf ||
            ({ __proto__: [] } instanceof Array && function (d, b) { d.__proto__ = b; }) ||
            function (d, b) { for (var p in b) if (Object.prototype.hasOwnProperty.call(b, p)) d[p] = b[p]; };
        return extendStatics(d, b);
    };
    return function (d, b) {
        if (typeof b !== "function" && b !== null)
            throw new TypeError("Class extends value " + String(b) + " is not a constructor or null");
        extendStatics(d, b);
        function __() { this.constructor = d; }
        d.prototype = b === null ? Object.create(b) : (__.prototype = b.prototype, new __());
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.DevToolsNameGenerator = exports.DevToolsNameResolver = exports.NameSectionReader = exports.WasmDisassembler = exports.LabelMode = exports.NumericNameResolver = exports.DefaultNameResolver = void 0;
/* Copyright 2016 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
var WasmParser_js_1 = require("./WasmParser.js");
var NAME_SECTION_NAME = "name";
var INVALID_NAME_SYMBOLS_REGEX = /[^0-9A-Za-z!#$%&'*+.:<=>?@^_`|~\/\-]/;
var INVALID_NAME_SYMBOLS_REGEX_GLOBAL = new RegExp(INVALID_NAME_SYMBOLS_REGEX.source, "g");
function formatFloat32(n, bits) {
    if (n === 0)
        return 1 / n < 0 ? "-0.0" : "0.0";
    if (isFinite(n))
        return n.toString();
    if (!isNaN(n))
        return n < 0 ? "-inf" : "inf";
    var data;
    if (bits !== undefined) {
        // Use the original bits: converting to a JS number may quiet a sNaN.
        data = bits | 0;
    }
    else {
        var view = new DataView(new ArrayBuffer(8));
        view.setFloat32(0, n, true);
        data = view.getInt32(0, true);
    }
    var payload = data & 0x7fffff;
    var canonicalBits = 4194304; // 0x800..0
    if (data > 0 && payload === canonicalBits)
        return "nan";
    // canonical NaN;
    else if (payload === canonicalBits)
        return "-nan";
    return (data < 0 ? "-" : "+") + "nan:0x" + payload.toString(16);
}
function formatFloat64(n) {
    if (n === 0)
        return 1 / n < 0 ? "-0.0" : "0.0";
    if (isFinite(n))
        return n.toString();
    if (!isNaN(n))
        return n < 0 ? "-inf" : "inf";
    var view = new DataView(new ArrayBuffer(8));
    view.setFloat64(0, n, true);
    var data1 = view.getUint32(0, true);
    var data2 = view.getInt32(4, true);
    var payload = data1 + (data2 & 0xfffff) * 4294967296;
    var canonicalBits = 524288 * 4294967296; // 0x800..0
    if (data2 > 0 && payload === canonicalBits)
        return "nan";
    // canonical NaN;
    else if (payload === canonicalBits)
        return "-nan";
    return (data2 < 0 ? "-" : "+") + "nan:0x" + payload.toString(16);
}
function formatI32Array(bytes, count) {
    var dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    var result = [];
    for (var i = 0; i < count; i++)
        result.push("0x".concat(formatHex(dv.getInt32(i << 2, true), 8)));
    return result.join(" ");
}
function formatI8Array(bytes, count) {
    var dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    var result = [];
    for (var i = 0; i < count; i++)
        result.push("".concat(dv.getInt8(i)));
    return result.join(" ");
}
function memoryAddressToString(address, code) {
    var defaultAlignFlags;
    switch (code) {
        case 1036288 /* OperatorCode.v128_load */:
        case 1036289 /* OperatorCode.v128_load8x8_s */:
        case 1036290 /* OperatorCode.v128_load8x8_u */:
        case 1036291 /* OperatorCode.v128_load16x4_s */:
        case 1036292 /* OperatorCode.v128_load16x4_u */:
        case 1036293 /* OperatorCode.v128_load32x2_s */:
        case 1036294 /* OperatorCode.v128_load32x2_u */:
        case 1036295 /* OperatorCode.v128_load8_splat */:
        case 1036296 /* OperatorCode.v128_load16_splat */:
        case 1036297 /* OperatorCode.v128_load32_splat */:
        case 1036298 /* OperatorCode.v128_load64_splat */:
        case 1036299 /* OperatorCode.v128_store */:
            defaultAlignFlags = 4;
            break;
        case 41 /* OperatorCode.i64_load */:
        case 55 /* OperatorCode.i64_store */:
        case 43 /* OperatorCode.f64_load */:
        case 57 /* OperatorCode.f64_store */:
        case 65026 /* OperatorCode.memory_atomic_wait64 */:
        case 65041 /* OperatorCode.i64_atomic_load */:
        case 65048 /* OperatorCode.i64_atomic_store */:
        case 65055 /* OperatorCode.i64_atomic_rmw_add */:
        case 65062 /* OperatorCode.i64_atomic_rmw_sub */:
        case 65069 /* OperatorCode.i64_atomic_rmw_and */:
        case 65076 /* OperatorCode.i64_atomic_rmw_or */:
        case 65083 /* OperatorCode.i64_atomic_rmw_xor */:
        case 65090 /* OperatorCode.i64_atomic_rmw_xchg */:
        case 65097 /* OperatorCode.i64_atomic_rmw_cmpxchg */:
        case 1036381 /* OperatorCode.v128_load64_zero */:
        case 1036375 /* OperatorCode.v128_load64_lane */:
        case 1036379 /* OperatorCode.v128_store64_lane */:
            defaultAlignFlags = 3;
            break;
        case 40 /* OperatorCode.i32_load */:
        case 52 /* OperatorCode.i64_load32_s */:
        case 53 /* OperatorCode.i64_load32_u */:
        case 54 /* OperatorCode.i32_store */:
        case 62 /* OperatorCode.i64_store32 */:
        case 42 /* OperatorCode.f32_load */:
        case 56 /* OperatorCode.f32_store */:
        case 65024 /* OperatorCode.memory_atomic_notify */:
        case 65025 /* OperatorCode.memory_atomic_wait32 */:
        case 65040 /* OperatorCode.i32_atomic_load */:
        case 65046 /* OperatorCode.i64_atomic_load32_u */:
        case 65047 /* OperatorCode.i32_atomic_store */:
        case 65053 /* OperatorCode.i64_atomic_store32 */:
        case 65054 /* OperatorCode.i32_atomic_rmw_add */:
        case 65060 /* OperatorCode.i64_atomic_rmw32_add_u */:
        case 65061 /* OperatorCode.i32_atomic_rmw_sub */:
        case 65067 /* OperatorCode.i64_atomic_rmw32_sub_u */:
        case 65068 /* OperatorCode.i32_atomic_rmw_and */:
        case 65074 /* OperatorCode.i64_atomic_rmw32_and_u */:
        case 65075 /* OperatorCode.i32_atomic_rmw_or */:
        case 65081 /* OperatorCode.i64_atomic_rmw32_or_u */:
        case 65082 /* OperatorCode.i32_atomic_rmw_xor */:
        case 65088 /* OperatorCode.i64_atomic_rmw32_xor_u */:
        case 65089 /* OperatorCode.i32_atomic_rmw_xchg */:
        case 65095 /* OperatorCode.i64_atomic_rmw32_xchg_u */:
        case 65096 /* OperatorCode.i32_atomic_rmw_cmpxchg */:
        case 65102 /* OperatorCode.i64_atomic_rmw32_cmpxchg_u */:
        case 1036380 /* OperatorCode.v128_load32_zero */:
        case 1036374 /* OperatorCode.v128_load32_lane */:
        case 1036378 /* OperatorCode.v128_store32_lane */:
            defaultAlignFlags = 2;
            break;
        case 46 /* OperatorCode.i32_load16_s */:
        case 47 /* OperatorCode.i32_load16_u */:
        case 50 /* OperatorCode.i64_load16_s */:
        case 51 /* OperatorCode.i64_load16_u */:
        case 59 /* OperatorCode.i32_store16 */:
        case 61 /* OperatorCode.i64_store16 */:
        case 65043 /* OperatorCode.i32_atomic_load16_u */:
        case 65045 /* OperatorCode.i64_atomic_load16_u */:
        case 65050 /* OperatorCode.i32_atomic_store16 */:
        case 65052 /* OperatorCode.i64_atomic_store16 */:
        case 65057 /* OperatorCode.i32_atomic_rmw16_add_u */:
        case 65059 /* OperatorCode.i64_atomic_rmw16_add_u */:
        case 65064 /* OperatorCode.i32_atomic_rmw16_sub_u */:
        case 65066 /* OperatorCode.i64_atomic_rmw16_sub_u */:
        case 65071 /* OperatorCode.i32_atomic_rmw16_and_u */:
        case 65073 /* OperatorCode.i64_atomic_rmw16_and_u */:
        case 65078 /* OperatorCode.i32_atomic_rmw16_or_u */:
        case 65080 /* OperatorCode.i64_atomic_rmw16_or_u */:
        case 65085 /* OperatorCode.i32_atomic_rmw16_xor_u */:
        case 65087 /* OperatorCode.i64_atomic_rmw16_xor_u */:
        case 65092 /* OperatorCode.i32_atomic_rmw16_xchg_u */:
        case 65094 /* OperatorCode.i64_atomic_rmw16_xchg_u */:
        case 65099 /* OperatorCode.i32_atomic_rmw16_cmpxchg_u */:
        case 65101 /* OperatorCode.i64_atomic_rmw16_cmpxchg_u */:
        case 1036373 /* OperatorCode.v128_load16_lane */:
        case 1036377 /* OperatorCode.v128_store16_lane */:
            defaultAlignFlags = 1;
            break;
        case 44 /* OperatorCode.i32_load8_s */:
        case 45 /* OperatorCode.i32_load8_u */:
        case 48 /* OperatorCode.i64_load8_s */:
        case 49 /* OperatorCode.i64_load8_u */:
        case 58 /* OperatorCode.i32_store8 */:
        case 60 /* OperatorCode.i64_store8 */:
        case 65042 /* OperatorCode.i32_atomic_load8_u */:
        case 65044 /* OperatorCode.i64_atomic_load8_u */:
        case 65049 /* OperatorCode.i32_atomic_store8 */:
        case 65051 /* OperatorCode.i64_atomic_store8 */:
        case 65056 /* OperatorCode.i32_atomic_rmw8_add_u */:
        case 65058 /* OperatorCode.i64_atomic_rmw8_add_u */:
        case 65063 /* OperatorCode.i32_atomic_rmw8_sub_u */:
        case 65065 /* OperatorCode.i64_atomic_rmw8_sub_u */:
        case 65070 /* OperatorCode.i32_atomic_rmw8_and_u */:
        case 65072 /* OperatorCode.i64_atomic_rmw8_and_u */:
        case 65077 /* OperatorCode.i32_atomic_rmw8_or_u */:
        case 65079 /* OperatorCode.i64_atomic_rmw8_or_u */:
        case 65084 /* OperatorCode.i32_atomic_rmw8_xor_u */:
        case 65086 /* OperatorCode.i64_atomic_rmw8_xor_u */:
        case 65091 /* OperatorCode.i32_atomic_rmw8_xchg_u */:
        case 65093 /* OperatorCode.i64_atomic_rmw8_xchg_u */:
        case 65098 /* OperatorCode.i32_atomic_rmw8_cmpxchg_u */:
        case 65100 /* OperatorCode.i64_atomic_rmw8_cmpxchg_u */:
        case 1036372 /* OperatorCode.v128_load8_lane */:
        case 1036376 /* OperatorCode.v128_store8_lane */:
            defaultAlignFlags = 0;
            break;
    }
    if (address.flags == defaultAlignFlags)
        // hide default flags
        return !address.offset ? null : "offset=".concat(address.offset);
    if (!address.offset)
        // hide default offset
        return "align=".concat(1 << address.flags);
    return "offset=".concat(address.offset, " align=").concat(1 << address.flags);
}
function limitsToString(limits, is64) {
    return ((is64 ? "i64 " : "") +
        limits.initial +
        (limits.maximum !== undefined ? " " + limits.maximum : ""));
}
var paddingCache = ["0", "00", "000"];
function formatHex(n, width) {
    var s = (n >>> 0).toString(16).toUpperCase();
    if (width === undefined || s.length >= width)
        return s;
    var paddingIndex = width - s.length - 1;
    while (paddingIndex >= paddingCache.length)
        paddingCache.push(paddingCache[paddingCache.length - 1] + "0");
    return paddingCache[paddingIndex] + s;
}
var IndentIncrement = "  ";
function isValidName(name) {
    return !INVALID_NAME_SYMBOLS_REGEX.test(name);
}
var DefaultNameResolver = /** @class */ (function () {
    function DefaultNameResolver() {
    }
    DefaultNameResolver.prototype.getTypeName = function (index, isRef) {
        return "$type" + index;
    };
    DefaultNameResolver.prototype.getTableName = function (index, isRef) {
        return "$table" + index;
    };
    DefaultNameResolver.prototype.getMemoryName = function (index, isRef) {
        return "$memory" + index;
    };
    DefaultNameResolver.prototype.getGlobalName = function (index, isRef) {
        return "$global" + index;
    };
    DefaultNameResolver.prototype.getElementName = function (index, isRef) {
        return "$elem".concat(index);
    };
    DefaultNameResolver.prototype.getTagName = function (index, isRef) {
        return "$tag".concat(index);
    };
    DefaultNameResolver.prototype.getFunctionName = function (index, isImport, isRef) {
        return (isImport ? "$import" : "$func") + index;
    };
    DefaultNameResolver.prototype.getVariableName = function (funcIndex, index, isRef) {
        return "$var" + index;
    };
    DefaultNameResolver.prototype.getFieldName = function (typeIndex, index, isRef) {
        return "$field" + index;
    };
    DefaultNameResolver.prototype.getLabel = function (index) {
        return "$label" + index;
    };
    return DefaultNameResolver;
}());
exports.DefaultNameResolver = DefaultNameResolver;
var EMPTY_STRING_ARRAY = [];
var DevToolsExportMetadata = /** @class */ (function () {
    function DevToolsExportMetadata(functionExportNames, globalExportNames, memoryExportNames, tableExportNames, eventExportNames) {
        this._functionExportNames = functionExportNames;
        this._globalExportNames = globalExportNames;
        this._memoryExportNames = memoryExportNames;
        this._tableExportNames = tableExportNames;
        this._eventExportNames = eventExportNames;
    }
    DevToolsExportMetadata.prototype.getFunctionExportNames = function (index) {
        var _a;
        return (_a = this._functionExportNames[index]) !== null && _a !== void 0 ? _a : EMPTY_STRING_ARRAY;
    };
    DevToolsExportMetadata.prototype.getGlobalExportNames = function (index) {
        var _a;
        return (_a = this._globalExportNames[index]) !== null && _a !== void 0 ? _a : EMPTY_STRING_ARRAY;
    };
    DevToolsExportMetadata.prototype.getMemoryExportNames = function (index) {
        var _a;
        return (_a = this._memoryExportNames[index]) !== null && _a !== void 0 ? _a : EMPTY_STRING_ARRAY;
    };
    DevToolsExportMetadata.prototype.getTableExportNames = function (index) {
        var _a;
        return (_a = this._tableExportNames[index]) !== null && _a !== void 0 ? _a : EMPTY_STRING_ARRAY;
    };
    DevToolsExportMetadata.prototype.getTagExportNames = function (index) {
        var _a;
        return (_a = this._eventExportNames[index]) !== null && _a !== void 0 ? _a : EMPTY_STRING_ARRAY;
    };
    return DevToolsExportMetadata;
}());
var NumericNameResolver = /** @class */ (function () {
    function NumericNameResolver() {
    }
    NumericNameResolver.prototype.getTypeName = function (index, isRef) {
        return isRef ? "" + index : "(;".concat(index, ";)");
    };
    NumericNameResolver.prototype.getTableName = function (index, isRef) {
        return isRef ? "" + index : "(;".concat(index, ";)");
    };
    NumericNameResolver.prototype.getMemoryName = function (index, isRef) {
        return isRef ? "" + index : "(;".concat(index, ";)");
    };
    NumericNameResolver.prototype.getGlobalName = function (index, isRef) {
        return isRef ? "" + index : "(;".concat(index, ";)");
    };
    NumericNameResolver.prototype.getElementName = function (index, isRef) {
        return isRef ? "" + index : "(;".concat(index, ";)");
    };
    NumericNameResolver.prototype.getTagName = function (index, isRef) {
        return isRef ? "" + index : "(;".concat(index, ";)");
    };
    NumericNameResolver.prototype.getFunctionName = function (index, isImport, isRef) {
        return isRef ? "" + index : "(;".concat(index, ";)");
    };
    NumericNameResolver.prototype.getVariableName = function (funcIndex, index, isRef) {
        return isRef ? "" + index : "(;".concat(index, ";)");
    };
    NumericNameResolver.prototype.getFieldName = function (typeIndex, index, isRef) {
        return isRef ? "" : index + "(;".concat(index, ";)");
    };
    NumericNameResolver.prototype.getLabel = function (index) {
        return null;
    };
    return NumericNameResolver;
}());
exports.NumericNameResolver = NumericNameResolver;
var LabelMode;
(function (LabelMode) {
    LabelMode[LabelMode["Depth"] = 0] = "Depth";
    LabelMode[LabelMode["WhenUsed"] = 1] = "WhenUsed";
    LabelMode[LabelMode["Always"] = 2] = "Always";
})(LabelMode = exports.LabelMode || (exports.LabelMode = {}));
var WasmDisassembler = /** @class */ (function () {
    function WasmDisassembler() {
        this._skipTypes = true;
        this._exportMetadata = null;
        this._lines = [];
        this._offsets = [];
        this._buffer = "";
        this._indent = null;
        this._indentLevel = 0;
        this._addOffsets = false;
        this._done = false;
        this._currentPosition = 0;
        this._nameResolver = new DefaultNameResolver();
        this._labelMode = LabelMode.WhenUsed;
        this._functionBodyOffsets = [];
        this._currentFunctionBodyOffset = 0;
        this._currentSectionId = -1 /* SectionCode.Unknown */;
        this._logFirstInstruction = false;
        this._reset();
    }
    WasmDisassembler.prototype._reset = function () {
        this._types = [];
        this._funcIndex = 0;
        this._funcTypes = [];
        this._importCount = 0;
        this._globalCount = 0;
        this._memoryCount = 0;
        this._eventCount = 0;
        this._tableCount = 0;
        this._elementCount = 0;
        this._expression = [];
        this._backrefLabels = null;
        this._labelIndex = 0;
    };
    Object.defineProperty(WasmDisassembler.prototype, "addOffsets", {
        get: function () {
            return this._addOffsets;
        },
        set: function (value) {
            if (this._currentPosition)
                throw new Error("Cannot switch addOffsets during processing.");
            this._addOffsets = value;
        },
        enumerable: false,
        configurable: true
    });
    Object.defineProperty(WasmDisassembler.prototype, "skipTypes", {
        get: function () {
            return this._skipTypes;
        },
        set: function (skipTypes) {
            if (this._currentPosition)
                throw new Error("Cannot switch skipTypes during processing.");
            this._skipTypes = skipTypes;
        },
        enumerable: false,
        configurable: true
    });
    Object.defineProperty(WasmDisassembler.prototype, "labelMode", {
        get: function () {
            return this._labelMode;
        },
        set: function (value) {
            if (this._currentPosition)
                throw new Error("Cannot switch labelMode during processing.");
            this._labelMode = value;
        },
        enumerable: false,
        configurable: true
    });
    Object.defineProperty(WasmDisassembler.prototype, "exportMetadata", {
        get: function () {
            return this._exportMetadata;
        },
        set: function (exportMetadata) {
            if (this._currentPosition)
                throw new Error("Cannot switch exportMetadata during processing.");
            this._exportMetadata = exportMetadata;
        },
        enumerable: false,
        configurable: true
    });
    Object.defineProperty(WasmDisassembler.prototype, "nameResolver", {
        get: function () {
            return this._nameResolver;
        },
        set: function (resolver) {
            if (this._currentPosition)
                throw new Error("Cannot switch nameResolver during processing.");
            this._nameResolver = resolver;
        },
        enumerable: false,
        configurable: true
    });
    WasmDisassembler.prototype.appendBuffer = function (s) {
        this._buffer += s;
    };
    WasmDisassembler.prototype.newLine = function () {
        if (this.addOffsets)
            this._offsets.push(this._currentPosition);
        this._lines.push(this._buffer);
        this._buffer = "";
    };
    WasmDisassembler.prototype.logStartOfFunctionBodyOffset = function () {
        if (this.addOffsets) {
            this._currentFunctionBodyOffset = this._currentPosition;
        }
    };
    WasmDisassembler.prototype.logEndOfFunctionBodyOffset = function () {
        if (this.addOffsets) {
            this._functionBodyOffsets.push({
                start: this._currentFunctionBodyOffset,
                end: this._currentPosition,
            });
        }
    };
    WasmDisassembler.prototype.typeIndexToString = function (typeIndex) {
        if (typeIndex >= 0)
            return this._nameResolver.getTypeName(typeIndex, true);
        switch (typeIndex) {
            case -16 /* TypeKind.funcref */:
                return "func";
            case -17 /* TypeKind.externref */:
                return "extern";
            case -18 /* TypeKind.anyref */:
                return "any";
            case -19 /* TypeKind.eqref */:
                return "eq";
            case -20 /* TypeKind.i31ref */:
                return "i31";
            case -23 /* TypeKind.exnref */:
                return "exnref";
            case -21 /* TypeKind.structref */:
                return "struct";
            case -22 /* TypeKind.arrayref */:
                return "array";
            case -13 /* TypeKind.nullfuncref */:
                return "nofunc";
            case -14 /* TypeKind.nullexternref */:
                return "noextern";
            case -15 /* TypeKind.nullref */:
                return "none";
            case -12 /* TypeKind.nullexnref */:
                return "noexnref";
        }
    };
    WasmDisassembler.prototype.refTypeToString = function (typeIndex, nullable) {
        return this.typeToString(new WasmParser_js_1.RefType(nullable ? -29 /* TypeKind.ref_null */ : -28 /* TypeKind.ref */, typeIndex));
    };
    WasmDisassembler.prototype.typeToString = function (type) {
        switch (type.kind) {
            case -1 /* TypeKind.i32 */:
                return "i32";
            case -2 /* TypeKind.i64 */:
                return "i64";
            case -3 /* TypeKind.f32 */:
                return "f32";
            case -4 /* TypeKind.f64 */:
                return "f64";
            case -5 /* TypeKind.v128 */:
                return "v128";
            case -8 /* TypeKind.i8 */:
                return "i8";
            case -9 /* TypeKind.i16 */:
                return "i16";
            case -16 /* TypeKind.funcref */:
                return "funcref";
            case -17 /* TypeKind.externref */:
                return "externref";
            case -23 /* TypeKind.exnref */:
                return "exnref";
            case -18 /* TypeKind.anyref */:
                return "anyref";
            case -19 /* TypeKind.eqref */:
                return "eqref";
            case -20 /* TypeKind.i31ref */:
                return "i31ref";
            case -21 /* TypeKind.structref */:
                return "structref";
            case -22 /* TypeKind.arrayref */:
                return "arrayref";
            case -13 /* TypeKind.nullfuncref */:
                return "nullfuncref";
            case -14 /* TypeKind.nullexternref */:
                return "nullexternref";
            case -12 /* TypeKind.nullexnref */:
                return "nullexnref";
            case -15 /* TypeKind.nullref */:
                return "nullref";
            case -28 /* TypeKind.ref */:
                return "(ref ".concat(this.typeIndexToString(type.ref_index), ")");
            case -29 /* TypeKind.ref_null */:
                return "(ref null ".concat(this.typeIndexToString(type.ref_index), ")");
            default:
                throw new Error("Unexpected type ".concat(JSON.stringify(type)));
        }
    };
    WasmDisassembler.prototype.maybeMut = function (type, mutability) {
        return mutability ? "(mut ".concat(type, ")") : type;
    };
    WasmDisassembler.prototype.globalTypeToString = function (type) {
        var typeStr = this.typeToString(type.contentType);
        return this.maybeMut(typeStr, !!type.mutability);
    };
    WasmDisassembler.prototype.printFuncType = function (typeIndex) {
        var type = this._types[typeIndex];
        if (type.params.length > 0) {
            this.appendBuffer(" (param");
            for (var i = 0; i < type.params.length; i++) {
                this.appendBuffer(" ");
                this.appendBuffer(this.typeToString(type.params[i]));
            }
            this.appendBuffer(")");
        }
        if (type.returns.length > 0) {
            this.appendBuffer(" (result");
            for (var i = 0; i < type.returns.length; i++) {
                this.appendBuffer(" ");
                this.appendBuffer(this.typeToString(type.returns[i]));
            }
            this.appendBuffer(")");
        }
    };
    WasmDisassembler.prototype.printStructType = function (typeIndex) {
        var type = this._types[typeIndex];
        if (type.fields.length === 0)
            return;
        for (var i = 0; i < type.fields.length; i++) {
            var fieldType = this.maybeMut(this.typeToString(type.fields[i]), type.mutabilities[i]);
            var fieldName = this._nameResolver.getFieldName(typeIndex, i, false);
            this.appendBuffer(" (field ".concat(fieldName, " ").concat(fieldType, ")"));
        }
    };
    WasmDisassembler.prototype.printArrayType = function (typeIndex) {
        var type = this._types[typeIndex];
        this.appendBuffer(" (field ");
        this.appendBuffer(this.maybeMut(this.typeToString(type.elementType), type.mutability));
    };
    WasmDisassembler.prototype.printBlockType = function (type) {
        if (type.kind === -64 /* TypeKind.empty_block_type */) {
            return;
        }
        if (type.kind === 0 /* TypeKind.unspecified */) {
            if (this._types[type.index].form == -32 /* TypeKind.func */) {
                return this.printFuncType(type.index);
            }
            else {
                // Encoding error.
                this.appendBuffer(" (type ".concat(type.index, ")"));
                return;
            }
        }
        this.appendBuffer(" (result ");
        this.appendBuffer(this.typeToString(type));
        this.appendBuffer(")");
    };
    WasmDisassembler.prototype.printString = function (b) {
        this.appendBuffer('"');
        for (var i = 0; i < b.length; i++) {
            var byte = b[i];
            if (byte < 0x20 ||
                byte >= 0x7f ||
                byte == /* " */ 0x22 ||
                byte == /* \ */ 0x5c) {
                this.appendBuffer("\\" + (byte >> 4).toString(16) + (byte & 15).toString(16));
            }
            else {
                this.appendBuffer(String.fromCharCode(byte));
            }
        }
        this.appendBuffer('"');
    };
    WasmDisassembler.prototype.printExpression = function (expression) {
        for (var _i = 0, expression_1 = expression; _i < expression_1.length; _i++) {
            var operator = expression_1[_i];
            this.appendBuffer("(");
            this.printOperator(operator);
            this.appendBuffer(")");
        }
    };
    // extraDepthOffset is used by "delegate" instructions.
    WasmDisassembler.prototype.useLabel = function (depth, extraDepthOffset) {
        if (extraDepthOffset === void 0) { extraDepthOffset = 0; }
        if (!this._backrefLabels) {
            return "" + depth;
        }
        var i = this._backrefLabels.length - depth - 1 - extraDepthOffset;
        if (i < 0) {
            return "" + depth;
        }
        var backrefLabel = this._backrefLabels[i];
        if (!backrefLabel.useLabel) {
            backrefLabel.useLabel = true;
            backrefLabel.label = this._nameResolver.getLabel(this._labelIndex);
            var line = this._lines[backrefLabel.line];
            this._lines[backrefLabel.line] =
                line.substring(0, backrefLabel.position) +
                    " " +
                    backrefLabel.label +
                    line.substring(backrefLabel.position);
            this._labelIndex++;
        }
        return backrefLabel.label || "" + depth;
    };
    WasmDisassembler.prototype.printPageSize = function (memoryType) {
        if (memoryType.pageSizeLog2 !== undefined) {
            this.appendBuffer(" (pagesize ".concat(Math.pow(2, memoryType.pageSizeLog2), ")"));
        }
    };
    WasmDisassembler.prototype.printMemoryIndex = function (memoryIndex) {
        if (memoryIndex) {
            var memoryName = this._nameResolver.getMemoryName(memoryIndex, true);
            this.appendBuffer(" ".concat(memoryName));
        }
    };
    WasmDisassembler.prototype.printOperator = function (operator) {
        var code = operator.code;
        this.appendBuffer(WasmParser_js_1.OperatorCodeNames[code]);
        switch (code) {
            case 2 /* OperatorCode.block */:
            case 3 /* OperatorCode.loop */:
            case 4 /* OperatorCode.if */:
            case 6 /* OperatorCode.try */:
            case 31 /* OperatorCode.try_table */:
                if (this._labelMode !== LabelMode.Depth) {
                    var backrefLabel_1 = {
                        line: this._lines.length,
                        position: this._buffer.length,
                        useLabel: false,
                        label: null,
                    };
                    if (this._labelMode === LabelMode.Always) {
                        backrefLabel_1.useLabel = true;
                        backrefLabel_1.label = this._nameResolver.getLabel(this._labelIndex++);
                        if (backrefLabel_1.label) {
                            this.appendBuffer(" ");
                            this.appendBuffer(backrefLabel_1.label);
                        }
                    }
                    this._backrefLabels.push(backrefLabel_1);
                }
                this.printBlockType(operator.blockType);
                if (operator.tryTable) {
                    for (var i = 0; i < operator.tryTable.length; i++) {
                        this.appendBuffer(" (");
                        switch (operator.tryTable[i].kind) {
                            case WasmParser_js_1.CatchHandlerKind.Catch:
                                this.appendBuffer("catch ");
                                break;
                            case WasmParser_js_1.CatchHandlerKind.CatchRef:
                                this.appendBuffer("catch_ref ");
                                break;
                            case WasmParser_js_1.CatchHandlerKind.CatchAll:
                                this.appendBuffer("catch_all ");
                                break;
                            case WasmParser_js_1.CatchHandlerKind.CatchAllRef:
                                this.appendBuffer("catch_all_ref ");
                                break;
                        }
                        if (operator.tryTable[i].tagIndex != null) {
                            var tagName = this._nameResolver.getTagName(operator.tryTable[i].tagIndex, true);
                            this.appendBuffer("".concat(tagName, " "));
                        }
                        this.appendBuffer(this.useLabel(operator.tryTable[i].depth + 1));
                        this.appendBuffer(")");
                    }
                }
                break;
            case 11 /* OperatorCode.end */:
                if (this._labelMode === LabelMode.Depth) {
                    break;
                }
                var backrefLabel = this._backrefLabels.pop();
                if (backrefLabel.label) {
                    this.appendBuffer(" ");
                    this.appendBuffer(backrefLabel.label);
                }
                break;
            case 12 /* OperatorCode.br */:
            case 13 /* OperatorCode.br_if */:
            case 213 /* OperatorCode.br_on_null */:
            case 214 /* OperatorCode.br_on_non_null */:
                this.appendBuffer(" ");
                this.appendBuffer(this.useLabel(operator.brDepth));
                break;
            case 64280 /* OperatorCode.br_on_cast */:
            case 64281 /* OperatorCode.br_on_cast_fail */:
                this.appendBuffer(" flags=" + operator.literal);
                this.appendBuffer(" ");
                this.appendBuffer(this.typeIndexToString(operator.srcType));
                this.appendBuffer(" ");
                this.appendBuffer(this.typeIndexToString(operator.refType));
                this.appendBuffer(" ");
                this.appendBuffer(this.useLabel(operator.brDepth));
                break;
            case 14 /* OperatorCode.br_table */:
                for (var i = 0; i < operator.brTable.length; i++) {
                    this.appendBuffer(" ");
                    this.appendBuffer(this.useLabel(operator.brTable[i]));
                }
                break;
            case 9 /* OperatorCode.rethrow */:
                this.appendBuffer(" ");
                this.appendBuffer(this.useLabel(operator.relativeDepth));
                break;
            case 24 /* OperatorCode.delegate */:
                this.appendBuffer(" ");
                this.appendBuffer(this.useLabel(operator.relativeDepth, 1));
                break;
            case 7 /* OperatorCode.catch */:
            case 8 /* OperatorCode.throw */:
                var tagName = this._nameResolver.getTagName(operator.tagIndex, true);
                this.appendBuffer(" ".concat(tagName));
                break;
            case 208 /* OperatorCode.ref_null */:
                this.appendBuffer(" ");
                this.appendBuffer(this.typeIndexToString(operator.refType));
                break;
            case 16 /* OperatorCode.call */:
            case 18 /* OperatorCode.return_call */:
            case 210 /* OperatorCode.ref_func */:
                var funcName = this._nameResolver.getFunctionName(operator.funcIndex, operator.funcIndex < this._importCount, true);
                this.appendBuffer(" ".concat(funcName));
                break;
            case 17 /* OperatorCode.call_indirect */:
            case 19 /* OperatorCode.return_call_indirect */:
                // Table index might be omitted and defaults to 0.
                if (operator.tableIndex !== 0) {
                    var tableName = this._nameResolver.getTableName(operator.tableIndex, true);
                    this.appendBuffer(" ".concat(tableName));
                }
                this.printFuncType(operator.typeIndex);
                break;
            case 28 /* OperatorCode.select_with_type */: {
                var selectType = this.typeToString(operator.selectType);
                this.appendBuffer(" (result ".concat(selectType, ")"));
                break;
            }
            case 32 /* OperatorCode.local_get */:
            case 33 /* OperatorCode.local_set */:
            case 34 /* OperatorCode.local_tee */:
                var paramName = this._nameResolver.getVariableName(this._funcIndex, operator.localIndex, true);
                this.appendBuffer(" ".concat(paramName));
                break;
            case 35 /* OperatorCode.global_get */:
            case 36 /* OperatorCode.global_set */:
                var globalName = this._nameResolver.getGlobalName(operator.globalIndex, true);
                this.appendBuffer(" ".concat(globalName));
                break;
            case 40 /* OperatorCode.i32_load */:
            case 41 /* OperatorCode.i64_load */:
            case 42 /* OperatorCode.f32_load */:
            case 43 /* OperatorCode.f64_load */:
            case 44 /* OperatorCode.i32_load8_s */:
            case 45 /* OperatorCode.i32_load8_u */:
            case 46 /* OperatorCode.i32_load16_s */:
            case 47 /* OperatorCode.i32_load16_u */:
            case 48 /* OperatorCode.i64_load8_s */:
            case 49 /* OperatorCode.i64_load8_u */:
            case 50 /* OperatorCode.i64_load16_s */:
            case 51 /* OperatorCode.i64_load16_u */:
            case 52 /* OperatorCode.i64_load32_s */:
            case 53 /* OperatorCode.i64_load32_u */:
            case 54 /* OperatorCode.i32_store */:
            case 55 /* OperatorCode.i64_store */:
            case 56 /* OperatorCode.f32_store */:
            case 57 /* OperatorCode.f64_store */:
            case 58 /* OperatorCode.i32_store8 */:
            case 59 /* OperatorCode.i32_store16 */:
            case 60 /* OperatorCode.i64_store8 */:
            case 61 /* OperatorCode.i64_store16 */:
            case 62 /* OperatorCode.i64_store32 */:
            case 65024 /* OperatorCode.memory_atomic_notify */:
            case 65025 /* OperatorCode.memory_atomic_wait32 */:
            case 65026 /* OperatorCode.memory_atomic_wait64 */:
            case 65040 /* OperatorCode.i32_atomic_load */:
            case 65041 /* OperatorCode.i64_atomic_load */:
            case 65042 /* OperatorCode.i32_atomic_load8_u */:
            case 65043 /* OperatorCode.i32_atomic_load16_u */:
            case 65044 /* OperatorCode.i64_atomic_load8_u */:
            case 65045 /* OperatorCode.i64_atomic_load16_u */:
            case 65046 /* OperatorCode.i64_atomic_load32_u */:
            case 65047 /* OperatorCode.i32_atomic_store */:
            case 65048 /* OperatorCode.i64_atomic_store */:
            case 65049 /* OperatorCode.i32_atomic_store8 */:
            case 65050 /* OperatorCode.i32_atomic_store16 */:
            case 65051 /* OperatorCode.i64_atomic_store8 */:
            case 65052 /* OperatorCode.i64_atomic_store16 */:
            case 65053 /* OperatorCode.i64_atomic_store32 */:
            case 65054 /* OperatorCode.i32_atomic_rmw_add */:
            case 65055 /* OperatorCode.i64_atomic_rmw_add */:
            case 65056 /* OperatorCode.i32_atomic_rmw8_add_u */:
            case 65057 /* OperatorCode.i32_atomic_rmw16_add_u */:
            case 65058 /* OperatorCode.i64_atomic_rmw8_add_u */:
            case 65059 /* OperatorCode.i64_atomic_rmw16_add_u */:
            case 65060 /* OperatorCode.i64_atomic_rmw32_add_u */:
            case 65061 /* OperatorCode.i32_atomic_rmw_sub */:
            case 65062 /* OperatorCode.i64_atomic_rmw_sub */:
            case 65063 /* OperatorCode.i32_atomic_rmw8_sub_u */:
            case 65064 /* OperatorCode.i32_atomic_rmw16_sub_u */:
            case 65065 /* OperatorCode.i64_atomic_rmw8_sub_u */:
            case 65066 /* OperatorCode.i64_atomic_rmw16_sub_u */:
            case 65067 /* OperatorCode.i64_atomic_rmw32_sub_u */:
            case 65068 /* OperatorCode.i32_atomic_rmw_and */:
            case 65069 /* OperatorCode.i64_atomic_rmw_and */:
            case 65070 /* OperatorCode.i32_atomic_rmw8_and_u */:
            case 65071 /* OperatorCode.i32_atomic_rmw16_and_u */:
            case 65072 /* OperatorCode.i64_atomic_rmw8_and_u */:
            case 65073 /* OperatorCode.i64_atomic_rmw16_and_u */:
            case 65074 /* OperatorCode.i64_atomic_rmw32_and_u */:
            case 65075 /* OperatorCode.i32_atomic_rmw_or */:
            case 65076 /* OperatorCode.i64_atomic_rmw_or */:
            case 65077 /* OperatorCode.i32_atomic_rmw8_or_u */:
            case 65078 /* OperatorCode.i32_atomic_rmw16_or_u */:
            case 65079 /* OperatorCode.i64_atomic_rmw8_or_u */:
            case 65080 /* OperatorCode.i64_atomic_rmw16_or_u */:
            case 65081 /* OperatorCode.i64_atomic_rmw32_or_u */:
            case 65082 /* OperatorCode.i32_atomic_rmw_xor */:
            case 65083 /* OperatorCode.i64_atomic_rmw_xor */:
            case 65084 /* OperatorCode.i32_atomic_rmw8_xor_u */:
            case 65085 /* OperatorCode.i32_atomic_rmw16_xor_u */:
            case 65086 /* OperatorCode.i64_atomic_rmw8_xor_u */:
            case 65087 /* OperatorCode.i64_atomic_rmw16_xor_u */:
            case 65088 /* OperatorCode.i64_atomic_rmw32_xor_u */:
            case 65089 /* OperatorCode.i32_atomic_rmw_xchg */:
            case 65090 /* OperatorCode.i64_atomic_rmw_xchg */:
            case 65091 /* OperatorCode.i32_atomic_rmw8_xchg_u */:
            case 65092 /* OperatorCode.i32_atomic_rmw16_xchg_u */:
            case 65093 /* OperatorCode.i64_atomic_rmw8_xchg_u */:
            case 65094 /* OperatorCode.i64_atomic_rmw16_xchg_u */:
            case 65095 /* OperatorCode.i64_atomic_rmw32_xchg_u */:
            case 65096 /* OperatorCode.i32_atomic_rmw_cmpxchg */:
            case 65097 /* OperatorCode.i64_atomic_rmw_cmpxchg */:
            case 65098 /* OperatorCode.i32_atomic_rmw8_cmpxchg_u */:
            case 65099 /* OperatorCode.i32_atomic_rmw16_cmpxchg_u */:
            case 65100 /* OperatorCode.i64_atomic_rmw8_cmpxchg_u */:
            case 65101 /* OperatorCode.i64_atomic_rmw16_cmpxchg_u */:
            case 65102 /* OperatorCode.i64_atomic_rmw32_cmpxchg_u */:
            case 1036288 /* OperatorCode.v128_load */:
            case 1036289 /* OperatorCode.v128_load8x8_s */:
            case 1036290 /* OperatorCode.v128_load8x8_u */:
            case 1036291 /* OperatorCode.v128_load16x4_s */:
            case 1036292 /* OperatorCode.v128_load16x4_u */:
            case 1036293 /* OperatorCode.v128_load32x2_s */:
            case 1036294 /* OperatorCode.v128_load32x2_u */:
            case 1036295 /* OperatorCode.v128_load8_splat */:
            case 1036296 /* OperatorCode.v128_load16_splat */:
            case 1036297 /* OperatorCode.v128_load32_splat */:
            case 1036298 /* OperatorCode.v128_load64_splat */:
            case 1036299 /* OperatorCode.v128_store */:
            case 1036380 /* OperatorCode.v128_load32_zero */:
            case 1036381 /* OperatorCode.v128_load64_zero */:
                this.printMemoryIndex(operator.memoryAddress.memoryIndex);
                var memoryAddress = memoryAddressToString(operator.memoryAddress, operator.code);
                if (memoryAddress !== null) {
                    this.appendBuffer(" ");
                    this.appendBuffer(memoryAddress);
                }
                break;
            case 63 /* OperatorCode.memory_size */:
            case 64 /* OperatorCode.memory_grow */:
            case 64523 /* OperatorCode.memory_fill */:
                this.printMemoryIndex(operator.memoryIndex);
                break;
            case 64522 /* OperatorCode.memory_copy */:
                if (operator.destinationIndex || operator.memoryIndex) {
                    var dstName = this._nameResolver.getMemoryName(operator.destinationIndex, true);
                    var srcName = this._nameResolver.getMemoryName(operator.memoryIndex, true);
                    this.appendBuffer(" ".concat(dstName, " ").concat(srcName));
                }
                break;
            case 65 /* OperatorCode.i32_const */:
                this.appendBuffer(" ".concat(operator.literal.toString()));
                break;
            case 66 /* OperatorCode.i64_const */:
                this.appendBuffer(" ".concat(operator.literal.toString()));
                break;
            case 67 /* OperatorCode.f32_const */:
                this.appendBuffer(" ".concat(formatFloat32(operator.literal, operator.literalBits)));
                break;
            case 68 /* OperatorCode.f64_const */:
                this.appendBuffer(" ".concat(formatFloat64(operator.literal)));
                break;
            case 1036300 /* OperatorCode.v128_const */:
                this.appendBuffer(" i32x4 ".concat(formatI32Array(operator.literal, 4)));
                break;
            case 1036301 /* OperatorCode.i8x16_shuffle */:
                this.appendBuffer(" ".concat(formatI8Array(operator.lines, 16)));
                break;
            case 1036309 /* OperatorCode.i8x16_extract_lane_s */:
            case 1036310 /* OperatorCode.i8x16_extract_lane_u */:
            case 1036311 /* OperatorCode.i8x16_replace_lane */:
            case 1036312 /* OperatorCode.i16x8_extract_lane_s */:
            case 1036313 /* OperatorCode.i16x8_extract_lane_u */:
            case 1036314 /* OperatorCode.i16x8_replace_lane */:
            case 1036315 /* OperatorCode.i32x4_extract_lane */:
            case 1036316 /* OperatorCode.i32x4_replace_lane */:
            case 1036319 /* OperatorCode.f32x4_extract_lane */:
            case 1036320 /* OperatorCode.f32x4_replace_lane */:
            case 1036317 /* OperatorCode.i64x2_extract_lane */:
            case 1036318 /* OperatorCode.i64x2_replace_lane */:
            case 1036321 /* OperatorCode.f64x2_extract_lane */:
            case 1036322 /* OperatorCode.f64x2_replace_lane */:
                this.appendBuffer(" ".concat(operator.lineIndex));
                break;
            case 1036372 /* OperatorCode.v128_load8_lane */:
            case 1036373 /* OperatorCode.v128_load16_lane */:
            case 1036374 /* OperatorCode.v128_load32_lane */:
            case 1036375 /* OperatorCode.v128_load64_lane */:
            case 1036376 /* OperatorCode.v128_store8_lane */:
            case 1036377 /* OperatorCode.v128_store16_lane */:
            case 1036378 /* OperatorCode.v128_store32_lane */:
            case 1036379 /* OperatorCode.v128_store64_lane */:
                this.printMemoryIndex(operator.memoryAddress.memoryIndex);
                var memoryAddress = memoryAddressToString(operator.memoryAddress, operator.code);
                if (memoryAddress !== null) {
                    this.appendBuffer(" ");
                    this.appendBuffer(memoryAddress);
                }
                this.appendBuffer(" ".concat(operator.lineIndex));
                break;
            case 64520 /* OperatorCode.memory_init */:
                this.printMemoryIndex(operator.memoryIndex);
                this.appendBuffer(" ".concat(operator.segmentIndex));
                break;
            case 64521 /* OperatorCode.data_drop */:
                this.appendBuffer(" ".concat(operator.segmentIndex));
                break;
            case 64525 /* OperatorCode.elem_drop */:
                var elementName = this._nameResolver.getElementName(operator.segmentIndex, true);
                this.appendBuffer(" ".concat(elementName));
                break;
            case 38 /* OperatorCode.table_set */:
            case 37 /* OperatorCode.table_get */:
            case 64529 /* OperatorCode.table_fill */:
            case 64528 /* OperatorCode.table_size */:
            case 64527 /* OperatorCode.table_grow */: {
                var tableName = this._nameResolver.getTableName(operator.tableIndex, true);
                this.appendBuffer(" ".concat(tableName));
                break;
            }
            case 64526 /* OperatorCode.table_copy */: {
                // Table index might be omitted and defaults to 0.
                if (operator.tableIndex !== 0 || operator.destinationIndex !== 0) {
                    var tableName = this._nameResolver.getTableName(operator.tableIndex, true);
                    var destinationName = this._nameResolver.getTableName(operator.destinationIndex, true);
                    this.appendBuffer(" ".concat(destinationName, " ").concat(tableName));
                }
                break;
            }
            case 64524 /* OperatorCode.table_init */: {
                // Table index might be omitted and defaults to 0.
                if (operator.tableIndex !== 0) {
                    var tableName = this._nameResolver.getTableName(operator.tableIndex, true);
                    this.appendBuffer(" ".concat(tableName));
                }
                var elementName_1 = this._nameResolver.getElementName(operator.segmentIndex, true);
                this.appendBuffer(" ".concat(elementName_1));
                break;
            }
            case 64258 /* OperatorCode.struct_get */:
            case 64259 /* OperatorCode.struct_get_s */:
            case 64260 /* OperatorCode.struct_get_u */:
            case 64261 /* OperatorCode.struct_set */: {
                var refType = this.typeIndexToString(operator.refType);
                var fieldName = this._nameResolver.getFieldName(operator.refType, operator.fieldIndex, true);
                this.appendBuffer(" ".concat(refType, " ").concat(fieldName));
                break;
            }
            case 64278 /* OperatorCode.ref_cast */:
            case 64276 /* OperatorCode.ref_test */: {
                var refType = this.refTypeToString(operator.refType, false);
                this.appendBuffer(" ".concat(refType));
                break;
            }
            case 64279 /* OperatorCode.ref_cast_null */:
            case 64277 /* OperatorCode.ref_test_null */: {
                var refType = this.refTypeToString(operator.refType, true);
                this.appendBuffer(" ".concat(refType));
                break;
            }
            case 64257 /* OperatorCode.struct_new_default */:
            case 64256 /* OperatorCode.struct_new */:
            case 64263 /* OperatorCode.array_new_default */:
            case 64262 /* OperatorCode.array_new */:
            case 64267 /* OperatorCode.array_get */:
            case 64268 /* OperatorCode.array_get_s */:
            case 64269 /* OperatorCode.array_get_u */:
            case 64270 /* OperatorCode.array_set */: {
                var refType = this.typeIndexToString(operator.refType);
                this.appendBuffer(" ".concat(refType));
                break;
            }
            case 64272 /* OperatorCode.array_fill */: {
                var dstType = this.typeIndexToString(operator.refType);
                this.appendBuffer(" ".concat(dstType));
                break;
            }
            case 64273 /* OperatorCode.array_copy */: {
                var dstType = this.typeIndexToString(operator.refType);
                var srcType = this.typeIndexToString(operator.srcType);
                this.appendBuffer(" ".concat(dstType, " ").concat(srcType));
                break;
            }
            case 64264 /* OperatorCode.array_new_fixed */: {
                var refType = this.typeIndexToString(operator.refType);
                var length_1 = operator.len;
                this.appendBuffer(" ".concat(refType, " ").concat(length_1));
                break;
            }
        }
    };
    WasmDisassembler.prototype.printImportSource = function (info) {
        this.printString(info.module);
        this.appendBuffer(" ");
        this.printString(info.field);
    };
    WasmDisassembler.prototype.increaseIndent = function () {
        this._indent += IndentIncrement;
        this._indentLevel++;
    };
    WasmDisassembler.prototype.decreaseIndent = function () {
        this._indent = this._indent.slice(0, -IndentIncrement.length);
        this._indentLevel--;
    };
    WasmDisassembler.prototype.disassemble = function (reader) {
        var _this = this;
        var done = this.disassembleChunk(reader);
        if (!done)
            return null;
        var lines = this._lines;
        if (this._addOffsets) {
            lines = lines.map(function (line, index) {
                var position = formatHex(_this._offsets[index], 4);
                return line + " ;; @" + position;
            });
        }
        lines.push(""); // we need '\n' after last line
        var result = lines.join("\n");
        this._lines.length = 0;
        this._offsets.length = 0;
        this._functionBodyOffsets.length = 0;
        return result;
    };
    WasmDisassembler.prototype.getResult = function () {
        var linesReady = this._lines.length;
        if (this._backrefLabels && this._labelMode === LabelMode.WhenUsed) {
            this._backrefLabels.some(function (backrefLabel) {
                if (backrefLabel.useLabel)
                    return false;
                linesReady = backrefLabel.line;
                return true;
            });
        }
        if (linesReady === 0) {
            return {
                lines: [],
                offsets: this._addOffsets ? [] : undefined,
                done: this._done,
                functionBodyOffsets: this._addOffsets ? [] : undefined,
            };
        }
        if (linesReady === this._lines.length) {
            var result_1 = {
                lines: this._lines,
                offsets: this._addOffsets ? this._offsets : undefined,
                done: this._done,
                functionBodyOffsets: this._addOffsets
                    ? this._functionBodyOffsets
                    : undefined,
            };
            this._lines = [];
            if (this._addOffsets) {
                this._offsets = [];
                this._functionBodyOffsets = [];
            }
            return result_1;
        }
        var result = {
            lines: this._lines.splice(0, linesReady),
            offsets: this._addOffsets
                ? this._offsets.splice(0, linesReady)
                : undefined,
            done: false,
            functionBodyOffsets: this._addOffsets
                ? this._functionBodyOffsets
                : undefined,
        };
        if (this._backrefLabels) {
            this._backrefLabels.forEach(function (backrefLabel) {
                backrefLabel.line -= linesReady;
            });
        }
        return result;
    };
    WasmDisassembler.prototype.disassembleChunk = function (reader, offsetInModule) {
        var _this = this;
        if (offsetInModule === void 0) { offsetInModule = 0; }
        if (this._done)
            throw new Error("Invalid state: disassembly process was already finished.");
        while (true) {
            this._currentPosition = reader.position + offsetInModule;
            if (!reader.read())
                return false;
            switch (reader.state) {
                case 2 /* BinaryReaderState.END_WASM */:
                    this.appendBuffer(")");
                    this.newLine();
                    this._reset();
                    if (!reader.hasMoreBytes()) {
                        this._done = true;
                        return true;
                    }
                    break;
                case -1 /* BinaryReaderState.ERROR */:
                    throw reader.error;
                case 1 /* BinaryReaderState.BEGIN_WASM */:
                    this.appendBuffer("(module");
                    this.newLine();
                    break;
                case 4 /* BinaryReaderState.END_SECTION */:
                    this._currentSectionId = -1 /* SectionCode.Unknown */;
                    break;
                case 3 /* BinaryReaderState.BEGIN_SECTION */:
                    var sectionInfo = reader.result;
                    switch (sectionInfo.id) {
                        case 1 /* SectionCode.Type */:
                        case 2 /* SectionCode.Import */:
                        case 7 /* SectionCode.Export */:
                        case 6 /* SectionCode.Global */:
                        case 3 /* SectionCode.Function */:
                        case 8 /* SectionCode.Start */:
                        case 10 /* SectionCode.Code */:
                        case 5 /* SectionCode.Memory */:
                        case 11 /* SectionCode.Data */:
                        case 4 /* SectionCode.Table */:
                        case 9 /* SectionCode.Element */:
                        case 13 /* SectionCode.Tag */:
                            this._currentSectionId = sectionInfo.id;
                            this._indent = "  ";
                            this._indentLevel = 0;
                            break; // reading known section;
                        default:
                            reader.skipSection();
                            break;
                    }
                    break;
                case 15 /* BinaryReaderState.MEMORY_SECTION_ENTRY */:
                    var memoryInfo = reader.result;
                    var memoryIndex = this._memoryCount++;
                    var memoryName = this._nameResolver.getMemoryName(memoryIndex, false);
                    this.appendBuffer("  (memory ".concat(memoryName));
                    if (this._exportMetadata !== null) {
                        for (var _i = 0, _a = this._exportMetadata.getMemoryExportNames(memoryIndex); _i < _a.length; _i++) {
                            var exportName = _a[_i];
                            this.appendBuffer(" (export ".concat(JSON.stringify(exportName), ")"));
                        }
                    }
                    this.appendBuffer(" ".concat(limitsToString(memoryInfo.limits, memoryInfo.is64)));
                    if (memoryInfo.shared) {
                        this.appendBuffer(" shared");
                    }
                    this.printPageSize(memoryInfo);
                    this.appendBuffer(")");
                    this.newLine();
                    break;
                case 23 /* BinaryReaderState.TAG_SECTION_ENTRY */:
                    var tagInfo = reader.result;
                    var tagIndex = this._eventCount++;
                    var tagName = this._nameResolver.getTagName(tagIndex, false);
                    this.appendBuffer("  (tag ".concat(tagName));
                    if (this._exportMetadata !== null) {
                        for (var _b = 0, _c = this._exportMetadata.getTagExportNames(tagIndex); _b < _c.length; _b++) {
                            var exportName = _c[_b];
                            this.appendBuffer(" (export ".concat(JSON.stringify(exportName), ")"));
                        }
                    }
                    this.printFuncType(tagInfo.typeIndex);
                    this.appendBuffer(")");
                    this.newLine();
                    break;
                case 14 /* BinaryReaderState.TABLE_SECTION_ENTRY */:
                    var tableInfo = reader.result;
                    var tableIndex = this._tableCount++;
                    var tableName = this._nameResolver.getTableName(tableIndex, false);
                    this.appendBuffer("  (table ".concat(tableName));
                    if (this._exportMetadata !== null) {
                        for (var _d = 0, _e = this._exportMetadata.getTableExportNames(tableIndex); _d < _e.length; _d++) {
                            var exportName = _e[_d];
                            this.appendBuffer(" (export ".concat(JSON.stringify(exportName), ")"));
                        }
                    }
                    this.appendBuffer(" ".concat(limitsToString(tableInfo.limits, tableInfo.is64), " ").concat(this.typeToString(tableInfo.elementType), ")"));
                    this.newLine();
                    break;
                case 17 /* BinaryReaderState.EXPORT_SECTION_ENTRY */:
                    // Skip printing exports here when we have export metadata
                    // which we can use to print export information inline.
                    if (this._exportMetadata === null) {
                        var exportInfo = reader.result;
                        this.appendBuffer("  (export ");
                        this.printString(exportInfo.field);
                        this.appendBuffer(" ");
                        switch (exportInfo.kind) {
                            case 0 /* ExternalKind.Function */:
                                var funcName = this._nameResolver.getFunctionName(exportInfo.index, exportInfo.index < this._importCount, true);
                                this.appendBuffer("(func ".concat(funcName, ")"));
                                break;
                            case 1 /* ExternalKind.Table */:
                                var tableName = this._nameResolver.getTableName(exportInfo.index, true);
                                this.appendBuffer("(table ".concat(tableName, ")"));
                                break;
                            case 2 /* ExternalKind.Memory */:
                                var memoryName = this._nameResolver.getMemoryName(exportInfo.index, true);
                                this.appendBuffer("(memory ".concat(memoryName, ")"));
                                break;
                            case 3 /* ExternalKind.Global */:
                                var globalName = this._nameResolver.getGlobalName(exportInfo.index, true);
                                this.appendBuffer("(global ".concat(globalName, ")"));
                                break;
                            case 4 /* ExternalKind.Tag */:
                                var tagName = this._nameResolver.getTagName(exportInfo.index, true);
                                this.appendBuffer("(tag ".concat(tagName, ")"));
                                break;
                            default:
                                throw new Error("Unsupported export ".concat(exportInfo.kind));
                        }
                        this.appendBuffer(")");
                        this.newLine();
                    }
                    break;
                case 12 /* BinaryReaderState.IMPORT_SECTION_ENTRY */:
                    var importInfo = reader.result;
                    switch (importInfo.kind) {
                        case 0 /* ExternalKind.Function */:
                            this._importCount++;
                            var funcIndex = this._funcIndex++;
                            var funcName = this._nameResolver.getFunctionName(funcIndex, true, false);
                            this.appendBuffer("  (func ".concat(funcName));
                            if (this._exportMetadata !== null) {
                                for (var _f = 0, _g = this._exportMetadata.getFunctionExportNames(funcIndex); _f < _g.length; _f++) {
                                    var exportName = _g[_f];
                                    this.appendBuffer(" (export ".concat(JSON.stringify(exportName), ")"));
                                }
                            }
                            this.appendBuffer(" (import ");
                            this.printImportSource(importInfo);
                            this.appendBuffer(")");
                            this.printFuncType(importInfo.funcTypeIndex);
                            this.appendBuffer(")");
                            break;
                        case 3 /* ExternalKind.Global */:
                            var globalImportInfo = importInfo.type;
                            var globalIndex = this._globalCount++;
                            var globalName = this._nameResolver.getGlobalName(globalIndex, false);
                            this.appendBuffer("  (global ".concat(globalName));
                            if (this._exportMetadata !== null) {
                                for (var _h = 0, _j = this._exportMetadata.getGlobalExportNames(globalIndex); _h < _j.length; _h++) {
                                    var exportName = _j[_h];
                                    this.appendBuffer(" (export ".concat(JSON.stringify(exportName), ")"));
                                }
                            }
                            this.appendBuffer(" (import ");
                            this.printImportSource(importInfo);
                            this.appendBuffer(") ".concat(this.globalTypeToString(globalImportInfo), ")"));
                            break;
                        case 2 /* ExternalKind.Memory */:
                            var memoryImportInfo = importInfo.type;
                            var memoryIndex = this._memoryCount++;
                            var memoryName = this._nameResolver.getMemoryName(memoryIndex, false);
                            this.appendBuffer("  (memory ".concat(memoryName));
                            if (this._exportMetadata !== null) {
                                for (var _k = 0, _l = this._exportMetadata.getMemoryExportNames(memoryIndex); _k < _l.length; _k++) {
                                    var exportName = _l[_k];
                                    this.appendBuffer(" (export ".concat(JSON.stringify(exportName), ")"));
                                }
                            }
                            this.appendBuffer(" (import ");
                            this.printImportSource(importInfo);
                            this.appendBuffer(") ".concat(limitsToString(memoryImportInfo.limits, memoryImportInfo.is64)));
                            if (memoryImportInfo.shared) {
                                this.appendBuffer(" shared");
                            }
                            this.printPageSize(memoryImportInfo);
                            this.appendBuffer(")");
                            break;
                        case 1 /* ExternalKind.Table */:
                            var tableImportInfo = importInfo.type;
                            var tableIndex = this._tableCount++;
                            var tableName = this._nameResolver.getTableName(tableIndex, false);
                            this.appendBuffer("  (table ".concat(tableName));
                            if (this._exportMetadata !== null) {
                                for (var _m = 0, _o = this._exportMetadata.getTableExportNames(tableIndex); _m < _o.length; _m++) {
                                    var exportName = _o[_m];
                                    this.appendBuffer(" (export ".concat(JSON.stringify(exportName), ")"));
                                }
                            }
                            this.appendBuffer(" (import ");
                            this.printImportSource(importInfo);
                            this.appendBuffer(") ".concat(limitsToString(tableImportInfo.limits, tableImportInfo.is64), " ").concat(this.typeToString(tableImportInfo.elementType), ")"));
                            break;
                        case 4 /* ExternalKind.Tag */:
                            var eventImportInfo = importInfo.type;
                            var tagIndex = this._eventCount++;
                            var tagName = this._nameResolver.getTagName(tagIndex, false);
                            this.appendBuffer("  (tag ".concat(tagName));
                            if (this._exportMetadata !== null) {
                                for (var _p = 0, _q = this._exportMetadata.getTagExportNames(tagIndex); _p < _q.length; _p++) {
                                    var exportName = _q[_p];
                                    this.appendBuffer(" (export ".concat(JSON.stringify(exportName), ")"));
                                }
                            }
                            this.appendBuffer(" (import ");
                            this.printImportSource(importInfo);
                            this.appendBuffer(")");
                            this.printFuncType(eventImportInfo.typeIndex);
                            this.appendBuffer(")");
                            break;
                        default:
                            throw new Error("NYI other import types: ".concat(importInfo.kind));
                    }
                    this.newLine();
                    break;
                case 33 /* BinaryReaderState.BEGIN_ELEMENT_SECTION_ENTRY */:
                    var elementSegment = reader.result;
                    var elementIndex = this._elementCount++;
                    var elementName = this._nameResolver.getElementName(elementIndex, false);
                    this.appendBuffer("  (elem ".concat(elementName));
                    switch (elementSegment.mode) {
                        case 0 /* ElementMode.Active */:
                            if (elementSegment.tableIndex !== 0) {
                                var tableName_1 = this._nameResolver.getTableName(elementSegment.tableIndex, false);
                                this.appendBuffer(" (table ".concat(tableName_1, ")"));
                            }
                            break;
                        case 1 /* ElementMode.Passive */:
                            break;
                        case 2 /* ElementMode.Declarative */:
                            this.appendBuffer(" declare");
                            break;
                    }
                    break;
                case 35 /* BinaryReaderState.END_ELEMENT_SECTION_ENTRY */:
                    this.appendBuffer(")");
                    this.newLine();
                    break;
                case 34 /* BinaryReaderState.ELEMENT_SECTION_ENTRY_BODY */:
                    var elementSegmentBody = reader.result;
                    this.appendBuffer(" ".concat(this.typeToString(elementSegmentBody.elementType)));
                    break;
                case 39 /* BinaryReaderState.BEGIN_GLOBAL_SECTION_ENTRY */:
                    var globalInfo = reader.result;
                    var globalIndex = this._globalCount++;
                    var globalName = this._nameResolver.getGlobalName(globalIndex, false);
                    this.appendBuffer("  (global ".concat(globalName));
                    if (this._exportMetadata !== null) {
                        for (var _r = 0, _s = this._exportMetadata.getGlobalExportNames(globalIndex); _r < _s.length; _r++) {
                            var exportName = _s[_r];
                            this.appendBuffer(" (export ".concat(JSON.stringify(exportName), ")"));
                        }
                    }
                    this.appendBuffer(" ".concat(this.globalTypeToString(globalInfo.type)));
                    break;
                case 40 /* BinaryReaderState.END_GLOBAL_SECTION_ENTRY */:
                    this.appendBuffer(")");
                    this.newLine();
                    break;
                case 11 /* BinaryReaderState.TYPE_SECTION_ENTRY */:
                    var typeEntry = reader.result;
                    var typeIndex = this._types.length;
                    this._types.push(typeEntry);
                    if (!this._skipTypes) {
                        var typeName = this._nameResolver.getTypeName(typeIndex, false);
                        var superTypeName = undefined;
                        if (typeEntry.supertypes !== undefined) {
                            superTypeName = typeEntry.supertypes
                                .map(function (ty) { return _this.typeIndexToString(ty); })
                                .join("+");
                        }
                        this.appendBuffer(this._indent);
                        this.appendBuffer("(type ".concat(typeName, " "));
                        var subtype = typeEntry.supertypes || typeEntry.final;
                        if (subtype) {
                            this.appendBuffer("(sub ");
                            if (typeEntry.final)
                                this.appendBuffer("final ");
                            if (typeEntry.supertypes) {
                                this.appendBuffer(typeEntry.supertypes
                                    .map(function (ty) { return _this.typeIndexToString(ty); })
                                    .join(" "));
                                this.appendBuffer(" ");
                            }
                        }
                        if (typeEntry.form === -32 /* TypeKind.func */) {
                            this.appendBuffer("(func");
                            this.printFuncType(typeIndex);
                            this.appendBuffer(")");
                        }
                        else if (typeEntry.form === -33 /* TypeKind.struct */) {
                            this.appendBuffer("(struct");
                            this.printStructType(typeIndex);
                            this.appendBuffer(")");
                        }
                        else if (typeEntry.form === -34 /* TypeKind.array */) {
                            this.appendBuffer("(array");
                            this.printArrayType(typeIndex);
                            this.appendBuffer(")");
                        }
                        else {
                            throw new Error("Unknown type form: ".concat(typeEntry.form));
                        }
                        if (subtype) {
                            this.appendBuffer(")");
                        }
                        this.appendBuffer(")");
                        this.newLine();
                    }
                    break;
                case 22 /* BinaryReaderState.START_SECTION_ENTRY */:
                    var startEntry = reader.result;
                    var funcName = this._nameResolver.getFunctionName(startEntry.index, startEntry.index < this._importCount, true);
                    this.appendBuffer("  (start ".concat(funcName, ")"));
                    this.newLine();
                    break;
                case 36 /* BinaryReaderState.BEGIN_DATA_SECTION_ENTRY */:
                    var dataSegment = reader.result;
                    this.appendBuffer("  (data");
                    if (dataSegment.memoryIndex) {
                        var dataMemoryName = this._nameResolver.getMemoryName(dataSegment.memoryIndex, true);
                        this.appendBuffer(" (memory ".concat(dataMemoryName, ")"));
                    }
                    break;
                case 37 /* BinaryReaderState.DATA_SECTION_ENTRY_BODY */:
                    var body = reader.result;
                    this.appendBuffer(" ");
                    this.printString(body.data);
                    break;
                case 38 /* BinaryReaderState.END_DATA_SECTION_ENTRY */:
                    this.appendBuffer(")");
                    this.newLine();
                    break;
                case 25 /* BinaryReaderState.BEGIN_INIT_EXPRESSION_BODY */:
                case 44 /* BinaryReaderState.BEGIN_OFFSET_EXPRESSION_BODY */:
                    this._expression = [];
                    break;
                case 26 /* BinaryReaderState.INIT_EXPRESSION_OPERATOR */:
                case 45 /* BinaryReaderState.OFFSET_EXPRESSION_OPERATOR */:
                    var operator = reader.result;
                    if (operator.code !== 11 /* OperatorCode.end */) {
                        this._expression.push(operator);
                    }
                    break;
                case 46 /* BinaryReaderState.END_OFFSET_EXPRESSION_BODY */:
                    if (this._expression.length > 1) {
                        this.appendBuffer(" (offset ");
                        this.printExpression(this._expression);
                        this.appendBuffer(")");
                    }
                    else {
                        this.appendBuffer(" ");
                        this.printExpression(this._expression);
                    }
                    this._expression = [];
                    break;
                case 27 /* BinaryReaderState.END_INIT_EXPRESSION_BODY */:
                    if (this._expression.length > 1 &&
                        this._currentSectionId === 9 /* SectionCode.Element */) {
                        this.appendBuffer(" (item ");
                        this.printExpression(this._expression);
                        this.appendBuffer(")");
                    }
                    else {
                        this.appendBuffer(" ");
                        this.printExpression(this._expression);
                    }
                    this._expression = [];
                    break;
                case 13 /* BinaryReaderState.FUNCTION_SECTION_ENTRY */:
                    this._funcTypes.push(reader.result.typeIndex);
                    break;
                case 28 /* BinaryReaderState.BEGIN_FUNCTION_BODY */:
                    var func = reader.result;
                    var type = this._types[this._funcTypes[this._funcIndex - this._importCount]];
                    this.appendBuffer("  (func ");
                    this.appendBuffer(this._nameResolver.getFunctionName(this._funcIndex, false, false));
                    if (this._exportMetadata !== null) {
                        for (var _t = 0, _u = this._exportMetadata.getFunctionExportNames(this._funcIndex); _t < _u.length; _t++) {
                            var exportName = _u[_t];
                            this.appendBuffer(" (export ".concat(JSON.stringify(exportName), ")"));
                        }
                    }
                    for (var i = 0; i < type.params.length; i++) {
                        var paramName = this._nameResolver.getVariableName(this._funcIndex, i, false);
                        this.appendBuffer(" (param ".concat(paramName, " ").concat(this.typeToString(type.params[i]), ")"));
                    }
                    for (var i = 0; i < type.returns.length; i++) {
                        this.appendBuffer(" (result ".concat(this.typeToString(type.returns[i]), ")"));
                    }
                    this.newLine();
                    var localIndex = type.params.length;
                    if (func.locals.length > 0) {
                        this.appendBuffer("   ");
                        for (var _v = 0, _w = func.locals; _v < _w.length; _v++) {
                            var l = _w[_v];
                            for (var i = 0; i < l.count; i++) {
                                var paramName = this._nameResolver.getVariableName(this._funcIndex, localIndex++, false);
                                this.appendBuffer(" (local ".concat(paramName, " ").concat(this.typeToString(l.type), ")"));
                            }
                        }
                        this.newLine();
                    }
                    this._indent = "    ";
                    this._indentLevel = 0;
                    this._labelIndex = 0;
                    this._backrefLabels = this._labelMode === LabelMode.Depth ? null : [];
                    this._logFirstInstruction = true;
                    break;
                case 30 /* BinaryReaderState.CODE_OPERATOR */:
                    if (this._logFirstInstruction) {
                        this.logStartOfFunctionBodyOffset();
                        this._logFirstInstruction = false;
                    }
                    var operator = reader.result;
                    if (operator.code == 11 /* OperatorCode.end */ && this._indentLevel == 0) {
                        // reached of the function, closing function body
                        this.appendBuffer("  )");
                        this.newLine();
                        break;
                    }
                    switch (operator.code) {
                        case 11 /* OperatorCode.end */:
                        case 5 /* OperatorCode.else */:
                        case 7 /* OperatorCode.catch */:
                        case 25 /* OperatorCode.catch_all */:
                        case 24 /* OperatorCode.delegate */:
                            this.decreaseIndent();
                            break;
                    }
                    this.appendBuffer(this._indent);
                    this.printOperator(operator);
                    this.newLine();
                    switch (operator.code) {
                        case 4 /* OperatorCode.if */:
                        case 2 /* OperatorCode.block */:
                        case 3 /* OperatorCode.loop */:
                        case 5 /* OperatorCode.else */:
                        case 6 /* OperatorCode.try */:
                        case 31 /* OperatorCode.try_table */:
                        case 7 /* OperatorCode.catch */:
                        case 25 /* OperatorCode.catch_all */:
                            this.increaseIndent();
                            break;
                    }
                    break;
                case 31 /* BinaryReaderState.END_FUNCTION_BODY */:
                    this._funcIndex++;
                    this._backrefLabels = null;
                    this.logEndOfFunctionBodyOffset();
                    // See case BinaryReaderState.CODE_OPERATOR for closing of body
                    break;
                case 47 /* BinaryReaderState.BEGIN_REC_GROUP */:
                    if (!this._skipTypes) {
                        this.appendBuffer("  (rec");
                        this.newLine();
                        this.increaseIndent();
                    }
                    break;
                case 48 /* BinaryReaderState.END_REC_GROUP */:
                    if (!this._skipTypes) {
                        this.decreaseIndent();
                        this.appendBuffer("  )");
                        this.newLine();
                    }
                    break;
                default:
                    throw new Error("Expectected state: ".concat(reader.state));
            }
        }
    };
    return WasmDisassembler;
}());
exports.WasmDisassembler = WasmDisassembler;
var UNKNOWN_FUNCTION_PREFIX = "unknown";
var NameSectionNameResolver = /** @class */ (function (_super) {
    __extends(NameSectionNameResolver, _super);
    function NameSectionNameResolver(functionNames, localNames, tagNames, typeNames, tableNames, memoryNames, globalNames, fieldNames) {
        var _this = _super.call(this) || this;
        _this._functionNames = functionNames;
        _this._localNames = localNames;
        _this._tagNames = tagNames;
        _this._typeNames = typeNames;
        _this._tableNames = tableNames;
        _this._memoryNames = memoryNames;
        _this._globalNames = globalNames;
        _this._fieldNames = fieldNames;
        return _this;
    }
    NameSectionNameResolver.prototype.getTypeName = function (index, isRef) {
        var name = this._typeNames[index];
        if (!name)
            return _super.prototype.getTypeName.call(this, index, isRef);
        return isRef ? "$".concat(name) : "$".concat(name, " (;").concat(index, ";)");
    };
    NameSectionNameResolver.prototype.getTableName = function (index, isRef) {
        var name = this._tableNames[index];
        if (!name)
            return _super.prototype.getTableName.call(this, index, isRef);
        return isRef ? "$".concat(name) : "$".concat(name, " (;").concat(index, ";)");
    };
    NameSectionNameResolver.prototype.getMemoryName = function (index, isRef) {
        var name = this._memoryNames[index];
        if (!name)
            return _super.prototype.getMemoryName.call(this, index, isRef);
        return isRef ? "$".concat(name) : "$".concat(name, " (;").concat(index, ";)");
    };
    NameSectionNameResolver.prototype.getGlobalName = function (index, isRef) {
        var name = this._globalNames[index];
        if (!name)
            return _super.prototype.getGlobalName.call(this, index, isRef);
        return isRef ? "$".concat(name) : "$".concat(name, " (;").concat(index, ";)");
    };
    NameSectionNameResolver.prototype.getTagName = function (index, isRef) {
        var name = this._tagNames[index];
        if (!name)
            return _super.prototype.getTagName.call(this, index, isRef);
        return isRef ? "$".concat(name) : "$".concat(name, " (;").concat(index, ";)");
    };
    NameSectionNameResolver.prototype.getFunctionName = function (index, isImport, isRef) {
        var name = this._functionNames[index];
        if (!name)
            return "$".concat(UNKNOWN_FUNCTION_PREFIX).concat(index);
        return isRef ? "$".concat(name) : "$".concat(name, " (;").concat(index, ";)");
    };
    NameSectionNameResolver.prototype.getVariableName = function (funcIndex, index, isRef) {
        var name = this._localNames[funcIndex] && this._localNames[funcIndex][index];
        if (!name)
            return _super.prototype.getVariableName.call(this, funcIndex, index, isRef);
        return isRef ? "$".concat(name) : "$".concat(name, " (;").concat(index, ";)");
    };
    NameSectionNameResolver.prototype.getFieldName = function (typeIndex, index, isRef) {
        var name = this._fieldNames[typeIndex] && this._fieldNames[typeIndex][index];
        if (!name)
            return _super.prototype.getFieldName.call(this, typeIndex, index, isRef);
        return isRef ? "$".concat(name) : "$".concat(name, " (;").concat(index, ";)");
    };
    return NameSectionNameResolver;
}(DefaultNameResolver));
var NameSectionReader = /** @class */ (function () {
    function NameSectionReader() {
        this._done = false;
        this._functionsCount = 0;
        this._functionImportsCount = 0;
        this._functionNames = null;
        this._functionLocalNames = null;
        this._tagNames = null;
        this._typeNames = null;
        this._tableNames = null;
        this._memoryNames = null;
        this._globalNames = null;
        this._fieldNames = null;
        this._hasNames = false;
    }
    NameSectionReader.prototype.read = function (reader) {
        var _this = this;
        if (this._done)
            throw new Error("Invalid state: disassembly process was already finished.");
        while (true) {
            if (!reader.read())
                return false;
            switch (reader.state) {
                case 2 /* BinaryReaderState.END_WASM */:
                    if (!reader.hasMoreBytes()) {
                        this._done = true;
                        return true;
                    }
                    break;
                case -1 /* BinaryReaderState.ERROR */:
                    throw reader.error;
                case 1 /* BinaryReaderState.BEGIN_WASM */:
                    this._functionsCount = 0;
                    this._functionImportsCount = 0;
                    this._functionNames = [];
                    this._functionLocalNames = [];
                    this._tagNames = [];
                    this._typeNames = [];
                    this._tableNames = [];
                    this._memoryNames = [];
                    this._globalNames = [];
                    this._fieldNames = [];
                    this._hasNames = false;
                    break;
                case 4 /* BinaryReaderState.END_SECTION */:
                    break;
                case 3 /* BinaryReaderState.BEGIN_SECTION */:
                    var sectionInfo = reader.result;
                    if (sectionInfo.id === 0 /* SectionCode.Custom */ &&
                        (0, WasmParser_js_1.bytesToString)(sectionInfo.name) === NAME_SECTION_NAME) {
                        break;
                    }
                    if (sectionInfo.id === 3 /* SectionCode.Function */ ||
                        sectionInfo.id === 2 /* SectionCode.Import */) {
                        break;
                    }
                    reader.skipSection();
                    break;
                case 12 /* BinaryReaderState.IMPORT_SECTION_ENTRY */:
                    var importInfo = reader.result;
                    if (importInfo.kind === 0 /* ExternalKind.Function */)
                        this._functionImportsCount++;
                    break;
                case 13 /* BinaryReaderState.FUNCTION_SECTION_ENTRY */:
                    this._functionsCount++;
                    break;
                case 19 /* BinaryReaderState.NAME_SECTION_ENTRY */:
                    var nameInfo = reader.result;
                    if (nameInfo.type === 1 /* NameType.Function */) {
                        var names = nameInfo.names;
                        names.forEach(function (_a) {
                            var index = _a.index, name = _a.name;
                            _this._functionNames[index] = (0, WasmParser_js_1.bytesToString)(name);
                        });
                        this._hasNames = true;
                    }
                    else if (nameInfo.type === 2 /* NameType.Local */) {
                        var funcs = nameInfo.funcs;
                        funcs.forEach(function (_a) {
                            var index = _a.index, locals = _a.locals;
                            var localNames = (_this._functionLocalNames[index] = []);
                            locals.forEach(function (_a) {
                                var index = _a.index, name = _a.name;
                                localNames[index] = (0, WasmParser_js_1.bytesToString)(name);
                            });
                        });
                        this._hasNames = true;
                    }
                    else if (nameInfo.type === 11 /* NameType.Tag */) {
                        var names = nameInfo.names;
                        names.forEach(function (_a) {
                            var index = _a.index, name = _a.name;
                            _this._tagNames[index] = (0, WasmParser_js_1.bytesToString)(name);
                        });
                        this._hasNames = true;
                    }
                    else if (nameInfo.type === 4 /* NameType.Type */) {
                        var names = nameInfo.names;
                        names.forEach(function (_a) {
                            var index = _a.index, name = _a.name;
                            _this._typeNames[index] = (0, WasmParser_js_1.bytesToString)(name);
                        });
                        this._hasNames = true;
                    }
                    else if (nameInfo.type === 5 /* NameType.Table */) {
                        var names = nameInfo.names;
                        names.forEach(function (_a) {
                            var index = _a.index, name = _a.name;
                            _this._tableNames[index] = (0, WasmParser_js_1.bytesToString)(name);
                        });
                        this._hasNames = true;
                    }
                    else if (nameInfo.type === 6 /* NameType.Memory */) {
                        var names = nameInfo.names;
                        names.forEach(function (_a) {
                            var index = _a.index, name = _a.name;
                            _this._memoryNames[index] = (0, WasmParser_js_1.bytesToString)(name);
                        });
                        this._hasNames = true;
                    }
                    else if (nameInfo.type === 7 /* NameType.Global */) {
                        var names = nameInfo.names;
                        names.forEach(function (_a) {
                            var index = _a.index, name = _a.name;
                            _this._globalNames[index] = (0, WasmParser_js_1.bytesToString)(name);
                        });
                        this._hasNames = true;
                    }
                    else if (nameInfo.type === 10 /* NameType.Field */) {
                        var types = nameInfo.types;
                        types.forEach(function (_a) {
                            var index = _a.index, fields = _a.fields;
                            var fieldNames = (_this._fieldNames[index] = []);
                            fields.forEach(function (_a) {
                                var index = _a.index, name = _a.name;
                                fieldNames[index] = (0, WasmParser_js_1.bytesToString)(name);
                            });
                        });
                    }
                    break;
                default:
                    throw new Error("Expectected state: ".concat(reader.state));
            }
        }
    };
    NameSectionReader.prototype.hasValidNames = function () {
        return this._hasNames;
    };
    NameSectionReader.prototype.getNameResolver = function () {
        if (!this.hasValidNames())
            throw new Error("Has no valid name section");
        // Fix bad names.
        var functionNamesLength = this._functionImportsCount + this._functionsCount;
        var functionNames = this._functionNames.slice(0, functionNamesLength);
        var usedNameAt = Object.create(null);
        for (var i = 0; i < functionNames.length; i++) {
            var name_1 = functionNames[i];
            if (!name_1)
                continue;
            var goodName = !(name_1 in usedNameAt) &&
                isValidName(name_1) &&
                name_1.indexOf(UNKNOWN_FUNCTION_PREFIX) !== 0;
            if (!goodName) {
                if (usedNameAt[name_1] >= 0) {
                    // Remove all non-unique names.
                    functionNames[usedNameAt[name_1]] = null;
                    usedNameAt[name_1] = -1;
                }
                functionNames[i] = null;
                continue;
            }
            usedNameAt[name_1] = i;
        }
        return new NameSectionNameResolver(functionNames, this._functionLocalNames, this._tagNames, this._typeNames, this._tableNames, this._memoryNames, this._globalNames, this._fieldNames);
    };
    return NameSectionReader;
}());
exports.NameSectionReader = NameSectionReader;
var DevToolsNameResolver = /** @class */ (function (_super) {
    __extends(DevToolsNameResolver, _super);
    function DevToolsNameResolver(functionNames, localNames, tagNames, typeNames, tableNames, memoryNames, globalNames, fieldNames) {
        return _super.call(this, functionNames, localNames, tagNames, typeNames, tableNames, memoryNames, globalNames, fieldNames) || this;
    }
    DevToolsNameResolver.prototype.getFunctionName = function (index, isImport, isRef) {
        var name = this._functionNames[index];
        if (!name)
            return isImport ? "$import".concat(index) : "$func".concat(index);
        return isRef ? "$".concat(name) : "$".concat(name, " (;").concat(index, ";)");
    };
    return DevToolsNameResolver;
}(NameSectionNameResolver));
exports.DevToolsNameResolver = DevToolsNameResolver;
var DevToolsNameGenerator = /** @class */ (function () {
    function DevToolsNameGenerator() {
        this._done = false;
        this._functionImportsCount = 0;
        this._memoryImportsCount = 0;
        this._tableImportsCount = 0;
        this._globalImportsCount = 0;
        this._tagImportsCount = 0;
        this._functionNames = null;
        this._functionLocalNames = null;
        this._tagNames = null;
        this._memoryNames = null;
        this._typeNames = null;
        this._tableNames = null;
        this._globalNames = null;
        this._fieldNames = null;
        this._functionExportNames = null;
        this._globalExportNames = null;
        this._memoryExportNames = null;
        this._tableExportNames = null;
        this._tagExportNames = null;
    }
    DevToolsNameGenerator.prototype._addExportName = function (exportNames, index, name) {
        var names = exportNames[index];
        if (names) {
            names.push(name);
        }
        else {
            exportNames[index] = [name];
        }
    };
    DevToolsNameGenerator.prototype._setName = function (names, index, name, isNameSectionName) {
        if (!name)
            return;
        if (isNameSectionName) {
            if (!isValidName(name))
                return;
            names[index] = name;
        }
        else if (!names[index]) {
            names[index] = name.replace(INVALID_NAME_SYMBOLS_REGEX_GLOBAL, "_");
        }
    };
    DevToolsNameGenerator.prototype.read = function (reader) {
        var _this = this;
        if (this._done)
            throw new Error("Invalid state: disassembly process was already finished.");
        while (true) {
            if (!reader.read())
                return false;
            switch (reader.state) {
                case 2 /* BinaryReaderState.END_WASM */:
                    if (!reader.hasMoreBytes()) {
                        this._done = true;
                        return true;
                    }
                    break;
                case -1 /* BinaryReaderState.ERROR */:
                    throw reader.error;
                case 1 /* BinaryReaderState.BEGIN_WASM */:
                    this._functionImportsCount = 0;
                    this._memoryImportsCount = 0;
                    this._tableImportsCount = 0;
                    this._globalImportsCount = 0;
                    this._tagImportsCount = 0;
                    this._functionNames = [];
                    this._functionLocalNames = [];
                    this._tagNames = [];
                    this._memoryNames = [];
                    this._typeNames = [];
                    this._tableNames = [];
                    this._globalNames = [];
                    this._fieldNames = [];
                    this._functionExportNames = [];
                    this._globalExportNames = [];
                    this._memoryExportNames = [];
                    this._tableExportNames = [];
                    this._tagExportNames = [];
                    break;
                case 4 /* BinaryReaderState.END_SECTION */:
                    break;
                case 3 /* BinaryReaderState.BEGIN_SECTION */:
                    var sectionInfo = reader.result;
                    if (sectionInfo.id === 0 /* SectionCode.Custom */ &&
                        (0, WasmParser_js_1.bytesToString)(sectionInfo.name) === NAME_SECTION_NAME) {
                        break;
                    }
                    switch (sectionInfo.id) {
                        case 2 /* SectionCode.Import */:
                        case 7 /* SectionCode.Export */:
                            break; // reading known section;
                        default:
                            reader.skipSection();
                            break;
                    }
                    break;
                case 12 /* BinaryReaderState.IMPORT_SECTION_ENTRY */:
                    var importInfo = reader.result;
                    var importName = "".concat((0, WasmParser_js_1.bytesToString)(importInfo.module), ".").concat((0, WasmParser_js_1.bytesToString)(importInfo.field));
                    switch (importInfo.kind) {
                        case 0 /* ExternalKind.Function */:
                            this._setName(this._functionNames, this._functionImportsCount++, importName, false);
                            break;
                        case 1 /* ExternalKind.Table */:
                            this._setName(this._tableNames, this._tableImportsCount++, importName, false);
                            break;
                        case 2 /* ExternalKind.Memory */:
                            this._setName(this._memoryNames, this._memoryImportsCount++, importName, false);
                            break;
                        case 3 /* ExternalKind.Global */:
                            this._setName(this._globalNames, this._globalImportsCount++, importName, false);
                            break;
                        case 4 /* ExternalKind.Tag */:
                            this._setName(this._tagNames, this._tagImportsCount++, importName, false);
                        default:
                            throw new Error("Unsupported export ".concat(importInfo.kind));
                    }
                    break;
                case 19 /* BinaryReaderState.NAME_SECTION_ENTRY */:
                    var nameInfo = reader.result;
                    if (nameInfo.type === 1 /* NameType.Function */) {
                        var names = nameInfo.names;
                        names.forEach(function (_a) {
                            var index = _a.index, name = _a.name;
                            _this._setName(_this._functionNames, index, (0, WasmParser_js_1.bytesToString)(name), true);
                        });
                    }
                    else if (nameInfo.type === 2 /* NameType.Local */) {
                        var funcs = nameInfo.funcs;
                        funcs.forEach(function (_a) {
                            var index = _a.index, locals = _a.locals;
                            var localNames = (_this._functionLocalNames[index] = []);
                            locals.forEach(function (_a) {
                                var index = _a.index, name = _a.name;
                                localNames[index] = (0, WasmParser_js_1.bytesToString)(name);
                            });
                        });
                    }
                    else if (nameInfo.type === 11 /* NameType.Tag */) {
                        var names = nameInfo.names;
                        names.forEach(function (_a) {
                            var index = _a.index, name = _a.name;
                            _this._setName(_this._tagNames, index, (0, WasmParser_js_1.bytesToString)(name), true);
                        });
                    }
                    else if (nameInfo.type === 4 /* NameType.Type */) {
                        var names = nameInfo.names;
                        names.forEach(function (_a) {
                            var index = _a.index, name = _a.name;
                            _this._setName(_this._typeNames, index, (0, WasmParser_js_1.bytesToString)(name), true);
                        });
                    }
                    else if (nameInfo.type === 5 /* NameType.Table */) {
                        var names = nameInfo.names;
                        names.forEach(function (_a) {
                            var index = _a.index, name = _a.name;
                            _this._setName(_this._tableNames, index, (0, WasmParser_js_1.bytesToString)(name), true);
                        });
                    }
                    else if (nameInfo.type === 6 /* NameType.Memory */) {
                        var names = nameInfo.names;
                        names.forEach(function (_a) {
                            var index = _a.index, name = _a.name;
                            _this._setName(_this._memoryNames, index, (0, WasmParser_js_1.bytesToString)(name), true);
                        });
                    }
                    else if (nameInfo.type === 7 /* NameType.Global */) {
                        var names = nameInfo.names;
                        names.forEach(function (_a) {
                            var index = _a.index, name = _a.name;
                            _this._setName(_this._globalNames, index, (0, WasmParser_js_1.bytesToString)(name), true);
                        });
                    }
                    else if (nameInfo.type === 10 /* NameType.Field */) {
                        var types = nameInfo.types;
                        types.forEach(function (_a) {
                            var index = _a.index, fields = _a.fields;
                            var fieldNames = (_this._fieldNames[index] = []);
                            fields.forEach(function (_a) {
                                var index = _a.index, name = _a.name;
                                fieldNames[index] = (0, WasmParser_js_1.bytesToString)(name);
                            });
                        });
                    }
                    break;
                case 17 /* BinaryReaderState.EXPORT_SECTION_ENTRY */:
                    var exportInfo = reader.result;
                    var exportName = (0, WasmParser_js_1.bytesToString)(exportInfo.field);
                    switch (exportInfo.kind) {
                        case 0 /* ExternalKind.Function */:
                            this._addExportName(this._functionExportNames, exportInfo.index, exportName);
                            this._setName(this._functionNames, exportInfo.index, exportName, false);
                            break;
                        case 3 /* ExternalKind.Global */:
                            this._addExportName(this._globalExportNames, exportInfo.index, exportName);
                            this._setName(this._globalNames, exportInfo.index, exportName, false);
                            break;
                        case 2 /* ExternalKind.Memory */:
                            this._addExportName(this._memoryExportNames, exportInfo.index, exportName);
                            this._setName(this._memoryNames, exportInfo.index, exportName, false);
                            break;
                        case 1 /* ExternalKind.Table */:
                            this._addExportName(this._tableExportNames, exportInfo.index, exportName);
                            this._setName(this._tableNames, exportInfo.index, exportName, false);
                            break;
                        case 4 /* ExternalKind.Tag */:
                            this._addExportName(this._tagExportNames, exportInfo.index, exportName);
                            this._setName(this._tagNames, exportInfo.index, exportName, false);
                            break;
                        default:
                            throw new Error("Unsupported export ".concat(exportInfo.kind));
                    }
                    break;
                default:
                    throw new Error("Expectected state: ".concat(reader.state));
            }
        }
    };
    DevToolsNameGenerator.prototype.getExportMetadata = function () {
        return new DevToolsExportMetadata(this._functionExportNames, this._globalExportNames, this._memoryExportNames, this._tableExportNames, this._tagExportNames);
    };
    DevToolsNameGenerator.prototype.getNameResolver = function () {
        return new DevToolsNameResolver(this._functionNames, this._functionLocalNames, this._tagNames, this._typeNames, this._tableNames, this._memoryNames, this._globalNames, this._fieldNames);
    };
    return DevToolsNameGenerator;
}());
exports.DevToolsNameGenerator = DevToolsNameGenerator;
