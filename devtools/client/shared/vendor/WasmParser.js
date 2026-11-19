"use strict";
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
exports.bytesToString = exports.BinaryReader = exports.Int64 = exports.TagAttribute = exports.ElementMode = exports.DataMode = exports.BinaryReaderState = exports.NameType = exports.LinkingType = exports.RelocType = exports.CatchHandler = exports.CatchHandlerKind = exports.RefType = exports.Type = exports.FuncDef = exports.FieldDef = exports.TypeKind = exports.ExternalKind = exports.OperatorCodeNames = exports.OperatorCode = exports.SectionCode = void 0;
// See https://github.com/WebAssembly/design/blob/master/BinaryEncoding.md
var WASM_MAGIC_NUMBER = 0x6d736100;
var WASM_SUPPORTED_EXPERIMENTAL_VERSION = 0xd;
var WASM_SUPPORTED_VERSION = 0x1;
var SectionCode;
(function (SectionCode) {
    SectionCode[SectionCode["Unknown"] = -1] = "Unknown";
    SectionCode[SectionCode["Custom"] = 0] = "Custom";
    SectionCode[SectionCode["Type"] = 1] = "Type";
    SectionCode[SectionCode["Import"] = 2] = "Import";
    SectionCode[SectionCode["Function"] = 3] = "Function";
    SectionCode[SectionCode["Table"] = 4] = "Table";
    SectionCode[SectionCode["Memory"] = 5] = "Memory";
    SectionCode[SectionCode["Global"] = 6] = "Global";
    SectionCode[SectionCode["Export"] = 7] = "Export";
    SectionCode[SectionCode["Start"] = 8] = "Start";
    SectionCode[SectionCode["Element"] = 9] = "Element";
    SectionCode[SectionCode["Code"] = 10] = "Code";
    SectionCode[SectionCode["Data"] = 11] = "Data";
    SectionCode[SectionCode["DataCount"] = 12] = "DataCount";
    SectionCode[SectionCode["Tag"] = 13] = "Tag";
})(SectionCode = exports.SectionCode || (exports.SectionCode = {}));
var OperatorCode;
(function (OperatorCode) {
    OperatorCode[OperatorCode["unreachable"] = 0] = "unreachable";
    OperatorCode[OperatorCode["nop"] = 1] = "nop";
    OperatorCode[OperatorCode["block"] = 2] = "block";
    OperatorCode[OperatorCode["loop"] = 3] = "loop";
    OperatorCode[OperatorCode["if"] = 4] = "if";
    OperatorCode[OperatorCode["else"] = 5] = "else";
    OperatorCode[OperatorCode["try"] = 6] = "try";
    OperatorCode[OperatorCode["catch"] = 7] = "catch";
    OperatorCode[OperatorCode["throw"] = 8] = "throw";
    OperatorCode[OperatorCode["rethrow"] = 9] = "rethrow";
    OperatorCode[OperatorCode["throw_ref"] = 10] = "throw_ref";
    OperatorCode[OperatorCode["end"] = 11] = "end";
    OperatorCode[OperatorCode["br"] = 12] = "br";
    OperatorCode[OperatorCode["br_if"] = 13] = "br_if";
    OperatorCode[OperatorCode["br_table"] = 14] = "br_table";
    OperatorCode[OperatorCode["return"] = 15] = "return";
    OperatorCode[OperatorCode["call"] = 16] = "call";
    OperatorCode[OperatorCode["call_indirect"] = 17] = "call_indirect";
    OperatorCode[OperatorCode["return_call"] = 18] = "return_call";
    OperatorCode[OperatorCode["return_call_indirect"] = 19] = "return_call_indirect";
    OperatorCode[OperatorCode["call_ref"] = 20] = "call_ref";
    OperatorCode[OperatorCode["return_call_ref"] = 21] = "return_call_ref";
    OperatorCode[OperatorCode["let"] = 23] = "let";
    OperatorCode[OperatorCode["delegate"] = 24] = "delegate";
    OperatorCode[OperatorCode["catch_all"] = 25] = "catch_all";
    OperatorCode[OperatorCode["drop"] = 26] = "drop";
    OperatorCode[OperatorCode["select"] = 27] = "select";
    OperatorCode[OperatorCode["select_with_type"] = 28] = "select_with_type";
    OperatorCode[OperatorCode["try_table"] = 31] = "try_table";
    OperatorCode[OperatorCode["local_get"] = 32] = "local_get";
    OperatorCode[OperatorCode["local_set"] = 33] = "local_set";
    OperatorCode[OperatorCode["local_tee"] = 34] = "local_tee";
    OperatorCode[OperatorCode["global_get"] = 35] = "global_get";
    OperatorCode[OperatorCode["global_set"] = 36] = "global_set";
    OperatorCode[OperatorCode["i32_load"] = 40] = "i32_load";
    OperatorCode[OperatorCode["i64_load"] = 41] = "i64_load";
    OperatorCode[OperatorCode["f32_load"] = 42] = "f32_load";
    OperatorCode[OperatorCode["f64_load"] = 43] = "f64_load";
    OperatorCode[OperatorCode["i32_load8_s"] = 44] = "i32_load8_s";
    OperatorCode[OperatorCode["i32_load8_u"] = 45] = "i32_load8_u";
    OperatorCode[OperatorCode["i32_load16_s"] = 46] = "i32_load16_s";
    OperatorCode[OperatorCode["i32_load16_u"] = 47] = "i32_load16_u";
    OperatorCode[OperatorCode["i64_load8_s"] = 48] = "i64_load8_s";
    OperatorCode[OperatorCode["i64_load8_u"] = 49] = "i64_load8_u";
    OperatorCode[OperatorCode["i64_load16_s"] = 50] = "i64_load16_s";
    OperatorCode[OperatorCode["i64_load16_u"] = 51] = "i64_load16_u";
    OperatorCode[OperatorCode["i64_load32_s"] = 52] = "i64_load32_s";
    OperatorCode[OperatorCode["i64_load32_u"] = 53] = "i64_load32_u";
    OperatorCode[OperatorCode["i32_store"] = 54] = "i32_store";
    OperatorCode[OperatorCode["i64_store"] = 55] = "i64_store";
    OperatorCode[OperatorCode["f32_store"] = 56] = "f32_store";
    OperatorCode[OperatorCode["f64_store"] = 57] = "f64_store";
    OperatorCode[OperatorCode["i32_store8"] = 58] = "i32_store8";
    OperatorCode[OperatorCode["i32_store16"] = 59] = "i32_store16";
    OperatorCode[OperatorCode["i64_store8"] = 60] = "i64_store8";
    OperatorCode[OperatorCode["i64_store16"] = 61] = "i64_store16";
    OperatorCode[OperatorCode["i64_store32"] = 62] = "i64_store32";
    OperatorCode[OperatorCode["memory_size"] = 63] = "memory_size";
    OperatorCode[OperatorCode["memory_grow"] = 64] = "memory_grow";
    OperatorCode[OperatorCode["i32_const"] = 65] = "i32_const";
    OperatorCode[OperatorCode["i64_const"] = 66] = "i64_const";
    OperatorCode[OperatorCode["f32_const"] = 67] = "f32_const";
    OperatorCode[OperatorCode["f64_const"] = 68] = "f64_const";
    OperatorCode[OperatorCode["i32_eqz"] = 69] = "i32_eqz";
    OperatorCode[OperatorCode["i32_eq"] = 70] = "i32_eq";
    OperatorCode[OperatorCode["i32_ne"] = 71] = "i32_ne";
    OperatorCode[OperatorCode["i32_lt_s"] = 72] = "i32_lt_s";
    OperatorCode[OperatorCode["i32_lt_u"] = 73] = "i32_lt_u";
    OperatorCode[OperatorCode["i32_gt_s"] = 74] = "i32_gt_s";
    OperatorCode[OperatorCode["i32_gt_u"] = 75] = "i32_gt_u";
    OperatorCode[OperatorCode["i32_le_s"] = 76] = "i32_le_s";
    OperatorCode[OperatorCode["i32_le_u"] = 77] = "i32_le_u";
    OperatorCode[OperatorCode["i32_ge_s"] = 78] = "i32_ge_s";
    OperatorCode[OperatorCode["i32_ge_u"] = 79] = "i32_ge_u";
    OperatorCode[OperatorCode["i64_eqz"] = 80] = "i64_eqz";
    OperatorCode[OperatorCode["i64_eq"] = 81] = "i64_eq";
    OperatorCode[OperatorCode["i64_ne"] = 82] = "i64_ne";
    OperatorCode[OperatorCode["i64_lt_s"] = 83] = "i64_lt_s";
    OperatorCode[OperatorCode["i64_lt_u"] = 84] = "i64_lt_u";
    OperatorCode[OperatorCode["i64_gt_s"] = 85] = "i64_gt_s";
    OperatorCode[OperatorCode["i64_gt_u"] = 86] = "i64_gt_u";
    OperatorCode[OperatorCode["i64_le_s"] = 87] = "i64_le_s";
    OperatorCode[OperatorCode["i64_le_u"] = 88] = "i64_le_u";
    OperatorCode[OperatorCode["i64_ge_s"] = 89] = "i64_ge_s";
    OperatorCode[OperatorCode["i64_ge_u"] = 90] = "i64_ge_u";
    OperatorCode[OperatorCode["f32_eq"] = 91] = "f32_eq";
    OperatorCode[OperatorCode["f32_ne"] = 92] = "f32_ne";
    OperatorCode[OperatorCode["f32_lt"] = 93] = "f32_lt";
    OperatorCode[OperatorCode["f32_gt"] = 94] = "f32_gt";
    OperatorCode[OperatorCode["f32_le"] = 95] = "f32_le";
    OperatorCode[OperatorCode["f32_ge"] = 96] = "f32_ge";
    OperatorCode[OperatorCode["f64_eq"] = 97] = "f64_eq";
    OperatorCode[OperatorCode["f64_ne"] = 98] = "f64_ne";
    OperatorCode[OperatorCode["f64_lt"] = 99] = "f64_lt";
    OperatorCode[OperatorCode["f64_gt"] = 100] = "f64_gt";
    OperatorCode[OperatorCode["f64_le"] = 101] = "f64_le";
    OperatorCode[OperatorCode["f64_ge"] = 102] = "f64_ge";
    OperatorCode[OperatorCode["i32_clz"] = 103] = "i32_clz";
    OperatorCode[OperatorCode["i32_ctz"] = 104] = "i32_ctz";
    OperatorCode[OperatorCode["i32_popcnt"] = 105] = "i32_popcnt";
    OperatorCode[OperatorCode["i32_add"] = 106] = "i32_add";
    OperatorCode[OperatorCode["i32_sub"] = 107] = "i32_sub";
    OperatorCode[OperatorCode["i32_mul"] = 108] = "i32_mul";
    OperatorCode[OperatorCode["i32_div_s"] = 109] = "i32_div_s";
    OperatorCode[OperatorCode["i32_div_u"] = 110] = "i32_div_u";
    OperatorCode[OperatorCode["i32_rem_s"] = 111] = "i32_rem_s";
    OperatorCode[OperatorCode["i32_rem_u"] = 112] = "i32_rem_u";
    OperatorCode[OperatorCode["i32_and"] = 113] = "i32_and";
    OperatorCode[OperatorCode["i32_or"] = 114] = "i32_or";
    OperatorCode[OperatorCode["i32_xor"] = 115] = "i32_xor";
    OperatorCode[OperatorCode["i32_shl"] = 116] = "i32_shl";
    OperatorCode[OperatorCode["i32_shr_s"] = 117] = "i32_shr_s";
    OperatorCode[OperatorCode["i32_shr_u"] = 118] = "i32_shr_u";
    OperatorCode[OperatorCode["i32_rotl"] = 119] = "i32_rotl";
    OperatorCode[OperatorCode["i32_rotr"] = 120] = "i32_rotr";
    OperatorCode[OperatorCode["i64_clz"] = 121] = "i64_clz";
    OperatorCode[OperatorCode["i64_ctz"] = 122] = "i64_ctz";
    OperatorCode[OperatorCode["i64_popcnt"] = 123] = "i64_popcnt";
    OperatorCode[OperatorCode["i64_add"] = 124] = "i64_add";
    OperatorCode[OperatorCode["i64_sub"] = 125] = "i64_sub";
    OperatorCode[OperatorCode["i64_mul"] = 126] = "i64_mul";
    OperatorCode[OperatorCode["i64_div_s"] = 127] = "i64_div_s";
    OperatorCode[OperatorCode["i64_div_u"] = 128] = "i64_div_u";
    OperatorCode[OperatorCode["i64_rem_s"] = 129] = "i64_rem_s";
    OperatorCode[OperatorCode["i64_rem_u"] = 130] = "i64_rem_u";
    OperatorCode[OperatorCode["i64_and"] = 131] = "i64_and";
    OperatorCode[OperatorCode["i64_or"] = 132] = "i64_or";
    OperatorCode[OperatorCode["i64_xor"] = 133] = "i64_xor";
    OperatorCode[OperatorCode["i64_shl"] = 134] = "i64_shl";
    OperatorCode[OperatorCode["i64_shr_s"] = 135] = "i64_shr_s";
    OperatorCode[OperatorCode["i64_shr_u"] = 136] = "i64_shr_u";
    OperatorCode[OperatorCode["i64_rotl"] = 137] = "i64_rotl";
    OperatorCode[OperatorCode["i64_rotr"] = 138] = "i64_rotr";
    OperatorCode[OperatorCode["f32_abs"] = 139] = "f32_abs";
    OperatorCode[OperatorCode["f32_neg"] = 140] = "f32_neg";
    OperatorCode[OperatorCode["f32_ceil"] = 141] = "f32_ceil";
    OperatorCode[OperatorCode["f32_floor"] = 142] = "f32_floor";
    OperatorCode[OperatorCode["f32_trunc"] = 143] = "f32_trunc";
    OperatorCode[OperatorCode["f32_nearest"] = 144] = "f32_nearest";
    OperatorCode[OperatorCode["f32_sqrt"] = 145] = "f32_sqrt";
    OperatorCode[OperatorCode["f32_add"] = 146] = "f32_add";
    OperatorCode[OperatorCode["f32_sub"] = 147] = "f32_sub";
    OperatorCode[OperatorCode["f32_mul"] = 148] = "f32_mul";
    OperatorCode[OperatorCode["f32_div"] = 149] = "f32_div";
    OperatorCode[OperatorCode["f32_min"] = 150] = "f32_min";
    OperatorCode[OperatorCode["f32_max"] = 151] = "f32_max";
    OperatorCode[OperatorCode["f32_copysign"] = 152] = "f32_copysign";
    OperatorCode[OperatorCode["f64_abs"] = 153] = "f64_abs";
    OperatorCode[OperatorCode["f64_neg"] = 154] = "f64_neg";
    OperatorCode[OperatorCode["f64_ceil"] = 155] = "f64_ceil";
    OperatorCode[OperatorCode["f64_floor"] = 156] = "f64_floor";
    OperatorCode[OperatorCode["f64_trunc"] = 157] = "f64_trunc";
    OperatorCode[OperatorCode["f64_nearest"] = 158] = "f64_nearest";
    OperatorCode[OperatorCode["f64_sqrt"] = 159] = "f64_sqrt";
    OperatorCode[OperatorCode["f64_add"] = 160] = "f64_add";
    OperatorCode[OperatorCode["f64_sub"] = 161] = "f64_sub";
    OperatorCode[OperatorCode["f64_mul"] = 162] = "f64_mul";
    OperatorCode[OperatorCode["f64_div"] = 163] = "f64_div";
    OperatorCode[OperatorCode["f64_min"] = 164] = "f64_min";
    OperatorCode[OperatorCode["f64_max"] = 165] = "f64_max";
    OperatorCode[OperatorCode["f64_copysign"] = 166] = "f64_copysign";
    OperatorCode[OperatorCode["i32_wrap_i64"] = 167] = "i32_wrap_i64";
    OperatorCode[OperatorCode["i32_trunc_f32_s"] = 168] = "i32_trunc_f32_s";
    OperatorCode[OperatorCode["i32_trunc_f32_u"] = 169] = "i32_trunc_f32_u";
    OperatorCode[OperatorCode["i32_trunc_f64_s"] = 170] = "i32_trunc_f64_s";
    OperatorCode[OperatorCode["i32_trunc_f64_u"] = 171] = "i32_trunc_f64_u";
    OperatorCode[OperatorCode["i64_extend_i32_s"] = 172] = "i64_extend_i32_s";
    OperatorCode[OperatorCode["i64_extend_i32_u"] = 173] = "i64_extend_i32_u";
    OperatorCode[OperatorCode["i64_trunc_f32_s"] = 174] = "i64_trunc_f32_s";
    OperatorCode[OperatorCode["i64_trunc_f32_u"] = 175] = "i64_trunc_f32_u";
    OperatorCode[OperatorCode["i64_trunc_f64_s"] = 176] = "i64_trunc_f64_s";
    OperatorCode[OperatorCode["i64_trunc_f64_u"] = 177] = "i64_trunc_f64_u";
    OperatorCode[OperatorCode["f32_convert_i32_s"] = 178] = "f32_convert_i32_s";
    OperatorCode[OperatorCode["f32_convert_i32_u"] = 179] = "f32_convert_i32_u";
    OperatorCode[OperatorCode["f32_convert_i64_s"] = 180] = "f32_convert_i64_s";
    OperatorCode[OperatorCode["f32_convert_i64_u"] = 181] = "f32_convert_i64_u";
    OperatorCode[OperatorCode["f32_demote_f64"] = 182] = "f32_demote_f64";
    OperatorCode[OperatorCode["f64_convert_i32_s"] = 183] = "f64_convert_i32_s";
    OperatorCode[OperatorCode["f64_convert_i32_u"] = 184] = "f64_convert_i32_u";
    OperatorCode[OperatorCode["f64_convert_i64_s"] = 185] = "f64_convert_i64_s";
    OperatorCode[OperatorCode["f64_convert_i64_u"] = 186] = "f64_convert_i64_u";
    OperatorCode[OperatorCode["f64_promote_f32"] = 187] = "f64_promote_f32";
    OperatorCode[OperatorCode["i32_reinterpret_f32"] = 188] = "i32_reinterpret_f32";
    OperatorCode[OperatorCode["i64_reinterpret_f64"] = 189] = "i64_reinterpret_f64";
    OperatorCode[OperatorCode["f32_reinterpret_i32"] = 190] = "f32_reinterpret_i32";
    OperatorCode[OperatorCode["f64_reinterpret_i64"] = 191] = "f64_reinterpret_i64";
    OperatorCode[OperatorCode["i32_extend8_s"] = 192] = "i32_extend8_s";
    OperatorCode[OperatorCode["i32_extend16_s"] = 193] = "i32_extend16_s";
    OperatorCode[OperatorCode["i64_extend8_s"] = 194] = "i64_extend8_s";
    OperatorCode[OperatorCode["i64_extend16_s"] = 195] = "i64_extend16_s";
    OperatorCode[OperatorCode["i64_extend32_s"] = 196] = "i64_extend32_s";
    OperatorCode[OperatorCode["prefix_0xfb"] = 251] = "prefix_0xfb";
    OperatorCode[OperatorCode["prefix_0xfc"] = 252] = "prefix_0xfc";
    OperatorCode[OperatorCode["prefix_0xfd"] = 253] = "prefix_0xfd";
    OperatorCode[OperatorCode["prefix_0xfe"] = 254] = "prefix_0xfe";
    OperatorCode[OperatorCode["i32_trunc_sat_f32_s"] = 64512] = "i32_trunc_sat_f32_s";
    OperatorCode[OperatorCode["i32_trunc_sat_f32_u"] = 64513] = "i32_trunc_sat_f32_u";
    OperatorCode[OperatorCode["i32_trunc_sat_f64_s"] = 64514] = "i32_trunc_sat_f64_s";
    OperatorCode[OperatorCode["i32_trunc_sat_f64_u"] = 64515] = "i32_trunc_sat_f64_u";
    OperatorCode[OperatorCode["i64_trunc_sat_f32_s"] = 64516] = "i64_trunc_sat_f32_s";
    OperatorCode[OperatorCode["i64_trunc_sat_f32_u"] = 64517] = "i64_trunc_sat_f32_u";
    OperatorCode[OperatorCode["i64_trunc_sat_f64_s"] = 64518] = "i64_trunc_sat_f64_s";
    OperatorCode[OperatorCode["i64_trunc_sat_f64_u"] = 64519] = "i64_trunc_sat_f64_u";
    OperatorCode[OperatorCode["memory_init"] = 64520] = "memory_init";
    OperatorCode[OperatorCode["data_drop"] = 64521] = "data_drop";
    OperatorCode[OperatorCode["memory_copy"] = 64522] = "memory_copy";
    OperatorCode[OperatorCode["memory_fill"] = 64523] = "memory_fill";
    OperatorCode[OperatorCode["table_init"] = 64524] = "table_init";
    OperatorCode[OperatorCode["elem_drop"] = 64525] = "elem_drop";
    OperatorCode[OperatorCode["table_copy"] = 64526] = "table_copy";
    OperatorCode[OperatorCode["table_grow"] = 64527] = "table_grow";
    OperatorCode[OperatorCode["table_size"] = 64528] = "table_size";
    OperatorCode[OperatorCode["table_fill"] = 64529] = "table_fill";
    OperatorCode[OperatorCode["i64_add128"] = 64531] = "i64_add128";
    OperatorCode[OperatorCode["i64_sub128"] = 64532] = "i64_sub128";
    OperatorCode[OperatorCode["i64_mul_wide_s"] = 64533] = "i64_mul_wide_s";
    OperatorCode[OperatorCode["i64_mul_wide_u"] = 64534] = "i64_mul_wide_u";
    OperatorCode[OperatorCode["table_get"] = 37] = "table_get";
    OperatorCode[OperatorCode["table_set"] = 38] = "table_set";
    OperatorCode[OperatorCode["ref_null"] = 208] = "ref_null";
    OperatorCode[OperatorCode["ref_is_null"] = 209] = "ref_is_null";
    OperatorCode[OperatorCode["ref_func"] = 210] = "ref_func";
    OperatorCode[OperatorCode["ref_eq"] = 211] = "ref_eq";
    OperatorCode[OperatorCode["ref_as_non_null"] = 212] = "ref_as_non_null";
    OperatorCode[OperatorCode["br_on_null"] = 213] = "br_on_null";
    OperatorCode[OperatorCode["br_on_non_null"] = 214] = "br_on_non_null";
    OperatorCode[OperatorCode["memory_atomic_notify"] = 65024] = "memory_atomic_notify";
    OperatorCode[OperatorCode["memory_atomic_wait32"] = 65025] = "memory_atomic_wait32";
    OperatorCode[OperatorCode["memory_atomic_wait64"] = 65026] = "memory_atomic_wait64";
    OperatorCode[OperatorCode["atomic_fence"] = 65027] = "atomic_fence";
    OperatorCode[OperatorCode["i32_atomic_load"] = 65040] = "i32_atomic_load";
    OperatorCode[OperatorCode["i64_atomic_load"] = 65041] = "i64_atomic_load";
    OperatorCode[OperatorCode["i32_atomic_load8_u"] = 65042] = "i32_atomic_load8_u";
    OperatorCode[OperatorCode["i32_atomic_load16_u"] = 65043] = "i32_atomic_load16_u";
    OperatorCode[OperatorCode["i64_atomic_load8_u"] = 65044] = "i64_atomic_load8_u";
    OperatorCode[OperatorCode["i64_atomic_load16_u"] = 65045] = "i64_atomic_load16_u";
    OperatorCode[OperatorCode["i64_atomic_load32_u"] = 65046] = "i64_atomic_load32_u";
    OperatorCode[OperatorCode["i32_atomic_store"] = 65047] = "i32_atomic_store";
    OperatorCode[OperatorCode["i64_atomic_store"] = 65048] = "i64_atomic_store";
    OperatorCode[OperatorCode["i32_atomic_store8"] = 65049] = "i32_atomic_store8";
    OperatorCode[OperatorCode["i32_atomic_store16"] = 65050] = "i32_atomic_store16";
    OperatorCode[OperatorCode["i64_atomic_store8"] = 65051] = "i64_atomic_store8";
    OperatorCode[OperatorCode["i64_atomic_store16"] = 65052] = "i64_atomic_store16";
    OperatorCode[OperatorCode["i64_atomic_store32"] = 65053] = "i64_atomic_store32";
    OperatorCode[OperatorCode["i32_atomic_rmw_add"] = 65054] = "i32_atomic_rmw_add";
    OperatorCode[OperatorCode["i64_atomic_rmw_add"] = 65055] = "i64_atomic_rmw_add";
    OperatorCode[OperatorCode["i32_atomic_rmw8_add_u"] = 65056] = "i32_atomic_rmw8_add_u";
    OperatorCode[OperatorCode["i32_atomic_rmw16_add_u"] = 65057] = "i32_atomic_rmw16_add_u";
    OperatorCode[OperatorCode["i64_atomic_rmw8_add_u"] = 65058] = "i64_atomic_rmw8_add_u";
    OperatorCode[OperatorCode["i64_atomic_rmw16_add_u"] = 65059] = "i64_atomic_rmw16_add_u";
    OperatorCode[OperatorCode["i64_atomic_rmw32_add_u"] = 65060] = "i64_atomic_rmw32_add_u";
    OperatorCode[OperatorCode["i32_atomic_rmw_sub"] = 65061] = "i32_atomic_rmw_sub";
    OperatorCode[OperatorCode["i64_atomic_rmw_sub"] = 65062] = "i64_atomic_rmw_sub";
    OperatorCode[OperatorCode["i32_atomic_rmw8_sub_u"] = 65063] = "i32_atomic_rmw8_sub_u";
    OperatorCode[OperatorCode["i32_atomic_rmw16_sub_u"] = 65064] = "i32_atomic_rmw16_sub_u";
    OperatorCode[OperatorCode["i64_atomic_rmw8_sub_u"] = 65065] = "i64_atomic_rmw8_sub_u";
    OperatorCode[OperatorCode["i64_atomic_rmw16_sub_u"] = 65066] = "i64_atomic_rmw16_sub_u";
    OperatorCode[OperatorCode["i64_atomic_rmw32_sub_u"] = 65067] = "i64_atomic_rmw32_sub_u";
    OperatorCode[OperatorCode["i32_atomic_rmw_and"] = 65068] = "i32_atomic_rmw_and";
    OperatorCode[OperatorCode["i64_atomic_rmw_and"] = 65069] = "i64_atomic_rmw_and";
    OperatorCode[OperatorCode["i32_atomic_rmw8_and_u"] = 65070] = "i32_atomic_rmw8_and_u";
    OperatorCode[OperatorCode["i32_atomic_rmw16_and_u"] = 65071] = "i32_atomic_rmw16_and_u";
    OperatorCode[OperatorCode["i64_atomic_rmw8_and_u"] = 65072] = "i64_atomic_rmw8_and_u";
    OperatorCode[OperatorCode["i64_atomic_rmw16_and_u"] = 65073] = "i64_atomic_rmw16_and_u";
    OperatorCode[OperatorCode["i64_atomic_rmw32_and_u"] = 65074] = "i64_atomic_rmw32_and_u";
    OperatorCode[OperatorCode["i32_atomic_rmw_or"] = 65075] = "i32_atomic_rmw_or";
    OperatorCode[OperatorCode["i64_atomic_rmw_or"] = 65076] = "i64_atomic_rmw_or";
    OperatorCode[OperatorCode["i32_atomic_rmw8_or_u"] = 65077] = "i32_atomic_rmw8_or_u";
    OperatorCode[OperatorCode["i32_atomic_rmw16_or_u"] = 65078] = "i32_atomic_rmw16_or_u";
    OperatorCode[OperatorCode["i64_atomic_rmw8_or_u"] = 65079] = "i64_atomic_rmw8_or_u";
    OperatorCode[OperatorCode["i64_atomic_rmw16_or_u"] = 65080] = "i64_atomic_rmw16_or_u";
    OperatorCode[OperatorCode["i64_atomic_rmw32_or_u"] = 65081] = "i64_atomic_rmw32_or_u";
    OperatorCode[OperatorCode["i32_atomic_rmw_xor"] = 65082] = "i32_atomic_rmw_xor";
    OperatorCode[OperatorCode["i64_atomic_rmw_xor"] = 65083] = "i64_atomic_rmw_xor";
    OperatorCode[OperatorCode["i32_atomic_rmw8_xor_u"] = 65084] = "i32_atomic_rmw8_xor_u";
    OperatorCode[OperatorCode["i32_atomic_rmw16_xor_u"] = 65085] = "i32_atomic_rmw16_xor_u";
    OperatorCode[OperatorCode["i64_atomic_rmw8_xor_u"] = 65086] = "i64_atomic_rmw8_xor_u";
    OperatorCode[OperatorCode["i64_atomic_rmw16_xor_u"] = 65087] = "i64_atomic_rmw16_xor_u";
    OperatorCode[OperatorCode["i64_atomic_rmw32_xor_u"] = 65088] = "i64_atomic_rmw32_xor_u";
    OperatorCode[OperatorCode["i32_atomic_rmw_xchg"] = 65089] = "i32_atomic_rmw_xchg";
    OperatorCode[OperatorCode["i64_atomic_rmw_xchg"] = 65090] = "i64_atomic_rmw_xchg";
    OperatorCode[OperatorCode["i32_atomic_rmw8_xchg_u"] = 65091] = "i32_atomic_rmw8_xchg_u";
    OperatorCode[OperatorCode["i32_atomic_rmw16_xchg_u"] = 65092] = "i32_atomic_rmw16_xchg_u";
    OperatorCode[OperatorCode["i64_atomic_rmw8_xchg_u"] = 65093] = "i64_atomic_rmw8_xchg_u";
    OperatorCode[OperatorCode["i64_atomic_rmw16_xchg_u"] = 65094] = "i64_atomic_rmw16_xchg_u";
    OperatorCode[OperatorCode["i64_atomic_rmw32_xchg_u"] = 65095] = "i64_atomic_rmw32_xchg_u";
    OperatorCode[OperatorCode["i32_atomic_rmw_cmpxchg"] = 65096] = "i32_atomic_rmw_cmpxchg";
    OperatorCode[OperatorCode["i64_atomic_rmw_cmpxchg"] = 65097] = "i64_atomic_rmw_cmpxchg";
    OperatorCode[OperatorCode["i32_atomic_rmw8_cmpxchg_u"] = 65098] = "i32_atomic_rmw8_cmpxchg_u";
    OperatorCode[OperatorCode["i32_atomic_rmw16_cmpxchg_u"] = 65099] = "i32_atomic_rmw16_cmpxchg_u";
    OperatorCode[OperatorCode["i64_atomic_rmw8_cmpxchg_u"] = 65100] = "i64_atomic_rmw8_cmpxchg_u";
    OperatorCode[OperatorCode["i64_atomic_rmw16_cmpxchg_u"] = 65101] = "i64_atomic_rmw16_cmpxchg_u";
    OperatorCode[OperatorCode["i64_atomic_rmw32_cmpxchg_u"] = 65102] = "i64_atomic_rmw32_cmpxchg_u";
    // Pre-standard SIMD load names, kept for backwards compatibility.
    /** @deprecated Use v128_load8x8_s. */
    OperatorCode[OperatorCode["i16x8_load8x8_s"] = 1036289] = "i16x8_load8x8_s";
    /** @deprecated Use v128_load8x8_u. */
    OperatorCode[OperatorCode["i16x8_load8x8_u"] = 1036290] = "i16x8_load8x8_u";
    /** @deprecated Use v128_load16x4_s. */
    OperatorCode[OperatorCode["i32x4_load16x4_s"] = 1036291] = "i32x4_load16x4_s";
    /** @deprecated Use v128_load16x4_u. */
    OperatorCode[OperatorCode["i32x4_load16x4_u"] = 1036292] = "i32x4_load16x4_u";
    /** @deprecated Use v128_load32x2_s. */
    OperatorCode[OperatorCode["i64x2_load32x2_s"] = 1036293] = "i64x2_load32x2_s";
    /** @deprecated Use v128_load32x2_u. */
    OperatorCode[OperatorCode["i64x2_load32x2_u"] = 1036294] = "i64x2_load32x2_u";
    /** @deprecated Use v128_load8_splat. */
    OperatorCode[OperatorCode["v8x16_load_splat"] = 1036295] = "v8x16_load_splat";
    /** @deprecated Use v128_load16_splat. */
    OperatorCode[OperatorCode["v16x8_load_splat"] = 1036296] = "v16x8_load_splat";
    /** @deprecated Use v128_load32_splat. */
    OperatorCode[OperatorCode["v32x4_load_splat"] = 1036297] = "v32x4_load_splat";
    /** @deprecated Use v128_load64_splat. */
    OperatorCode[OperatorCode["v64x2_load_splat"] = 1036298] = "v64x2_load_splat";
    OperatorCode[OperatorCode["v128_load"] = 1036288] = "v128_load";
    OperatorCode[OperatorCode["v128_load8x8_s"] = 1036289] = "v128_load8x8_s";
    OperatorCode[OperatorCode["v128_load8x8_u"] = 1036290] = "v128_load8x8_u";
    OperatorCode[OperatorCode["v128_load16x4_s"] = 1036291] = "v128_load16x4_s";
    OperatorCode[OperatorCode["v128_load16x4_u"] = 1036292] = "v128_load16x4_u";
    OperatorCode[OperatorCode["v128_load32x2_s"] = 1036293] = "v128_load32x2_s";
    OperatorCode[OperatorCode["v128_load32x2_u"] = 1036294] = "v128_load32x2_u";
    OperatorCode[OperatorCode["v128_load8_splat"] = 1036295] = "v128_load8_splat";
    OperatorCode[OperatorCode["v128_load16_splat"] = 1036296] = "v128_load16_splat";
    OperatorCode[OperatorCode["v128_load32_splat"] = 1036297] = "v128_load32_splat";
    OperatorCode[OperatorCode["v128_load64_splat"] = 1036298] = "v128_load64_splat";
    OperatorCode[OperatorCode["v128_store"] = 1036299] = "v128_store";
    OperatorCode[OperatorCode["v128_const"] = 1036300] = "v128_const";
    OperatorCode[OperatorCode["i8x16_shuffle"] = 1036301] = "i8x16_shuffle";
    OperatorCode[OperatorCode["i8x16_swizzle"] = 1036302] = "i8x16_swizzle";
    OperatorCode[OperatorCode["i8x16_splat"] = 1036303] = "i8x16_splat";
    OperatorCode[OperatorCode["i16x8_splat"] = 1036304] = "i16x8_splat";
    OperatorCode[OperatorCode["i32x4_splat"] = 1036305] = "i32x4_splat";
    OperatorCode[OperatorCode["i64x2_splat"] = 1036306] = "i64x2_splat";
    OperatorCode[OperatorCode["f32x4_splat"] = 1036307] = "f32x4_splat";
    OperatorCode[OperatorCode["f64x2_splat"] = 1036308] = "f64x2_splat";
    OperatorCode[OperatorCode["i8x16_extract_lane_s"] = 1036309] = "i8x16_extract_lane_s";
    OperatorCode[OperatorCode["i8x16_extract_lane_u"] = 1036310] = "i8x16_extract_lane_u";
    OperatorCode[OperatorCode["i8x16_replace_lane"] = 1036311] = "i8x16_replace_lane";
    OperatorCode[OperatorCode["i16x8_extract_lane_s"] = 1036312] = "i16x8_extract_lane_s";
    OperatorCode[OperatorCode["i16x8_extract_lane_u"] = 1036313] = "i16x8_extract_lane_u";
    OperatorCode[OperatorCode["i16x8_replace_lane"] = 1036314] = "i16x8_replace_lane";
    OperatorCode[OperatorCode["i32x4_extract_lane"] = 1036315] = "i32x4_extract_lane";
    OperatorCode[OperatorCode["i32x4_replace_lane"] = 1036316] = "i32x4_replace_lane";
    OperatorCode[OperatorCode["i64x2_extract_lane"] = 1036317] = "i64x2_extract_lane";
    OperatorCode[OperatorCode["i64x2_replace_lane"] = 1036318] = "i64x2_replace_lane";
    OperatorCode[OperatorCode["f32x4_extract_lane"] = 1036319] = "f32x4_extract_lane";
    OperatorCode[OperatorCode["f32x4_replace_lane"] = 1036320] = "f32x4_replace_lane";
    OperatorCode[OperatorCode["f64x2_extract_lane"] = 1036321] = "f64x2_extract_lane";
    OperatorCode[OperatorCode["f64x2_replace_lane"] = 1036322] = "f64x2_replace_lane";
    OperatorCode[OperatorCode["i8x16_eq"] = 1036323] = "i8x16_eq";
    OperatorCode[OperatorCode["i8x16_ne"] = 1036324] = "i8x16_ne";
    OperatorCode[OperatorCode["i8x16_lt_s"] = 1036325] = "i8x16_lt_s";
    OperatorCode[OperatorCode["i8x16_lt_u"] = 1036326] = "i8x16_lt_u";
    OperatorCode[OperatorCode["i8x16_gt_s"] = 1036327] = "i8x16_gt_s";
    OperatorCode[OperatorCode["i8x16_gt_u"] = 1036328] = "i8x16_gt_u";
    OperatorCode[OperatorCode["i8x16_le_s"] = 1036329] = "i8x16_le_s";
    OperatorCode[OperatorCode["i8x16_le_u"] = 1036330] = "i8x16_le_u";
    OperatorCode[OperatorCode["i8x16_ge_s"] = 1036331] = "i8x16_ge_s";
    OperatorCode[OperatorCode["i8x16_ge_u"] = 1036332] = "i8x16_ge_u";
    OperatorCode[OperatorCode["i16x8_eq"] = 1036333] = "i16x8_eq";
    OperatorCode[OperatorCode["i16x8_ne"] = 1036334] = "i16x8_ne";
    OperatorCode[OperatorCode["i16x8_lt_s"] = 1036335] = "i16x8_lt_s";
    OperatorCode[OperatorCode["i16x8_lt_u"] = 1036336] = "i16x8_lt_u";
    OperatorCode[OperatorCode["i16x8_gt_s"] = 1036337] = "i16x8_gt_s";
    OperatorCode[OperatorCode["i16x8_gt_u"] = 1036338] = "i16x8_gt_u";
    OperatorCode[OperatorCode["i16x8_le_s"] = 1036339] = "i16x8_le_s";
    OperatorCode[OperatorCode["i16x8_le_u"] = 1036340] = "i16x8_le_u";
    OperatorCode[OperatorCode["i16x8_ge_s"] = 1036341] = "i16x8_ge_s";
    OperatorCode[OperatorCode["i16x8_ge_u"] = 1036342] = "i16x8_ge_u";
    OperatorCode[OperatorCode["i32x4_eq"] = 1036343] = "i32x4_eq";
    OperatorCode[OperatorCode["i32x4_ne"] = 1036344] = "i32x4_ne";
    OperatorCode[OperatorCode["i32x4_lt_s"] = 1036345] = "i32x4_lt_s";
    OperatorCode[OperatorCode["i32x4_lt_u"] = 1036346] = "i32x4_lt_u";
    OperatorCode[OperatorCode["i32x4_gt_s"] = 1036347] = "i32x4_gt_s";
    OperatorCode[OperatorCode["i32x4_gt_u"] = 1036348] = "i32x4_gt_u";
    OperatorCode[OperatorCode["i32x4_le_s"] = 1036349] = "i32x4_le_s";
    OperatorCode[OperatorCode["i32x4_le_u"] = 1036350] = "i32x4_le_u";
    OperatorCode[OperatorCode["i32x4_ge_s"] = 1036351] = "i32x4_ge_s";
    OperatorCode[OperatorCode["i32x4_ge_u"] = 1036352] = "i32x4_ge_u";
    OperatorCode[OperatorCode["f32x4_eq"] = 1036353] = "f32x4_eq";
    OperatorCode[OperatorCode["f32x4_ne"] = 1036354] = "f32x4_ne";
    OperatorCode[OperatorCode["f32x4_lt"] = 1036355] = "f32x4_lt";
    OperatorCode[OperatorCode["f32x4_gt"] = 1036356] = "f32x4_gt";
    OperatorCode[OperatorCode["f32x4_le"] = 1036357] = "f32x4_le";
    OperatorCode[OperatorCode["f32x4_ge"] = 1036358] = "f32x4_ge";
    OperatorCode[OperatorCode["f64x2_eq"] = 1036359] = "f64x2_eq";
    OperatorCode[OperatorCode["f64x2_ne"] = 1036360] = "f64x2_ne";
    OperatorCode[OperatorCode["f64x2_lt"] = 1036361] = "f64x2_lt";
    OperatorCode[OperatorCode["f64x2_gt"] = 1036362] = "f64x2_gt";
    OperatorCode[OperatorCode["f64x2_le"] = 1036363] = "f64x2_le";
    OperatorCode[OperatorCode["f64x2_ge"] = 1036364] = "f64x2_ge";
    OperatorCode[OperatorCode["v128_not"] = 1036365] = "v128_not";
    OperatorCode[OperatorCode["v128_and"] = 1036366] = "v128_and";
    OperatorCode[OperatorCode["v128_andnot"] = 1036367] = "v128_andnot";
    OperatorCode[OperatorCode["v128_or"] = 1036368] = "v128_or";
    OperatorCode[OperatorCode["v128_xor"] = 1036369] = "v128_xor";
    OperatorCode[OperatorCode["v128_bitselect"] = 1036370] = "v128_bitselect";
    OperatorCode[OperatorCode["v128_any_true"] = 1036371] = "v128_any_true";
    OperatorCode[OperatorCode["v128_load8_lane"] = 1036372] = "v128_load8_lane";
    OperatorCode[OperatorCode["v128_load16_lane"] = 1036373] = "v128_load16_lane";
    OperatorCode[OperatorCode["v128_load32_lane"] = 1036374] = "v128_load32_lane";
    OperatorCode[OperatorCode["v128_load64_lane"] = 1036375] = "v128_load64_lane";
    OperatorCode[OperatorCode["v128_store8_lane"] = 1036376] = "v128_store8_lane";
    OperatorCode[OperatorCode["v128_store16_lane"] = 1036377] = "v128_store16_lane";
    OperatorCode[OperatorCode["v128_store32_lane"] = 1036378] = "v128_store32_lane";
    OperatorCode[OperatorCode["v128_store64_lane"] = 1036379] = "v128_store64_lane";
    OperatorCode[OperatorCode["v128_load32_zero"] = 1036380] = "v128_load32_zero";
    OperatorCode[OperatorCode["v128_load64_zero"] = 1036381] = "v128_load64_zero";
    OperatorCode[OperatorCode["f32x4_demote_f64x2_zero"] = 1036382] = "f32x4_demote_f64x2_zero";
    OperatorCode[OperatorCode["f64x2_promote_low_f32x4"] = 1036383] = "f64x2_promote_low_f32x4";
    OperatorCode[OperatorCode["i8x16_abs"] = 1036384] = "i8x16_abs";
    OperatorCode[OperatorCode["i8x16_neg"] = 1036385] = "i8x16_neg";
    OperatorCode[OperatorCode["i8x16_popcnt"] = 1036386] = "i8x16_popcnt";
    OperatorCode[OperatorCode["i8x16_all_true"] = 1036387] = "i8x16_all_true";
    OperatorCode[OperatorCode["i8x16_bitmask"] = 1036388] = "i8x16_bitmask";
    OperatorCode[OperatorCode["i8x16_narrow_i16x8_s"] = 1036389] = "i8x16_narrow_i16x8_s";
    OperatorCode[OperatorCode["i8x16_narrow_i16x8_u"] = 1036390] = "i8x16_narrow_i16x8_u";
    OperatorCode[OperatorCode["f32x4_ceil"] = 1036391] = "f32x4_ceil";
    OperatorCode[OperatorCode["f32x4_floor"] = 1036392] = "f32x4_floor";
    OperatorCode[OperatorCode["f32x4_trunc"] = 1036393] = "f32x4_trunc";
    OperatorCode[OperatorCode["f32x4_nearest"] = 1036394] = "f32x4_nearest";
    OperatorCode[OperatorCode["i8x16_shl"] = 1036395] = "i8x16_shl";
    OperatorCode[OperatorCode["i8x16_shr_s"] = 1036396] = "i8x16_shr_s";
    OperatorCode[OperatorCode["i8x16_shr_u"] = 1036397] = "i8x16_shr_u";
    OperatorCode[OperatorCode["i8x16_add"] = 1036398] = "i8x16_add";
    OperatorCode[OperatorCode["i8x16_add_sat_s"] = 1036399] = "i8x16_add_sat_s";
    OperatorCode[OperatorCode["i8x16_add_sat_u"] = 1036400] = "i8x16_add_sat_u";
    OperatorCode[OperatorCode["i8x16_sub"] = 1036401] = "i8x16_sub";
    OperatorCode[OperatorCode["i8x16_sub_sat_s"] = 1036402] = "i8x16_sub_sat_s";
    OperatorCode[OperatorCode["i8x16_sub_sat_u"] = 1036403] = "i8x16_sub_sat_u";
    OperatorCode[OperatorCode["f64x2_ceil"] = 1036404] = "f64x2_ceil";
    OperatorCode[OperatorCode["f64x2_floor"] = 1036405] = "f64x2_floor";
    OperatorCode[OperatorCode["i8x16_min_s"] = 1036406] = "i8x16_min_s";
    OperatorCode[OperatorCode["i8x16_min_u"] = 1036407] = "i8x16_min_u";
    OperatorCode[OperatorCode["i8x16_max_s"] = 1036408] = "i8x16_max_s";
    OperatorCode[OperatorCode["i8x16_max_u"] = 1036409] = "i8x16_max_u";
    OperatorCode[OperatorCode["f64x2_trunc"] = 1036410] = "f64x2_trunc";
    OperatorCode[OperatorCode["i8x16_avgr_u"] = 1036411] = "i8x16_avgr_u";
    OperatorCode[OperatorCode["i16x8_extadd_pairwise_i8x16_s"] = 1036412] = "i16x8_extadd_pairwise_i8x16_s";
    OperatorCode[OperatorCode["i16x8_extadd_pairwise_i8x16_u"] = 1036413] = "i16x8_extadd_pairwise_i8x16_u";
    OperatorCode[OperatorCode["i32x4_extadd_pairwise_i16x8_s"] = 1036414] = "i32x4_extadd_pairwise_i16x8_s";
    OperatorCode[OperatorCode["i32x4_extadd_pairwise_i16x8_u"] = 1036415] = "i32x4_extadd_pairwise_i16x8_u";
    OperatorCode[OperatorCode["i16x8_abs"] = 1036416] = "i16x8_abs";
    OperatorCode[OperatorCode["i16x8_neg"] = 1036417] = "i16x8_neg";
    OperatorCode[OperatorCode["i16x8_q15mulr_sat_s"] = 1036418] = "i16x8_q15mulr_sat_s";
    OperatorCode[OperatorCode["i16x8_all_true"] = 1036419] = "i16x8_all_true";
    OperatorCode[OperatorCode["i16x8_bitmask"] = 1036420] = "i16x8_bitmask";
    OperatorCode[OperatorCode["i16x8_narrow_i32x4_s"] = 1036421] = "i16x8_narrow_i32x4_s";
    OperatorCode[OperatorCode["i16x8_narrow_i32x4_u"] = 1036422] = "i16x8_narrow_i32x4_u";
    OperatorCode[OperatorCode["i16x8_extend_low_i8x16_s"] = 1036423] = "i16x8_extend_low_i8x16_s";
    OperatorCode[OperatorCode["i16x8_extend_high_i8x16_s"] = 1036424] = "i16x8_extend_high_i8x16_s";
    OperatorCode[OperatorCode["i16x8_extend_low_i8x16_u"] = 1036425] = "i16x8_extend_low_i8x16_u";
    OperatorCode[OperatorCode["i16x8_extend_high_i8x16_u"] = 1036426] = "i16x8_extend_high_i8x16_u";
    OperatorCode[OperatorCode["i16x8_shl"] = 1036427] = "i16x8_shl";
    OperatorCode[OperatorCode["i16x8_shr_s"] = 1036428] = "i16x8_shr_s";
    OperatorCode[OperatorCode["i16x8_shr_u"] = 1036429] = "i16x8_shr_u";
    OperatorCode[OperatorCode["i16x8_add"] = 1036430] = "i16x8_add";
    OperatorCode[OperatorCode["i16x8_add_sat_s"] = 1036431] = "i16x8_add_sat_s";
    OperatorCode[OperatorCode["i16x8_add_sat_u"] = 1036432] = "i16x8_add_sat_u";
    OperatorCode[OperatorCode["i16x8_sub"] = 1036433] = "i16x8_sub";
    OperatorCode[OperatorCode["i16x8_sub_sat_s"] = 1036434] = "i16x8_sub_sat_s";
    OperatorCode[OperatorCode["i16x8_sub_sat_u"] = 1036435] = "i16x8_sub_sat_u";
    OperatorCode[OperatorCode["f64x2_nearest"] = 1036436] = "f64x2_nearest";
    OperatorCode[OperatorCode["i16x8_mul"] = 1036437] = "i16x8_mul";
    OperatorCode[OperatorCode["i16x8_min_s"] = 1036438] = "i16x8_min_s";
    OperatorCode[OperatorCode["i16x8_min_u"] = 1036439] = "i16x8_min_u";
    OperatorCode[OperatorCode["i16x8_max_s"] = 1036440] = "i16x8_max_s";
    OperatorCode[OperatorCode["i16x8_max_u"] = 1036441] = "i16x8_max_u";
    OperatorCode[OperatorCode["i16x8_avgr_u"] = 1036443] = "i16x8_avgr_u";
    OperatorCode[OperatorCode["i16x8_extmul_low_i8x16_s"] = 1036444] = "i16x8_extmul_low_i8x16_s";
    OperatorCode[OperatorCode["i16x8_extmul_high_i8x16_s"] = 1036445] = "i16x8_extmul_high_i8x16_s";
    OperatorCode[OperatorCode["i16x8_extmul_low_i8x16_u"] = 1036446] = "i16x8_extmul_low_i8x16_u";
    OperatorCode[OperatorCode["i16x8_extmul_high_i8x16_u"] = 1036447] = "i16x8_extmul_high_i8x16_u";
    OperatorCode[OperatorCode["i32x4_abs"] = 1036448] = "i32x4_abs";
    OperatorCode[OperatorCode["i32x4_neg"] = 1036449] = "i32x4_neg";
    OperatorCode[OperatorCode["i32x4_all_true"] = 1036451] = "i32x4_all_true";
    OperatorCode[OperatorCode["i32x4_bitmask"] = 1036452] = "i32x4_bitmask";
    OperatorCode[OperatorCode["i32x4_extend_low_i16x8_s"] = 1036455] = "i32x4_extend_low_i16x8_s";
    OperatorCode[OperatorCode["i32x4_extend_high_i16x8_s"] = 1036456] = "i32x4_extend_high_i16x8_s";
    OperatorCode[OperatorCode["i32x4_extend_low_i16x8_u"] = 1036457] = "i32x4_extend_low_i16x8_u";
    OperatorCode[OperatorCode["i32x4_extend_high_i16x8_u"] = 1036458] = "i32x4_extend_high_i16x8_u";
    OperatorCode[OperatorCode["i32x4_shl"] = 1036459] = "i32x4_shl";
    OperatorCode[OperatorCode["i32x4_shr_s"] = 1036460] = "i32x4_shr_s";
    OperatorCode[OperatorCode["i32x4_shr_u"] = 1036461] = "i32x4_shr_u";
    OperatorCode[OperatorCode["i32x4_add"] = 1036462] = "i32x4_add";
    OperatorCode[OperatorCode["i32x4_sub"] = 1036465] = "i32x4_sub";
    OperatorCode[OperatorCode["i32x4_mul"] = 1036469] = "i32x4_mul";
    OperatorCode[OperatorCode["i32x4_min_s"] = 1036470] = "i32x4_min_s";
    OperatorCode[OperatorCode["i32x4_min_u"] = 1036471] = "i32x4_min_u";
    OperatorCode[OperatorCode["i32x4_max_s"] = 1036472] = "i32x4_max_s";
    OperatorCode[OperatorCode["i32x4_max_u"] = 1036473] = "i32x4_max_u";
    OperatorCode[OperatorCode["i32x4_dot_i16x8_s"] = 1036474] = "i32x4_dot_i16x8_s";
    OperatorCode[OperatorCode["i32x4_extmul_low_i16x8_s"] = 1036476] = "i32x4_extmul_low_i16x8_s";
    OperatorCode[OperatorCode["i32x4_extmul_high_i16x8_s"] = 1036477] = "i32x4_extmul_high_i16x8_s";
    OperatorCode[OperatorCode["i32x4_extmul_low_i16x8_u"] = 1036478] = "i32x4_extmul_low_i16x8_u";
    OperatorCode[OperatorCode["i32x4_extmul_high_i16x8_u"] = 1036479] = "i32x4_extmul_high_i16x8_u";
    OperatorCode[OperatorCode["i64x2_abs"] = 1036480] = "i64x2_abs";
    OperatorCode[OperatorCode["i64x2_neg"] = 1036481] = "i64x2_neg";
    OperatorCode[OperatorCode["i64x2_all_true"] = 1036483] = "i64x2_all_true";
    OperatorCode[OperatorCode["i64x2_bitmask"] = 1036484] = "i64x2_bitmask";
    OperatorCode[OperatorCode["i64x2_extend_low_i32x4_s"] = 1036487] = "i64x2_extend_low_i32x4_s";
    OperatorCode[OperatorCode["i64x2_extend_high_i32x4_s"] = 1036488] = "i64x2_extend_high_i32x4_s";
    OperatorCode[OperatorCode["i64x2_extend_low_i32x4_u"] = 1036489] = "i64x2_extend_low_i32x4_u";
    OperatorCode[OperatorCode["i64x2_extend_high_i32x4_u"] = 1036490] = "i64x2_extend_high_i32x4_u";
    OperatorCode[OperatorCode["i64x2_shl"] = 1036491] = "i64x2_shl";
    OperatorCode[OperatorCode["i64x2_shr_s"] = 1036492] = "i64x2_shr_s";
    OperatorCode[OperatorCode["i64x2_shr_u"] = 1036493] = "i64x2_shr_u";
    OperatorCode[OperatorCode["i64x2_add"] = 1036494] = "i64x2_add";
    OperatorCode[OperatorCode["i64x2_sub"] = 1036497] = "i64x2_sub";
    OperatorCode[OperatorCode["i64x2_mul"] = 1036501] = "i64x2_mul";
    OperatorCode[OperatorCode["i64x2_eq"] = 1036502] = "i64x2_eq";
    OperatorCode[OperatorCode["i64x2_ne"] = 1036503] = "i64x2_ne";
    OperatorCode[OperatorCode["i64x2_lt_s"] = 1036504] = "i64x2_lt_s";
    OperatorCode[OperatorCode["i64x2_gt_s"] = 1036505] = "i64x2_gt_s";
    OperatorCode[OperatorCode["i64x2_le_s"] = 1036506] = "i64x2_le_s";
    OperatorCode[OperatorCode["i64x2_ge_s"] = 1036507] = "i64x2_ge_s";
    OperatorCode[OperatorCode["i64x2_extmul_low_i32x4_s"] = 1036508] = "i64x2_extmul_low_i32x4_s";
    OperatorCode[OperatorCode["i64x2_extmul_high_i32x4_s"] = 1036509] = "i64x2_extmul_high_i32x4_s";
    OperatorCode[OperatorCode["i64x2_extmul_low_i32x4_u"] = 1036510] = "i64x2_extmul_low_i32x4_u";
    OperatorCode[OperatorCode["i64x2_extmul_high_i32x4_u"] = 1036511] = "i64x2_extmul_high_i32x4_u";
    OperatorCode[OperatorCode["f32x4_abs"] = 1036512] = "f32x4_abs";
    OperatorCode[OperatorCode["f32x4_neg"] = 1036513] = "f32x4_neg";
    OperatorCode[OperatorCode["f32x4_sqrt"] = 1036515] = "f32x4_sqrt";
    OperatorCode[OperatorCode["f32x4_add"] = 1036516] = "f32x4_add";
    OperatorCode[OperatorCode["f32x4_sub"] = 1036517] = "f32x4_sub";
    OperatorCode[OperatorCode["f32x4_mul"] = 1036518] = "f32x4_mul";
    OperatorCode[OperatorCode["f32x4_div"] = 1036519] = "f32x4_div";
    OperatorCode[OperatorCode["f32x4_min"] = 1036520] = "f32x4_min";
    OperatorCode[OperatorCode["f32x4_max"] = 1036521] = "f32x4_max";
    OperatorCode[OperatorCode["f32x4_pmin"] = 1036522] = "f32x4_pmin";
    OperatorCode[OperatorCode["f32x4_pmax"] = 1036523] = "f32x4_pmax";
    OperatorCode[OperatorCode["f64x2_abs"] = 1036524] = "f64x2_abs";
    OperatorCode[OperatorCode["f64x2_neg"] = 1036525] = "f64x2_neg";
    OperatorCode[OperatorCode["f64x2_sqrt"] = 1036527] = "f64x2_sqrt";
    OperatorCode[OperatorCode["f64x2_add"] = 1036528] = "f64x2_add";
    OperatorCode[OperatorCode["f64x2_sub"] = 1036529] = "f64x2_sub";
    OperatorCode[OperatorCode["f64x2_mul"] = 1036530] = "f64x2_mul";
    OperatorCode[OperatorCode["f64x2_div"] = 1036531] = "f64x2_div";
    OperatorCode[OperatorCode["f64x2_min"] = 1036532] = "f64x2_min";
    OperatorCode[OperatorCode["f64x2_max"] = 1036533] = "f64x2_max";
    OperatorCode[OperatorCode["f64x2_pmin"] = 1036534] = "f64x2_pmin";
    OperatorCode[OperatorCode["f64x2_pmax"] = 1036535] = "f64x2_pmax";
    OperatorCode[OperatorCode["i32x4_trunc_sat_f32x4_s"] = 1036536] = "i32x4_trunc_sat_f32x4_s";
    OperatorCode[OperatorCode["i32x4_trunc_sat_f32x4_u"] = 1036537] = "i32x4_trunc_sat_f32x4_u";
    OperatorCode[OperatorCode["f32x4_convert_i32x4_s"] = 1036538] = "f32x4_convert_i32x4_s";
    OperatorCode[OperatorCode["f32x4_convert_i32x4_u"] = 1036539] = "f32x4_convert_i32x4_u";
    OperatorCode[OperatorCode["i32x4_trunc_sat_f64x2_s_zero"] = 1036540] = "i32x4_trunc_sat_f64x2_s_zero";
    OperatorCode[OperatorCode["i32x4_trunc_sat_f64x2_u_zero"] = 1036541] = "i32x4_trunc_sat_f64x2_u_zero";
    OperatorCode[OperatorCode["f64x2_convert_low_i32x4_s"] = 1036542] = "f64x2_convert_low_i32x4_s";
    OperatorCode[OperatorCode["f64x2_convert_low_i32x4_u"] = 1036543] = "f64x2_convert_low_i32x4_u";
    // Relaxed SIMD
    OperatorCode[OperatorCode["i8x16_relaxed_swizzle"] = 1036544] = "i8x16_relaxed_swizzle";
    OperatorCode[OperatorCode["i32x4_relaxed_trunc_f32x4_s"] = 1036545] = "i32x4_relaxed_trunc_f32x4_s";
    OperatorCode[OperatorCode["i32x4_relaxed_trunc_f32x4_u"] = 1036546] = "i32x4_relaxed_trunc_f32x4_u";
    OperatorCode[OperatorCode["i32x4_relaxed_trunc_f64x2_s_zero"] = 1036547] = "i32x4_relaxed_trunc_f64x2_s_zero";
    OperatorCode[OperatorCode["i32x4_relaxed_trunc_f64x2_u_zero"] = 1036548] = "i32x4_relaxed_trunc_f64x2_u_zero";
    OperatorCode[OperatorCode["f32x4_relaxed_madd"] = 1036549] = "f32x4_relaxed_madd";
    OperatorCode[OperatorCode["f32x4_relaxed_nmadd"] = 1036550] = "f32x4_relaxed_nmadd";
    OperatorCode[OperatorCode["f64x2_relaxed_madd"] = 1036551] = "f64x2_relaxed_madd";
    OperatorCode[OperatorCode["f64x2_relaxed_nmadd"] = 1036552] = "f64x2_relaxed_nmadd";
    OperatorCode[OperatorCode["i8x16_relaxed_laneselect"] = 1036553] = "i8x16_relaxed_laneselect";
    OperatorCode[OperatorCode["i16x8_relaxed_laneselect"] = 1036554] = "i16x8_relaxed_laneselect";
    OperatorCode[OperatorCode["i32x4_relaxed_laneselect"] = 1036555] = "i32x4_relaxed_laneselect";
    OperatorCode[OperatorCode["i64x2_relaxed_laneselect"] = 1036556] = "i64x2_relaxed_laneselect";
    OperatorCode[OperatorCode["f32x4_relaxed_min"] = 1036557] = "f32x4_relaxed_min";
    OperatorCode[OperatorCode["f32x4_relaxed_max"] = 1036558] = "f32x4_relaxed_max";
    OperatorCode[OperatorCode["f64x2_relaxed_min"] = 1036559] = "f64x2_relaxed_min";
    OperatorCode[OperatorCode["f64x2_relaxed_max"] = 1036560] = "f64x2_relaxed_max";
    OperatorCode[OperatorCode["i16x8_relaxed_q15mulr_s"] = 1036561] = "i16x8_relaxed_q15mulr_s";
    OperatorCode[OperatorCode["i16x8_relaxed_dot_i8x16_i7x16_s"] = 1036562] = "i16x8_relaxed_dot_i8x16_i7x16_s";
    OperatorCode[OperatorCode["i32x4_relaxed_dot_i8x16_i7x16_add_s"] = 1036563] = "i32x4_relaxed_dot_i8x16_i7x16_add_s";
    // GC proposal.
    OperatorCode[OperatorCode["struct_new"] = 64256] = "struct_new";
    OperatorCode[OperatorCode["struct_new_default"] = 64257] = "struct_new_default";
    OperatorCode[OperatorCode["struct_get"] = 64258] = "struct_get";
    OperatorCode[OperatorCode["struct_get_s"] = 64259] = "struct_get_s";
    OperatorCode[OperatorCode["struct_get_u"] = 64260] = "struct_get_u";
    OperatorCode[OperatorCode["struct_set"] = 64261] = "struct_set";
    OperatorCode[OperatorCode["array_new"] = 64262] = "array_new";
    OperatorCode[OperatorCode["array_new_default"] = 64263] = "array_new_default";
    OperatorCode[OperatorCode["array_new_fixed"] = 64264] = "array_new_fixed";
    OperatorCode[OperatorCode["array_new_data"] = 64265] = "array_new_data";
    OperatorCode[OperatorCode["array_new_elem"] = 64266] = "array_new_elem";
    OperatorCode[OperatorCode["array_get"] = 64267] = "array_get";
    OperatorCode[OperatorCode["array_get_s"] = 64268] = "array_get_s";
    OperatorCode[OperatorCode["array_get_u"] = 64269] = "array_get_u";
    OperatorCode[OperatorCode["array_set"] = 64270] = "array_set";
    OperatorCode[OperatorCode["array_len"] = 64271] = "array_len";
    OperatorCode[OperatorCode["array_fill"] = 64272] = "array_fill";
    OperatorCode[OperatorCode["array_copy"] = 64273] = "array_copy";
    OperatorCode[OperatorCode["array_init_data"] = 64274] = "array_init_data";
    OperatorCode[OperatorCode["array_init_elem"] = 64275] = "array_init_elem";
    OperatorCode[OperatorCode["ref_test"] = 64276] = "ref_test";
    OperatorCode[OperatorCode["ref_test_null"] = 64277] = "ref_test_null";
    OperatorCode[OperatorCode["ref_cast"] = 64278] = "ref_cast";
    OperatorCode[OperatorCode["ref_cast_null"] = 64279] = "ref_cast_null";
    OperatorCode[OperatorCode["br_on_cast"] = 64280] = "br_on_cast";
    OperatorCode[OperatorCode["br_on_cast_fail"] = 64281] = "br_on_cast_fail";
    OperatorCode[OperatorCode["any_convert_extern"] = 64282] = "any_convert_extern";
    OperatorCode[OperatorCode["extern_convert_any"] = 64283] = "extern_convert_any";
    OperatorCode[OperatorCode["ref_i31"] = 64284] = "ref_i31";
    OperatorCode[OperatorCode["i31_get_s"] = 64285] = "i31_get_s";
    OperatorCode[OperatorCode["i31_get_u"] = 64286] = "i31_get_u";
})(OperatorCode = exports.OperatorCode || (exports.OperatorCode = {}));
exports.OperatorCodeNames = [
    "unreachable",
    "nop",
    "block",
    "loop",
    "if",
    "else",
    "try",
    "catch",
    "throw",
    "rethrow",
    "throw_ref",
    "end",
    "br",
    "br_if",
    "br_table",
    "return",
    "call",
    "call_indirect",
    "return_call",
    "return_call_indirect",
    "call_ref",
    "return_call_ref",
    undefined,
    "let",
    "delegate",
    "catch_all",
    "drop",
    "select",
    "select",
    undefined,
    undefined,
    "try_table",
    "local.get",
    "local.set",
    "local.tee",
    "global.get",
    "global.set",
    "table.get",
    "table.set",
    undefined,
    "i32.load",
    "i64.load",
    "f32.load",
    "f64.load",
    "i32.load8_s",
    "i32.load8_u",
    "i32.load16_s",
    "i32.load16_u",
    "i64.load8_s",
    "i64.load8_u",
    "i64.load16_s",
    "i64.load16_u",
    "i64.load32_s",
    "i64.load32_u",
    "i32.store",
    "i64.store",
    "f32.store",
    "f64.store",
    "i32.store8",
    "i32.store16",
    "i64.store8",
    "i64.store16",
    "i64.store32",
    "memory.size",
    "memory.grow",
    "i32.const",
    "i64.const",
    "f32.const",
    "f64.const",
    "i32.eqz",
    "i32.eq",
    "i32.ne",
    "i32.lt_s",
    "i32.lt_u",
    "i32.gt_s",
    "i32.gt_u",
    "i32.le_s",
    "i32.le_u",
    "i32.ge_s",
    "i32.ge_u",
    "i64.eqz",
    "i64.eq",
    "i64.ne",
    "i64.lt_s",
    "i64.lt_u",
    "i64.gt_s",
    "i64.gt_u",
    "i64.le_s",
    "i64.le_u",
    "i64.ge_s",
    "i64.ge_u",
    "f32.eq",
    "f32.ne",
    "f32.lt",
    "f32.gt",
    "f32.le",
    "f32.ge",
    "f64.eq",
    "f64.ne",
    "f64.lt",
    "f64.gt",
    "f64.le",
    "f64.ge",
    "i32.clz",
    "i32.ctz",
    "i32.popcnt",
    "i32.add",
    "i32.sub",
    "i32.mul",
    "i32.div_s",
    "i32.div_u",
    "i32.rem_s",
    "i32.rem_u",
    "i32.and",
    "i32.or",
    "i32.xor",
    "i32.shl",
    "i32.shr_s",
    "i32.shr_u",
    "i32.rotl",
    "i32.rotr",
    "i64.clz",
    "i64.ctz",
    "i64.popcnt",
    "i64.add",
    "i64.sub",
    "i64.mul",
    "i64.div_s",
    "i64.div_u",
    "i64.rem_s",
    "i64.rem_u",
    "i64.and",
    "i64.or",
    "i64.xor",
    "i64.shl",
    "i64.shr_s",
    "i64.shr_u",
    "i64.rotl",
    "i64.rotr",
    "f32.abs",
    "f32.neg",
    "f32.ceil",
    "f32.floor",
    "f32.trunc",
    "f32.nearest",
    "f32.sqrt",
    "f32.add",
    "f32.sub",
    "f32.mul",
    "f32.div",
    "f32.min",
    "f32.max",
    "f32.copysign",
    "f64.abs",
    "f64.neg",
    "f64.ceil",
    "f64.floor",
    "f64.trunc",
    "f64.nearest",
    "f64.sqrt",
    "f64.add",
    "f64.sub",
    "f64.mul",
    "f64.div",
    "f64.min",
    "f64.max",
    "f64.copysign",
    "i32.wrap_i64",
    "i32.trunc_f32_s",
    "i32.trunc_f32_u",
    "i32.trunc_f64_s",
    "i32.trunc_f64_u",
    "i64.extend_i32_s",
    "i64.extend_i32_u",
    "i64.trunc_f32_s",
    "i64.trunc_f32_u",
    "i64.trunc_f64_s",
    "i64.trunc_f64_u",
    "f32.convert_i32_s",
    "f32.convert_i32_u",
    "f32.convert_i64_s",
    "f32.convert_i64_u",
    "f32.demote_f64",
    "f64.convert_i32_s",
    "f64.convert_i32_u",
    "f64.convert_i64_s",
    "f64.convert_i64_u",
    "f64.promote_f32",
    "i32.reinterpret_f32",
    "i64.reinterpret_f64",
    "f32.reinterpret_i32",
    "f64.reinterpret_i64",
    "i32.extend8_s",
    "i32.extend16_s",
    "i64.extend8_s",
    "i64.extend16_s",
    "i64.extend32_s",
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    "ref.null",
    "ref.is_null",
    "ref.func",
    "ref.eq",
    "ref.as_non_null",
    "br_on_null",
    "br_on_non_null",
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
];
[
    "i32.trunc_sat_f32_s",
    "i32.trunc_sat_f32_u",
    "i32.trunc_sat_f64_s",
    "i32.trunc_sat_f64_u",
    "i64.trunc_sat_f32_s",
    "i64.trunc_sat_f32_u",
    "i64.trunc_sat_f64_s",
    "i64.trunc_sat_f64_u",
    "memory.init",
    "data.drop",
    "memory.copy",
    "memory.fill",
    "table.init",
    "elem.drop",
    "table.copy",
    "table.grow",
    "table.size",
    "table.fill",
    undefined,
    "i64.add128",
    "i64.sub128",
    "i64.mul_wide_s",
    "i64.mul_wide_u",
].forEach(function (s, i) {
    exports.OperatorCodeNames[0xfc00 | i] = s;
});
[
    "v128.load",
    "v128.load8x8_s",
    "v128.load8x8_u",
    "v128.load16x4_s",
    "v128.load16x4_u",
    "v128.load32x2_s",
    "v128.load32x2_u",
    "v128.load8_splat",
    "v128.load16_splat",
    "v128.load32_splat",
    "v128.load64_splat",
    "v128.store",
    "v128.const",
    "i8x16.shuffle",
    "i8x16.swizzle",
    "i8x16.splat",
    "i16x8.splat",
    "i32x4.splat",
    "i64x2.splat",
    "f32x4.splat",
    "f64x2.splat",
    "i8x16.extract_lane_s",
    "i8x16.extract_lane_u",
    "i8x16.replace_lane",
    "i16x8.extract_lane_s",
    "i16x8.extract_lane_u",
    "i16x8.replace_lane",
    "i32x4.extract_lane",
    "i32x4.replace_lane",
    "i64x2.extract_lane",
    "i64x2.replace_lane",
    "f32x4.extract_lane",
    "f32x4.replace_lane",
    "f64x2.extract_lane",
    "f64x2.replace_lane",
    "i8x16.eq",
    "i8x16.ne",
    "i8x16.lt_s",
    "i8x16.lt_u",
    "i8x16.gt_s",
    "i8x16.gt_u",
    "i8x16.le_s",
    "i8x16.le_u",
    "i8x16.ge_s",
    "i8x16.ge_u",
    "i16x8.eq",
    "i16x8.ne",
    "i16x8.lt_s",
    "i16x8.lt_u",
    "i16x8.gt_s",
    "i16x8.gt_u",
    "i16x8.le_s",
    "i16x8.le_u",
    "i16x8.ge_s",
    "i16x8.ge_u",
    "i32x4.eq",
    "i32x4.ne",
    "i32x4.lt_s",
    "i32x4.lt_u",
    "i32x4.gt_s",
    "i32x4.gt_u",
    "i32x4.le_s",
    "i32x4.le_u",
    "i32x4.ge_s",
    "i32x4.ge_u",
    "f32x4.eq",
    "f32x4.ne",
    "f32x4.lt",
    "f32x4.gt",
    "f32x4.le",
    "f32x4.ge",
    "f64x2.eq",
    "f64x2.ne",
    "f64x2.lt",
    "f64x2.gt",
    "f64x2.le",
    "f64x2.ge",
    "v128.not",
    "v128.and",
    "v128.andnot",
    "v128.or",
    "v128.xor",
    "v128.bitselect",
    "v128.any_true",
    "v128.load8_lane",
    "v128.load16_lane",
    "v128.load32_lane",
    "v128.load64_lane",
    "v128.store8_lane",
    "v128.store16_lane",
    "v128.store32_lane",
    "v128.store64_lane",
    "v128.load32_zero",
    "v128.load64_zero",
    "f32x4.demote_f64x2_zero",
    "f64x2.promote_low_f32x4",
    "i8x16.abs",
    "i8x16.neg",
    "i8x16_popcnt",
    "i8x16.all_true",
    "i8x16.bitmask",
    "i8x16.narrow_i16x8_s",
    "i8x16.narrow_i16x8_u",
    "f32x4.ceil",
    "f32x4.floor",
    "f32x4.trunc",
    "f32x4.nearest",
    "i8x16.shl",
    "i8x16.shr_s",
    "i8x16.shr_u",
    "i8x16.add",
    "i8x16.add_sat_s",
    "i8x16.add_sat_u",
    "i8x16.sub",
    "i8x16.sub_sat_s",
    "i8x16.sub_sat_u",
    "f64x2.ceil",
    "f64x2.floor",
    "i8x16.min_s",
    "i8x16.min_u",
    "i8x16.max_s",
    "i8x16.max_u",
    "f64x2.trunc",
    "i8x16.avgr_u",
    "i16x8.extadd_pairwise_i8x16_s",
    "i16x8.extadd_pairwise_i8x16_u",
    "i32x4.extadd_pairwise_i16x8_s",
    "i32x4.extadd_pairwise_i16x8_u",
    "i16x8.abs",
    "i16x8.neg",
    "i16x8.q15mulr_sat_s",
    "i16x8.all_true",
    "i16x8.bitmask",
    "i16x8.narrow_i32x4_s",
    "i16x8.narrow_i32x4_u",
    "i16x8.extend_low_i8x16_s",
    "i16x8.extend_high_i8x16_s",
    "i16x8.extend_low_i8x16_u",
    "i16x8.extend_high_i8x16_u",
    "i16x8.shl",
    "i16x8.shr_s",
    "i16x8.shr_u",
    "i16x8.add",
    "i16x8.add_sat_s",
    "i16x8.add_sat_u",
    "i16x8.sub",
    "i16x8.sub_sat_s",
    "i16x8.sub_sat_u",
    "f64x2.nearest",
    "i16x8.mul",
    "i16x8.min_s",
    "i16x8.min_u",
    "i16x8.max_s",
    "i16x8.max_u",
    undefined,
    "i16x8.avgr_u",
    "i16x8.extmul_low_i8x16_s",
    "i16x8.extmul_high_i8x16_s",
    "i16x8.extmul_low_i8x16_u",
    "i16x8.extmul_high_i8x16_u",
    "i32x4.abs",
    "i32x4.neg",
    undefined,
    "i32x4.all_true",
    "i32x4.bitmask",
    undefined,
    undefined,
    "i32x4.extend_low_i16x8_s",
    "i32x4.extend_high_i16x8_s",
    "i32x4.extend_low_i16x8_u",
    "i32x4.extend_high_i16x8_u",
    "i32x4.shl",
    "i32x4.shr_s",
    "i32x4.shr_u",
    "i32x4.add",
    undefined,
    undefined,
    "i32x4.sub",
    undefined,
    undefined,
    undefined,
    "i32x4.mul",
    "i32x4.min_s",
    "i32x4.min_u",
    "i32x4.max_s",
    "i32x4.max_u",
    "i32x4.dot_i16x8_s",
    undefined,
    "i32x4.extmul_low_i16x8_s",
    "i32x4.extmul_high_i16x8_s",
    "i32x4.extmul_low_i16x8_u",
    "i32x4.extmul_high_i16x8_u",
    "i64x2.abs",
    "i64x2.neg",
    undefined,
    "i64x2.all_true",
    "i64x2.bitmask",
    undefined,
    undefined,
    "i64x2.extend_low_i32x4_s",
    "i64x2.extend_high_i32x4_s",
    "i64x2.extend_low_i32x4_u",
    "i64x2.extend_high_i32x4_u",
    "i64x2.shl",
    "i64x2.shr_s",
    "i64x2.shr_u",
    "i64x2.add",
    undefined,
    undefined,
    "i64x2.sub",
    undefined,
    undefined,
    undefined,
    "i64x2.mul",
    "i64x2.eq",
    "i64x2.ne",
    "i64x2.lt_s",
    "i64x2.gt_s",
    "i64x2.le_s",
    "i64x2.ge_s",
    "i64x2.extmul_low_i32x4_s",
    "i64x2.extmul_high_i32x4_s",
    "i64x2.extmul_low_i32x4_u",
    "i64x2.extmul_high_i32x4_u",
    "f32x4.abs",
    "f32x4.neg",
    undefined,
    "f32x4.sqrt",
    "f32x4.add",
    "f32x4.sub",
    "f32x4.mul",
    "f32x4.div",
    "f32x4.min",
    "f32x4.max",
    "f32x4.pmin",
    "f32x4.pmax",
    "f64x2.abs",
    "f64x2.neg",
    undefined,
    "f64x2.sqrt",
    "f64x2.add",
    "f64x2.sub",
    "f64x2.mul",
    "f64x2.div",
    "f64x2.min",
    "f64x2.max",
    "f64x2.pmin",
    "f64x2.pmax",
    "i32x4.trunc_sat_f32x4_s",
    "i32x4.trunc_sat_f32x4_u",
    "f32x4.convert_i32x4_s",
    "f32x4.convert_i32x4_u",
    "i32x4.trunc_sat_f64x2_s_zero",
    "i32x4.trunc_sat_f64x2_u_zero",
    "f64x2.convert_low_i32x4_s",
    "f64x2.convert_low_i32x4_u",
    "i8x16.relaxed_swizzle",
    "i32x4.relaxed_trunc_f32x4_s",
    "i32x4.relaxed_trunc_f32x4_u",
    "i32x4.relaxed_trunc_f64x2_s_zero",
    "i32x4.relaxed_trunc_f64x2_u_zero",
    "f32x4.relaxed_madd",
    "f32x4.relaxed_nmadd",
    "f64x2.relaxed_madd",
    "f64x2.relaxed_nmadd",
    "i8x16.relaxed_laneselect",
    "i16x8.relaxed_laneselect",
    "i32x4.relaxed_laneselect",
    "i64x2.relaxed_laneselect",
    "f32x4.relaxed_min",
    "f32x4.relaxed_max",
    "f64x2.relaxed_min",
    "f64x2.relaxed_max",
    "i16x8.relaxed_q15mulr_s",
    "i16x8.relaxed_dot_i8x16_i7x16_s",
    "i32x4.relaxed_dot_i8x16_i7x16_add_s",
].forEach(function (s, i) {
    exports.OperatorCodeNames[0xfd000 | i] = s;
});
[
    "memory.atomic.notify",
    "memory.atomic.wait32",
    "memory.atomic.wait64",
    "atomic.fence",
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    "i32.atomic.load",
    "i64.atomic.load",
    "i32.atomic.load8_u",
    "i32.atomic.load16_u",
    "i64.atomic.load8_u",
    "i64.atomic.load16_u",
    "i64.atomic.load32_u",
    "i32.atomic.store",
    "i64.atomic.store",
    "i32.atomic.store8",
    "i32.atomic.store16",
    "i64.atomic.store8",
    "i64.atomic.store16",
    "i64.atomic.store32",
    "i32.atomic.rmw.add",
    "i64.atomic.rmw.add",
    "i32.atomic.rmw8.add_u",
    "i32.atomic.rmw16.add_u",
    "i64.atomic.rmw8.add_u",
    "i64.atomic.rmw16.add_u",
    "i64.atomic.rmw32.add_u",
    "i32.atomic.rmw.sub",
    "i64.atomic.rmw.sub",
    "i32.atomic.rmw8.sub_u",
    "i32.atomic.rmw16.sub_u",
    "i64.atomic.rmw8.sub_u",
    "i64.atomic.rmw16.sub_u",
    "i64.atomic.rmw32.sub_u",
    "i32.atomic.rmw.and",
    "i64.atomic.rmw.and",
    "i32.atomic.rmw8.and_u",
    "i32.atomic.rmw16.and_u",
    "i64.atomic.rmw8.and_u",
    "i64.atomic.rmw16.and_u",
    "i64.atomic.rmw32.and_u",
    "i32.atomic.rmw.or",
    "i64.atomic.rmw.or",
    "i32.atomic.rmw8.or_u",
    "i32.atomic.rmw16.or_u",
    "i64.atomic.rmw8.or_u",
    "i64.atomic.rmw16.or_u",
    "i64.atomic.rmw32.or_u",
    "i32.atomic.rmw.xor",
    "i64.atomic.rmw.xor",
    "i32.atomic.rmw8.xor_u",
    "i32.atomic.rmw16.xor_u",
    "i64.atomic.rmw8.xor_u",
    "i64.atomic.rmw16.xor_u",
    "i64.atomic.rmw32.xor_u",
    "i32.atomic.rmw.xchg",
    "i64.atomic.rmw.xchg",
    "i32.atomic.rmw8.xchg_u",
    "i32.atomic.rmw16.xchg_u",
    "i64.atomic.rmw8.xchg_u",
    "i64.atomic.rmw16.xchg_u",
    "i64.atomic.rmw32.xchg_u",
    "i32.atomic.rmw.cmpxchg",
    "i64.atomic.rmw.cmpxchg",
    "i32.atomic.rmw8.cmpxchg_u",
    "i32.atomic.rmw16.cmpxchg_u",
    "i64.atomic.rmw8.cmpxchg_u",
    "i64.atomic.rmw16.cmpxchg_u",
    "i64.atomic.rmw32.cmpxchg_u",
].forEach(function (s, i) {
    exports.OperatorCodeNames[0xfe00 | i] = s;
});
[
    "struct.new",
    "struct.new_default",
    "struct.get",
    "struct.get_s",
    "struct.get_u",
    "struct.set",
    "array.new",
    "array.new_default",
    "array.new_fixed",
    "array.new_data",
    "array.new_elem",
    "array.get",
    "array.get_s",
    "array.get_u",
    "array.set",
    "array.len",
    "array.fill",
    "array.copy",
    "array.init_data",
    "array.init_elem",
    "ref.test",
    "ref.test",
    "ref.cast",
    "ref.cast",
    "br_on_cast",
    "br_on_cast_fail",
    "any.convert_extern",
    "extern.convert_any",
    "ref.i31",
    "i31.get_s",
    "i31.get_u",
].forEach(function (s, i) {
    exports.OperatorCodeNames[0xfb00 | i] = s;
});
var CompactImportKind;
(function (CompactImportKind) {
    CompactImportKind[CompactImportKind["ModuleName"] = 127] = "ModuleName";
    CompactImportKind[CompactImportKind["ModuleNameAndExternType"] = 126] = "ModuleNameAndExternType";
})(CompactImportKind || (CompactImportKind = {}));
var ExternalKind;
(function (ExternalKind) {
    ExternalKind[ExternalKind["Function"] = 0] = "Function";
    ExternalKind[ExternalKind["Table"] = 1] = "Table";
    ExternalKind[ExternalKind["Memory"] = 2] = "Memory";
    ExternalKind[ExternalKind["Global"] = 3] = "Global";
    ExternalKind[ExternalKind["Tag"] = 4] = "Tag";
})(ExternalKind = exports.ExternalKind || (exports.ExternalKind = {}));
var TypeKind;
(function (TypeKind) {
    TypeKind[TypeKind["unspecified"] = 0] = "unspecified";
    TypeKind[TypeKind["i32"] = -1] = "i32";
    TypeKind[TypeKind["i64"] = -2] = "i64";
    TypeKind[TypeKind["f32"] = -3] = "f32";
    TypeKind[TypeKind["f64"] = -4] = "f64";
    TypeKind[TypeKind["v128"] = -5] = "v128";
    TypeKind[TypeKind["i8"] = -8] = "i8";
    TypeKind[TypeKind["i16"] = -9] = "i16";
    TypeKind[TypeKind["nullexnref"] = -12] = "nullexnref";
    TypeKind[TypeKind["nullfuncref"] = -13] = "nullfuncref";
    TypeKind[TypeKind["nullref"] = -15] = "nullref";
    TypeKind[TypeKind["nullexternref"] = -14] = "nullexternref";
    TypeKind[TypeKind["funcref"] = -16] = "funcref";
    TypeKind[TypeKind["externref"] = -17] = "externref";
    TypeKind[TypeKind["anyref"] = -18] = "anyref";
    TypeKind[TypeKind["eqref"] = -19] = "eqref";
    TypeKind[TypeKind["i31ref"] = -20] = "i31ref";
    TypeKind[TypeKind["structref"] = -21] = "structref";
    TypeKind[TypeKind["arrayref"] = -22] = "arrayref";
    TypeKind[TypeKind["exnref"] = -23] = "exnref";
    TypeKind[TypeKind["ref"] = -28] = "ref";
    TypeKind[TypeKind["ref_null"] = -29] = "ref_null";
    TypeKind[TypeKind["func"] = -32] = "func";
    TypeKind[TypeKind["struct"] = -33] = "struct";
    TypeKind[TypeKind["array"] = -34] = "array";
    TypeKind[TypeKind["subtype"] = -48] = "subtype";
    TypeKind[TypeKind["subtype_final"] = -49] = "subtype_final";
    TypeKind[TypeKind["rec_group"] = -50] = "rec_group";
    TypeKind[TypeKind["empty_block_type"] = -64] = "empty_block_type";
})(TypeKind = exports.TypeKind || (exports.TypeKind = {}));
var FieldDef = /** @class */ (function () {
    function FieldDef() {
    }
    return FieldDef;
}());
exports.FieldDef = FieldDef;
var FuncDef = /** @class */ (function () {
    function FuncDef() {
    }
    return FuncDef;
}());
exports.FuncDef = FuncDef;
var Type = exports.Type = /** @class */ (function () {
    function Type(code) {
        this.code = code;
    }
    Object.defineProperty(Type.prototype, "isIndex", {
        get: function () {
            return this.code >= 0;
        },
        enumerable: false,
        configurable: true
    });
    Object.defineProperty(Type.prototype, "kind", {
        get: function () {
            return this.code >= 0 ? 0 /* TypeKind.unspecified */ : this.code;
        },
        enumerable: false,
        configurable: true
    });
    Object.defineProperty(Type.prototype, "index", {
        get: function () {
            return this.code < 0 ? -1 : this.code;
        },
        enumerable: false,
        configurable: true
    });
    // Convenience singletons.
    Type.funcref = new Type(-16 /* TypeKind.funcref */);
    Type.externref = new Type(-17 /* TypeKind.externref */);
    Type.exnref = new Type(-23 /* TypeKind.exnref */);
    return Type;
}());
var RefType = /** @class */ (function (_super) {
    __extends(RefType, _super);
    function RefType(kind, ref_index) {
        var _this = this;
        if (kind != -28 /* TypeKind.ref */ && kind !== -29 /* TypeKind.ref_null */) {
            throw new Error("Unexpected type kind: ".concat(kind, "}"));
        }
        _this = _super.call(this, kind) || this;
        _this.ref_index = ref_index;
        return _this;
    }
    Object.defineProperty(RefType.prototype, "isNullable", {
        get: function () {
            return this.kind == -29 /* TypeKind.ref_null */;
        },
        enumerable: false,
        configurable: true
    });
    return RefType;
}(Type));
exports.RefType = RefType;
var CatchHandlerKind;
(function (CatchHandlerKind) {
    CatchHandlerKind[CatchHandlerKind["Catch"] = 0] = "Catch";
    CatchHandlerKind[CatchHandlerKind["CatchRef"] = 1] = "CatchRef";
    CatchHandlerKind[CatchHandlerKind["CatchAll"] = 2] = "CatchAll";
    CatchHandlerKind[CatchHandlerKind["CatchAllRef"] = 3] = "CatchAllRef";
})(CatchHandlerKind = exports.CatchHandlerKind || (exports.CatchHandlerKind = {}));
var CatchHandler = /** @class */ (function () {
    function CatchHandler() {
    }
    return CatchHandler;
}());
exports.CatchHandler = CatchHandler;
var RelocType;
(function (RelocType) {
    RelocType[RelocType["FunctionIndex_LEB"] = 0] = "FunctionIndex_LEB";
    RelocType[RelocType["TableIndex_SLEB"] = 1] = "TableIndex_SLEB";
    RelocType[RelocType["TableIndex_I32"] = 2] = "TableIndex_I32";
    RelocType[RelocType["GlobalAddr_LEB"] = 3] = "GlobalAddr_LEB";
    RelocType[RelocType["GlobalAddr_SLEB"] = 4] = "GlobalAddr_SLEB";
    RelocType[RelocType["GlobalAddr_I32"] = 5] = "GlobalAddr_I32";
    RelocType[RelocType["TypeIndex_LEB"] = 6] = "TypeIndex_LEB";
    RelocType[RelocType["GlobalIndex_LEB"] = 7] = "GlobalIndex_LEB";
})(RelocType = exports.RelocType || (exports.RelocType = {}));
var LinkingType;
(function (LinkingType) {
    LinkingType[LinkingType["StackPointer"] = 1] = "StackPointer";
})(LinkingType = exports.LinkingType || (exports.LinkingType = {}));
var NameType;
(function (NameType) {
    NameType[NameType["Module"] = 0] = "Module";
    NameType[NameType["Function"] = 1] = "Function";
    NameType[NameType["Local"] = 2] = "Local";
    NameType[NameType["Label"] = 3] = "Label";
    NameType[NameType["Type"] = 4] = "Type";
    NameType[NameType["Table"] = 5] = "Table";
    NameType[NameType["Memory"] = 6] = "Memory";
    NameType[NameType["Global"] = 7] = "Global";
    NameType[NameType["Elem"] = 8] = "Elem";
    NameType[NameType["Data"] = 9] = "Data";
    NameType[NameType["Field"] = 10] = "Field";
    NameType[NameType["Tag"] = 11] = "Tag";
})(NameType = exports.NameType || (exports.NameType = {}));
var BinaryReaderState;
(function (BinaryReaderState) {
    BinaryReaderState[BinaryReaderState["ERROR"] = -1] = "ERROR";
    BinaryReaderState[BinaryReaderState["INITIAL"] = 0] = "INITIAL";
    BinaryReaderState[BinaryReaderState["BEGIN_WASM"] = 1] = "BEGIN_WASM";
    BinaryReaderState[BinaryReaderState["END_WASM"] = 2] = "END_WASM";
    BinaryReaderState[BinaryReaderState["BEGIN_SECTION"] = 3] = "BEGIN_SECTION";
    BinaryReaderState[BinaryReaderState["END_SECTION"] = 4] = "END_SECTION";
    BinaryReaderState[BinaryReaderState["SKIPPING_SECTION"] = 5] = "SKIPPING_SECTION";
    BinaryReaderState[BinaryReaderState["READING_SECTION_RAW_DATA"] = 6] = "READING_SECTION_RAW_DATA";
    BinaryReaderState[BinaryReaderState["SECTION_RAW_DATA"] = 7] = "SECTION_RAW_DATA";
    BinaryReaderState[BinaryReaderState["TYPE_SECTION_ENTRY"] = 11] = "TYPE_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["IMPORT_SECTION_ENTRY"] = 12] = "IMPORT_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["FUNCTION_SECTION_ENTRY"] = 13] = "FUNCTION_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["TABLE_SECTION_ENTRY"] = 14] = "TABLE_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["MEMORY_SECTION_ENTRY"] = 15] = "MEMORY_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["GLOBAL_SECTION_ENTRY"] = 16] = "GLOBAL_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["EXPORT_SECTION_ENTRY"] = 17] = "EXPORT_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["DATA_SECTION_ENTRY"] = 18] = "DATA_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["NAME_SECTION_ENTRY"] = 19] = "NAME_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["ELEMENT_SECTION_ENTRY"] = 20] = "ELEMENT_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["LINKING_SECTION_ENTRY"] = 21] = "LINKING_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["START_SECTION_ENTRY"] = 22] = "START_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["TAG_SECTION_ENTRY"] = 23] = "TAG_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["BEGIN_INIT_EXPRESSION_BODY"] = 25] = "BEGIN_INIT_EXPRESSION_BODY";
    BinaryReaderState[BinaryReaderState["INIT_EXPRESSION_OPERATOR"] = 26] = "INIT_EXPRESSION_OPERATOR";
    BinaryReaderState[BinaryReaderState["END_INIT_EXPRESSION_BODY"] = 27] = "END_INIT_EXPRESSION_BODY";
    BinaryReaderState[BinaryReaderState["BEGIN_FUNCTION_BODY"] = 28] = "BEGIN_FUNCTION_BODY";
    BinaryReaderState[BinaryReaderState["READING_FUNCTION_HEADER"] = 29] = "READING_FUNCTION_HEADER";
    BinaryReaderState[BinaryReaderState["CODE_OPERATOR"] = 30] = "CODE_OPERATOR";
    BinaryReaderState[BinaryReaderState["END_FUNCTION_BODY"] = 31] = "END_FUNCTION_BODY";
    BinaryReaderState[BinaryReaderState["SKIPPING_FUNCTION_BODY"] = 32] = "SKIPPING_FUNCTION_BODY";
    BinaryReaderState[BinaryReaderState["BEGIN_ELEMENT_SECTION_ENTRY"] = 33] = "BEGIN_ELEMENT_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["ELEMENT_SECTION_ENTRY_BODY"] = 34] = "ELEMENT_SECTION_ENTRY_BODY";
    BinaryReaderState[BinaryReaderState["END_ELEMENT_SECTION_ENTRY"] = 35] = "END_ELEMENT_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["BEGIN_DATA_SECTION_ENTRY"] = 36] = "BEGIN_DATA_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["DATA_SECTION_ENTRY_BODY"] = 37] = "DATA_SECTION_ENTRY_BODY";
    BinaryReaderState[BinaryReaderState["END_DATA_SECTION_ENTRY"] = 38] = "END_DATA_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["BEGIN_GLOBAL_SECTION_ENTRY"] = 39] = "BEGIN_GLOBAL_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["END_GLOBAL_SECTION_ENTRY"] = 40] = "END_GLOBAL_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["RELOC_SECTION_HEADER"] = 41] = "RELOC_SECTION_HEADER";
    BinaryReaderState[BinaryReaderState["RELOC_SECTION_ENTRY"] = 42] = "RELOC_SECTION_ENTRY";
    BinaryReaderState[BinaryReaderState["SOURCE_MAPPING_URL"] = 43] = "SOURCE_MAPPING_URL";
    BinaryReaderState[BinaryReaderState["BEGIN_OFFSET_EXPRESSION_BODY"] = 44] = "BEGIN_OFFSET_EXPRESSION_BODY";
    BinaryReaderState[BinaryReaderState["OFFSET_EXPRESSION_OPERATOR"] = 45] = "OFFSET_EXPRESSION_OPERATOR";
    BinaryReaderState[BinaryReaderState["END_OFFSET_EXPRESSION_BODY"] = 46] = "END_OFFSET_EXPRESSION_BODY";
    BinaryReaderState[BinaryReaderState["BEGIN_REC_GROUP"] = 47] = "BEGIN_REC_GROUP";
    BinaryReaderState[BinaryReaderState["END_REC_GROUP"] = 48] = "END_REC_GROUP";
    BinaryReaderState[BinaryReaderState["DATA_COUNT_SECTION_ENTRY"] = 49] = "DATA_COUNT_SECTION_ENTRY";
})(BinaryReaderState = exports.BinaryReaderState || (exports.BinaryReaderState = {}));
var DataSegmentType;
(function (DataSegmentType) {
    DataSegmentType[DataSegmentType["Active"] = 0] = "Active";
    DataSegmentType[DataSegmentType["Passive"] = 1] = "Passive";
    DataSegmentType[DataSegmentType["ActiveWithMemoryIndex"] = 2] = "ActiveWithMemoryIndex";
})(DataSegmentType || (DataSegmentType = {}));
function isActiveDataSegmentType(segmentType) {
    switch (segmentType) {
        case 0 /* DataSegmentType.Active */:
        case 2 /* DataSegmentType.ActiveWithMemoryIndex */:
            return true;
        default:
            return false;
    }
}
var DataMode;
(function (DataMode) {
    DataMode[DataMode["Active"] = 0] = "Active";
    DataMode[DataMode["Passive"] = 1] = "Passive";
})(DataMode = exports.DataMode || (exports.DataMode = {}));
var ElementSegmentType;
(function (ElementSegmentType) {
    ElementSegmentType[ElementSegmentType["LegacyActiveFuncrefExternval"] = 0] = "LegacyActiveFuncrefExternval";
    ElementSegmentType[ElementSegmentType["PassiveExternval"] = 1] = "PassiveExternval";
    ElementSegmentType[ElementSegmentType["ActiveExternval"] = 2] = "ActiveExternval";
    ElementSegmentType[ElementSegmentType["DeclaredExternval"] = 3] = "DeclaredExternval";
    ElementSegmentType[ElementSegmentType["LegacyActiveFuncrefElemexpr"] = 4] = "LegacyActiveFuncrefElemexpr";
    ElementSegmentType[ElementSegmentType["PassiveElemexpr"] = 5] = "PassiveElemexpr";
    ElementSegmentType[ElementSegmentType["ActiveElemexpr"] = 6] = "ActiveElemexpr";
    ElementSegmentType[ElementSegmentType["DeclaredElemexpr"] = 7] = "DeclaredElemexpr";
})(ElementSegmentType || (ElementSegmentType = {}));
function isActiveElementSegmentType(segmentType) {
    switch (segmentType) {
        case 0 /* ElementSegmentType.LegacyActiveFuncrefExternval */:
        case 2 /* ElementSegmentType.ActiveExternval */:
        case 4 /* ElementSegmentType.LegacyActiveFuncrefElemexpr */:
        case 6 /* ElementSegmentType.ActiveElemexpr */:
            return true;
        default:
            return false;
    }
}
function isExternvalElementSegmentType(segmentType) {
    switch (segmentType) {
        case 0 /* ElementSegmentType.LegacyActiveFuncrefExternval */:
        case 1 /* ElementSegmentType.PassiveExternval */:
        case 2 /* ElementSegmentType.ActiveExternval */:
        case 3 /* ElementSegmentType.DeclaredExternval */:
            return true;
        default:
            return false;
    }
}
var ElementMode;
(function (ElementMode) {
    ElementMode[ElementMode["Active"] = 0] = "Active";
    ElementMode[ElementMode["Passive"] = 1] = "Passive";
    ElementMode[ElementMode["Declarative"] = 2] = "Declarative";
})(ElementMode = exports.ElementMode || (exports.ElementMode = {}));
var DataRange = /** @class */ (function () {
    function DataRange(start, end) {
        this.start = start;
        this.end = end;
    }
    DataRange.prototype.offset = function (delta) {
        this.start += delta;
        this.end += delta;
    };
    return DataRange;
}());
var TagAttribute;
(function (TagAttribute) {
    TagAttribute[TagAttribute["Exception"] = 0] = "Exception";
})(TagAttribute = exports.TagAttribute || (exports.TagAttribute = {}));
var Int64 = /** @class */ (function () {
    function Int64(data) {
        this._data = data || new Uint8Array(8);
    }
    Int64.prototype.toInt32 = function () {
        return (this._data[0] |
            (this._data[1] << 8) |
            (this._data[2] << 16) |
            (this._data[3] << 24));
    };
    Int64.prototype.toDouble = function () {
        var power = 1;
        var sum;
        if (this._data[7] & 0x80) {
            sum = -1;
            for (var i = 0; i < 8; i++, power *= 256)
                sum -= power * (0xff ^ this._data[i]);
        }
        else {
            sum = 0;
            for (var i = 0; i < 8; i++, power *= 256)
                sum += power * this._data[i];
        }
        return sum;
    };
    Int64.prototype.toString = function () {
        var low = (this._data[0] |
            (this._data[1] << 8) |
            (this._data[2] << 16) |
            (this._data[3] << 24)) >>>
            0;
        var high = (this._data[4] |
            (this._data[5] << 8) |
            (this._data[6] << 16) |
            (this._data[7] << 24)) >>>
            0;
        if (low === 0 && high === 0) {
            return "0";
        }
        var sign = false;
        if (high >> 31) {
            high = 4294967296 - high;
            if (low > 0) {
                high--;
                low = 4294967296 - low;
            }
            sign = true;
        }
        var buf = [];
        while (high > 0) {
            var t = (high % 10) * 4294967296 + low;
            high = Math.floor(high / 10);
            buf.unshift((t % 10).toString());
            low = Math.floor(t / 10);
        }
        while (low > 0) {
            buf.unshift((low % 10).toString());
            low = Math.floor(low / 10);
        }
        if (sign)
            buf.unshift("-");
        return buf.join("");
    };
    Object.defineProperty(Int64.prototype, "data", {
        get: function () {
            return this._data;
        },
        enumerable: false,
        configurable: true
    });
    return Int64;
}());
exports.Int64 = Int64;
var BinaryReader = /** @class */ (function () {
    function BinaryReader() {
        this._data = null;
        this._pos = 0;
        this._length = 0;
        this._eof = false;
        this.state = 0 /* BinaryReaderState.INITIAL */;
        this.result = null;
        this.error = null;
        this._sectionEntriesLeft = 0;
        this._compactImportsLeft = 0;
        this._compactImportModule = null;
        this._compactImportDesc = null;
        this._sectionId = -1 /* SectionCode.Unknown */;
        this._sectionRange = null;
        this._functionRange = null;
        this._segmentType = 0;
        this._segmentEntriesLeft = 0;
        this._recGroupTypesLeft = 0;
    }
    Object.defineProperty(BinaryReader.prototype, "data", {
        get: function () {
            return this._data;
        },
        enumerable: false,
        configurable: true
    });
    Object.defineProperty(BinaryReader.prototype, "position", {
        get: function () {
            return this._pos;
        },
        enumerable: false,
        configurable: true
    });
    Object.defineProperty(BinaryReader.prototype, "length", {
        get: function () {
            return this._length;
        },
        enumerable: false,
        configurable: true
    });
    BinaryReader.prototype.setData = function (buffer, pos, length, eof) {
        var posDelta = pos - this._pos;
        this._data = new Uint8Array(buffer);
        this._pos = pos;
        this._length = length;
        this._eof = eof === undefined ? true : eof;
        if (this._sectionRange)
            this._sectionRange.offset(posDelta);
        if (this._functionRange)
            this._functionRange.offset(posDelta);
    };
    BinaryReader.prototype.hasBytes = function (n) {
        return this._pos + n <= this._length;
    };
    BinaryReader.prototype.hasMoreBytes = function () {
        return this.hasBytes(1);
    };
    BinaryReader.prototype.readUint8 = function () {
        return this._data[this._pos++];
    };
    BinaryReader.prototype.readInt32 = function () {
        var b1 = this._data[this._pos++];
        var b2 = this._data[this._pos++];
        var b3 = this._data[this._pos++];
        var b4 = this._data[this._pos++];
        return b1 | (b2 << 8) | (b3 << 16) | (b4 << 24);
    };
    BinaryReader.prototype.readUint32 = function () {
        return this.readInt32();
    };
    BinaryReader.prototype.peekInt32 = function () {
        var b1 = this._data[this._pos];
        var b2 = this._data[this._pos + 1];
        var b3 = this._data[this._pos + 2];
        var b4 = this._data[this._pos + 3];
        return b1 | (b2 << 8) | (b3 << 16) | (b4 << 24);
    };
    BinaryReader.prototype.hasVarIntBytes = function () {
        var pos = this._pos;
        while (pos < this._length) {
            if ((this._data[pos++] & 0x80) == 0)
                return true;
        }
        return false;
    };
    BinaryReader.prototype.readVarUint1 = function () {
        return this.readUint8();
    };
    BinaryReader.prototype.readVarInt7 = function () {
        return (this.readUint8() << 25) >> 25;
    };
    BinaryReader.prototype.readVarUint7 = function () {
        return this.readUint8();
    };
    BinaryReader.prototype.readVarInt32 = function () {
        var result = 0;
        var shift = 0;
        while (true) {
            var byte = this.readUint8();
            result |= (byte & 0x7f) << shift;
            shift += 7;
            if ((byte & 0x80) === 0)
                break;
        }
        if (shift >= 32)
            return result;
        var ashift = 32 - shift;
        return (result << ashift) >> ashift;
    };
    BinaryReader.prototype.readVarUint32 = function () {
        var result = 0;
        var shift = 0;
        while (true) {
            var byte = this.readUint8();
            result |= (byte & 0x7f) << shift;
            shift += 7;
            if ((byte & 0x80) === 0)
                break;
        }
        return result >>> 0;
    };
    // Reads an unsigned 64-bit LEB128 value as a number: values above 2^53 lose
    // precision, which is fine for memory and table sizes and offsets.
    BinaryReader.prototype.readVarUint64AsNumber = function () {
        var result = 0;
        var scale = 1;
        while (true) {
            var byte = this.readUint8();
            result += (byte & 0x7f) * scale;
            scale *= 0x80;
            if ((byte & 0x80) === 0)
                break;
        }
        return result;
    };
    BinaryReader.prototype.readVarInt64 = function () {
        var result = new Uint8Array(8);
        var i = 0;
        var c = 0;
        var shift = 0;
        while (true) {
            var byte = this.readUint8();
            c |= (byte & 0x7f) << shift;
            shift += 7;
            if (shift > 8) {
                result[i++] = c & 0xff;
                c >>= 8;
                shift -= 8;
            }
            if ((byte & 0x80) === 0)
                break;
        }
        var ashift = 32 - shift;
        c = (c << ashift) >> ashift;
        while (i < 8) {
            result[i++] = c & 0xff;
            c >>= 8;
        }
        return new Int64(result);
    };
    // Reads any "s33" (signed 33-bit integer) value correctly; no guarantees
    // outside that range.
    BinaryReader.prototype.readHeapType = function () {
        var lsb = this.readUint8();
        if (lsb & 0x80) {
            // Has more data than one byte.
            var tail = this.readVarInt32();
            return (tail - 1) * 128 + lsb;
        }
        else {
            return (lsb << 25) >> 25;
        }
    };
    BinaryReader.prototype.readType = function () {
        var kind = this.readHeapType();
        if (kind >= 0) {
            return new Type(kind);
        }
        switch (kind) {
            case -29 /* TypeKind.ref_null */:
            case -28 /* TypeKind.ref */:
                var index = this.readHeapType();
                return new RefType(kind, index);
            case -1 /* TypeKind.i32 */:
            case -2 /* TypeKind.i64 */:
            case -3 /* TypeKind.f32 */:
            case -4 /* TypeKind.f64 */:
            case -5 /* TypeKind.v128 */:
            case -8 /* TypeKind.i8 */:
            case -9 /* TypeKind.i16 */:
            case -16 /* TypeKind.funcref */:
            case -17 /* TypeKind.externref */:
            case -23 /* TypeKind.exnref */:
            case -18 /* TypeKind.anyref */:
            case -19 /* TypeKind.eqref */:
            case -20 /* TypeKind.i31ref */:
            case -14 /* TypeKind.nullexternref */:
            case -13 /* TypeKind.nullfuncref */:
            case -12 /* TypeKind.nullexnref */:
            case -21 /* TypeKind.structref */:
            case -22 /* TypeKind.arrayref */:
            case -15 /* TypeKind.nullref */:
            case -32 /* TypeKind.func */:
            case -33 /* TypeKind.struct */:
            case -34 /* TypeKind.array */:
            case -48 /* TypeKind.subtype */:
            case -50 /* TypeKind.rec_group */:
            case -49 /* TypeKind.subtype_final */:
            case -64 /* TypeKind.empty_block_type */:
                return new Type(kind);
            default:
                throw new Error("Unknown type kind: ".concat(kind));
        }
    };
    BinaryReader.prototype.readStringBytes = function () {
        var length = this.readVarUint32();
        return this.readBytes(length);
    };
    BinaryReader.prototype.readBytes = function (length) {
        var result = this._data.subarray(this._pos, this._pos + length);
        this._pos += length;
        return new Uint8Array(result); // making a clone of the data
    };
    BinaryReader.prototype.skipBytes = function (length) {
        this._pos += length;
    };
    BinaryReader.prototype.hasStringBytes = function () {
        if (!this.hasVarIntBytes())
            return false;
        var pos = this._pos;
        var length = this.readVarUint32();
        var result = this.hasBytes(length);
        this._pos = pos;
        return result;
    };
    BinaryReader.prototype.hasSectionPayload = function () {
        return this.hasBytes(this._sectionRange.end - this._pos);
    };
    BinaryReader.prototype.readFuncType = function () {
        var paramCount = this.readVarUint32();
        var paramTypes = new Array(paramCount);
        for (var i = 0; i < paramCount; i++)
            paramTypes[i] = this.readType();
        var returnCount = this.readVarUint32();
        var returnTypes = new Array(returnCount);
        for (var i = 0; i < returnCount; i++)
            returnTypes[i] = this.readType();
        return {
            form: -32 /* TypeKind.func */,
            params: paramTypes,
            returns: returnTypes,
        };
    };
    BinaryReader.prototype.readBaseType = function () {
        var form = this.readVarInt7();
        switch (form) {
            case -32 /* TypeKind.func */:
                return this.readFuncType();
            case -33 /* TypeKind.struct */:
                return this.readStructType();
            case -34 /* TypeKind.array */:
                return this.readArrayType();
            default:
                throw new Error("Unknown type kind: ".concat(form));
        }
    };
    BinaryReader.prototype.readSubtype = function (final) {
        var supertypesCount = this.readVarUint32();
        var supertypes = new Array(supertypesCount);
        for (var i = 0; i < supertypesCount; i++) {
            supertypes[i] = this.readHeapType();
        }
        var result = this.readBaseType();
        result.supertypes = supertypes;
        result.final = final;
        return result;
    };
    BinaryReader.prototype.readStructType = function () {
        var fieldCount = this.readVarUint32();
        var fieldTypes = new Array(fieldCount);
        var fieldMutabilities = new Array(fieldCount);
        for (var i = 0; i < fieldCount; i++) {
            fieldTypes[i] = this.readType();
            fieldMutabilities[i] = !!this.readVarUint1();
        }
        return {
            form: -33 /* TypeKind.struct */,
            fields: fieldTypes,
            mutabilities: fieldMutabilities,
        };
    };
    BinaryReader.prototype.readArrayType = function () {
        var elementType = this.readType();
        var mutability = !!this.readVarUint1();
        return {
            form: -34 /* TypeKind.array */,
            elementType: elementType,
            mutability: mutability,
        };
    };
    BinaryReader.prototype.readResizableLimits = function (maxPresent, is64) {
        var initial = is64 ? this.readVarUint64AsNumber() : this.readVarUint32();
        var maximum;
        if (maxPresent) {
            maximum = is64 ? this.readVarUint64AsNumber() : this.readVarUint32();
        }
        return { initial: initial, maximum: maximum };
    };
    BinaryReader.prototype.readTableType = function () {
        var elementType = this.readType();
        var flags = this.readVarUint32();
        var is64 = !!(flags & 0x04);
        var limits = this.readResizableLimits(!!(flags & 0x01), is64);
        return { elementType: elementType, limits: limits, is64: is64 };
    };
    BinaryReader.prototype.readMemoryType = function () {
        var flags = this.readVarUint32();
        var shared = !!(flags & 0x02);
        var is64 = !!(flags & 0x04);
        var limits = this.readResizableLimits(!!(flags & 0x01), is64);
        var pageSizeLog2;
        if (flags & 0x08) {
            pageSizeLog2 = this.readVarUint32();
        }
        return {
            limits: limits,
            shared: shared,
            is64: is64,
            pageSizeLog2: pageSizeLog2,
        };
    };
    BinaryReader.prototype.readGlobalType = function () {
        if (!this.hasVarIntBytes()) {
            return null;
        }
        var pos = this._pos;
        var contentType = this.readType();
        if (!this.hasVarIntBytes()) {
            this._pos = pos;
            return null;
        }
        var mutability = this.readVarUint1();
        return { contentType: contentType, mutability: mutability };
    };
    BinaryReader.prototype.readTagType = function () {
        var attribute = this.readVarUint32();
        var typeIndex = this.readVarUint32();
        return {
            attribute: attribute,
            typeIndex: typeIndex,
        };
    };
    BinaryReader.prototype.readTypeEntryCommon = function (form) {
        switch (form) {
            case -32 /* TypeKind.func */:
                this.result = this.readFuncType();
                break;
            case -48 /* TypeKind.subtype */:
                this.result = this.readSubtype(false);
                break;
            case -49 /* TypeKind.subtype_final */:
                this.result = this.readSubtype(true);
                break;
            case -33 /* TypeKind.struct */:
                this.result = this.readStructType();
                break;
            case -34 /* TypeKind.array */:
                this.result = this.readArrayType();
                break;
            case -1 /* TypeKind.i32 */:
            case -2 /* TypeKind.i64 */:
            case -3 /* TypeKind.f32 */:
            case -4 /* TypeKind.f64 */:
            case -5 /* TypeKind.v128 */:
            case -8 /* TypeKind.i8 */:
            case -9 /* TypeKind.i16 */:
            case -16 /* TypeKind.funcref */:
            case -17 /* TypeKind.externref */:
            case -23 /* TypeKind.exnref */:
            case -18 /* TypeKind.anyref */:
            case -19 /* TypeKind.eqref */:
                this.result = {
                    form: form,
                };
                break;
            default:
                throw new Error("Unknown type kind: ".concat(form));
        }
    };
    BinaryReader.prototype.readTypeEntry = function () {
        if (this._sectionEntriesLeft === 0) {
            this.skipSection();
            return this.read();
        }
        var form = this.readVarInt7();
        if (form == -50 /* TypeKind.rec_group */) {
            this.state = 47 /* BinaryReaderState.BEGIN_REC_GROUP */;
            this.result = null;
            this._recGroupTypesLeft = this.readVarUint32();
        }
        else {
            this.state = 11 /* BinaryReaderState.TYPE_SECTION_ENTRY */;
            this.readTypeEntryCommon(form);
            this._sectionEntriesLeft--;
        }
        return true;
    };
    BinaryReader.prototype.readRecGroupEntry = function () {
        if (this._recGroupTypesLeft === 0) {
            this.state = 48 /* BinaryReaderState.END_REC_GROUP */;
            this.result = null;
            this._sectionEntriesLeft--;
            this._recGroupTypesLeft = -1;
            return true;
        }
        this.state = 11 /* BinaryReaderState.TYPE_SECTION_ENTRY */;
        var form = this.readVarInt7();
        this.readTypeEntryCommon(form);
        this._recGroupTypesLeft--;
        return true;
    };
    BinaryReader.prototype.readImportEntry = function () {
        if (this._compactImportsLeft > 0) {
            this.state = 12 /* BinaryReaderState.IMPORT_SECTION_ENTRY */;
            var field = this.readStringBytes();
            var desc = this._compactImportDesc || this.readImportDesc(this.readUint8());
            this.result = {
                module: this._compactImportModule,
                field: field,
                kind: desc.kind,
                funcTypeIndex: desc.funcTypeIndex,
                type: desc.type,
            };
            this._compactImportsLeft--;
            return true;
        }
        if (this._sectionEntriesLeft === 0) {
            this.skipSection();
            return this.read();
        }
        this.state = 12 /* BinaryReaderState.IMPORT_SECTION_ENTRY */;
        var module = this.readStringBytes();
        var field = this.readStringBytes();
        var kind = this.readUint8();
        if (field.length === 0 &&
            (kind === 127 /* CompactImportKind.ModuleName */ ||
                kind === 126 /* CompactImportKind.ModuleNameAndExternType */)) {
            // A group of imports from one module, either with their own types
            // or sharing one.
            this._compactImportModule = module;
            this._compactImportDesc =
                kind === 126 /* CompactImportKind.ModuleNameAndExternType */
                    ? this.readImportDesc(this.readUint8())
                    : null;
            this._compactImportsLeft = this.readVarUint32();
            this._sectionEntriesLeft--;
            return this.readImportEntry();
        }
        var desc = this.readImportDesc(kind);
        this.result = {
            module: module,
            field: field,
            kind: desc.kind,
            funcTypeIndex: desc.funcTypeIndex,
            type: desc.type,
        };
        this._sectionEntriesLeft--;
        return true;
    };
    BinaryReader.prototype.readImportDesc = function (kind) {
        var funcTypeIndex;
        var type;
        switch (kind) {
            case 0 /* ExternalKind.Function */:
                funcTypeIndex = this.readVarUint32();
                break;
            case 1 /* ExternalKind.Table */:
                type = this.readTableType();
                break;
            case 2 /* ExternalKind.Memory */:
                type = this.readMemoryType();
                break;
            case 3 /* ExternalKind.Global */:
                type = this.readGlobalType();
                break;
            case 4 /* ExternalKind.Tag */:
                type = this.readTagType();
                break;
        }
        return { kind: kind, funcTypeIndex: funcTypeIndex, type: type };
    };
    BinaryReader.prototype.readExportEntry = function () {
        if (this._sectionEntriesLeft === 0) {
            this.skipSection();
            return this.read();
        }
        var field = this.readStringBytes();
        var kind = this.readUint8();
        var index = this.readVarUint32();
        this.state = 17 /* BinaryReaderState.EXPORT_SECTION_ENTRY */;
        this.result = { field: field, kind: kind, index: index };
        this._sectionEntriesLeft--;
        return true;
    };
    BinaryReader.prototype.readFunctionEntry = function () {
        if (this._sectionEntriesLeft === 0) {
            this.skipSection();
            return this.read();
        }
        var typeIndex = this.readVarUint32();
        this.state = 13 /* BinaryReaderState.FUNCTION_SECTION_ENTRY */;
        this.result = { typeIndex: typeIndex };
        this._sectionEntriesLeft--;
        return true;
    };
    BinaryReader.prototype.readTableEntry = function () {
        if (this._sectionEntriesLeft === 0) {
            this.skipSection();
            return this.read();
        }
        this.state = 14 /* BinaryReaderState.TABLE_SECTION_ENTRY */;
        this.result = this.readTableType();
        this._sectionEntriesLeft--;
        return true;
    };
    BinaryReader.prototype.readMemoryEntry = function () {
        if (this._sectionEntriesLeft === 0) {
            this.skipSection();
            return this.read();
        }
        this.state = 15 /* BinaryReaderState.MEMORY_SECTION_ENTRY */;
        this.result = this.readMemoryType();
        this._sectionEntriesLeft--;
        return true;
    };
    BinaryReader.prototype.readTagEntry = function () {
        if (this._sectionEntriesLeft === 0) {
            this.skipSection();
            return this.read();
        }
        this.state = 23 /* BinaryReaderState.TAG_SECTION_ENTRY */;
        this.result = this.readTagType();
        this._sectionEntriesLeft--;
        return true;
    };
    BinaryReader.prototype.readGlobalEntry = function () {
        if (this._sectionEntriesLeft === 0) {
            this.skipSection();
            return this.read();
        }
        var globalType = this.readGlobalType();
        if (!globalType) {
            this.state = 16 /* BinaryReaderState.GLOBAL_SECTION_ENTRY */;
            return false;
        }
        this.state = 39 /* BinaryReaderState.BEGIN_GLOBAL_SECTION_ENTRY */;
        this.result = {
            type: globalType,
        };
        this._sectionEntriesLeft--;
        return true;
    };
    BinaryReader.prototype.readElementEntry = function () {
        if (this._sectionEntriesLeft === 0) {
            this.skipSection();
            return this.read();
        }
        var pos = this._pos;
        if (!this.hasMoreBytes()) {
            this.state = 20 /* BinaryReaderState.ELEMENT_SECTION_ENTRY */;
            return false;
        }
        var segmentType = this.readUint8();
        var mode, tableIndex;
        switch (segmentType) {
            case 0 /* ElementSegmentType.LegacyActiveFuncrefExternval */:
            case 4 /* ElementSegmentType.LegacyActiveFuncrefElemexpr */:
                mode = 0 /* ElementMode.Active */;
                tableIndex = 0;
                break;
            case 1 /* ElementSegmentType.PassiveExternval */:
            case 5 /* ElementSegmentType.PassiveElemexpr */:
                mode = 1 /* ElementMode.Passive */;
                break;
            case 2 /* ElementSegmentType.ActiveExternval */:
            case 6 /* ElementSegmentType.ActiveElemexpr */:
                mode = 0 /* ElementMode.Active */;
                if (!this.hasVarIntBytes()) {
                    this.state = 20 /* BinaryReaderState.ELEMENT_SECTION_ENTRY */;
                    this._pos = pos;
                    return false;
                }
                tableIndex = this.readVarUint32();
                break;
            case 3 /* ElementSegmentType.DeclaredExternval */:
            case 7 /* ElementSegmentType.DeclaredElemexpr */:
                mode = 2 /* ElementMode.Declarative */;
                break;
            default:
                throw new Error("Unsupported element segment type ".concat(segmentType));
        }
        this.state = 33 /* BinaryReaderState.BEGIN_ELEMENT_SECTION_ENTRY */;
        this.result = { mode: mode, tableIndex: tableIndex };
        this._sectionEntriesLeft--;
        this._segmentType = segmentType;
        return true;
    };
    BinaryReader.prototype.readElementEntryBody = function () {
        var elementType = Type.funcref;
        switch (this._segmentType) {
            case 1 /* ElementSegmentType.PassiveExternval */:
            case 2 /* ElementSegmentType.ActiveExternval */:
            case 3 /* ElementSegmentType.DeclaredExternval */:
                if (!this.hasMoreBytes())
                    return false;
                // We just skip the 0x00 byte, the `elemkind` byte
                // is reserved for future versions of WebAssembly.
                this.skipBytes(1);
                break;
            case 5 /* ElementSegmentType.PassiveElemexpr */:
            case 6 /* ElementSegmentType.ActiveElemexpr */:
            case 7 /* ElementSegmentType.DeclaredElemexpr */:
                if (!this.hasMoreBytes())
                    return false;
                elementType = this.readType();
                break;
            case 0 /* ElementSegmentType.LegacyActiveFuncrefExternval */:
            case 4 /* ElementSegmentType.LegacyActiveFuncrefElemexpr */:
                // The element type is implicitly `funcref`.
                break;
            default:
                throw new Error("Unsupported element segment type ".concat(this._segmentType));
        }
        this.state = 34 /* BinaryReaderState.ELEMENT_SECTION_ENTRY_BODY */;
        this.result = { elementType: elementType };
        return true;
    };
    BinaryReader.prototype.readDataEntry = function () {
        if (this._sectionEntriesLeft === 0) {
            this.skipSection();
            return this.read();
        }
        var pos = this._pos;
        if (!this.hasVarIntBytes()) {
            this.state = 18 /* BinaryReaderState.DATA_SECTION_ENTRY */;
            return false;
        }
        var segmentType = this.readVarUint32();
        var mode, memoryIndex;
        switch (segmentType) {
            case 0 /* DataSegmentType.Active */:
                mode = 0 /* DataMode.Active */;
                memoryIndex = 0;
                break;
            case 1 /* DataSegmentType.Passive */:
                mode = 1 /* DataMode.Passive */;
                break;
            case 2 /* DataSegmentType.ActiveWithMemoryIndex */:
                mode = 0 /* DataMode.Active */;
                if (!this.hasVarIntBytes()) {
                    this._pos = pos;
                    this.state = 18 /* BinaryReaderState.DATA_SECTION_ENTRY */;
                    return false;
                }
                memoryIndex = this.readVarUint32();
                break;
            default:
                throw new Error("Unsupported data segment type ".concat(segmentType));
        }
        this.state = 36 /* BinaryReaderState.BEGIN_DATA_SECTION_ENTRY */;
        this.result = { mode: mode, memoryIndex: memoryIndex };
        this._sectionEntriesLeft--;
        this._segmentType = segmentType;
        return true;
    };
    BinaryReader.prototype.readDataCountEntry = function () {
        if (this._sectionEntriesLeft === 0) {
            this.skipSection();
            return this.read();
        }
        this.state = 49 /* BinaryReaderState.DATA_COUNT_SECTION_ENTRY */;
        this.result = this.readVarUint32();
        this._sectionEntriesLeft--;
        return true;
    };
    BinaryReader.prototype.readDataEntryBody = function () {
        if (!this.hasStringBytes()) {
            return false;
        }
        this.state = 37 /* BinaryReaderState.DATA_SECTION_ENTRY_BODY */;
        this.result = {
            data: this.readStringBytes(),
        };
        return true;
    };
    BinaryReader.prototype.readInitExpressionBody = function () {
        this.state = 25 /* BinaryReaderState.BEGIN_INIT_EXPRESSION_BODY */;
        this.result = null;
        return true;
    };
    BinaryReader.prototype.readOffsetExpressionBody = function () {
        this.state = 44 /* BinaryReaderState.BEGIN_OFFSET_EXPRESSION_BODY */;
        this.result = null;
        return true;
    };
    BinaryReader.prototype.readMemoryImmediate = function () {
        var flags = this.readVarUint32();
        var memoryIndex = 0;
        if (flags & 0x40) {
            flags &= ~0x40;
            memoryIndex = this.readVarUint32();
        }
        var offset = this.readVarUint64AsNumber();
        return { flags: flags, offset: offset, memoryIndex: memoryIndex };
    };
    BinaryReader.prototype.readNameMap = function () {
        var count = this.readVarUint32();
        var result = [];
        for (var i = 0; i < count; i++) {
            var index = this.readVarUint32();
            var name = this.readStringBytes();
            result.push({ index: index, name: name });
        }
        return result;
    };
    BinaryReader.prototype.readNameEntry = function () {
        var pos = this._pos;
        if (pos >= this._sectionRange.end) {
            this.skipSection();
            return this.read();
        }
        if (!this.hasVarIntBytes())
            return false;
        var type = this.readVarUint7();
        if (!this.hasVarIntBytes()) {
            this._pos = pos;
            return false;
        }
        var payloadLength = this.readVarUint32();
        if (!this.hasBytes(payloadLength)) {
            this._pos = pos;
            return false;
        }
        var result;
        switch (type) {
            case 0 /* NameType.Module */:
                result = {
                    type: type,
                    moduleName: this.readStringBytes(),
                };
                break;
            case 1 /* NameType.Function */:
            case 11 /* NameType.Tag */:
            case 4 /* NameType.Type */:
            case 5 /* NameType.Table */:
            case 6 /* NameType.Memory */:
            case 7 /* NameType.Global */:
                result = {
                    type: type,
                    names: this.readNameMap(),
                };
                break;
            case 2 /* NameType.Local */:
                var funcsLength = this.readVarUint32();
                var funcs = [];
                for (var i = 0; i < funcsLength; i++) {
                    var funcIndex = this.readVarUint32();
                    funcs.push({
                        index: funcIndex,
                        locals: this.readNameMap(),
                    });
                }
                result = {
                    type: type,
                    funcs: funcs,
                };
                break;
            case 10 /* NameType.Field */:
                var typesLength = this.readVarUint32();
                var types = [];
                for (var i = 0; i < typesLength; i++) {
                    var fieldIndex = this.readVarUint32();
                    types.push({
                        index: fieldIndex,
                        fields: this.readNameMap(),
                    });
                }
                result = {
                    type: type,
                    types: types,
                };
                break;
            default:
                // Skip this unknown name subsection (as per specification,
                // custom section errors shouldn't cause Wasm parsing to fail).
                this.skipBytes(payloadLength);
                return this.read();
        }
        this.state = 19 /* BinaryReaderState.NAME_SECTION_ENTRY */;
        this.result = result;
        return true;
    };
    BinaryReader.prototype.readRelocHeader = function () {
        // See https://github.com/WebAssembly/tool-conventions/blob/master/Linking.md
        if (!this.hasVarIntBytes()) {
            return false;
        }
        var pos = this._pos;
        var sectionId = this.readVarUint7();
        var sectionName;
        if (sectionId === 0 /* SectionCode.Custom */) {
            if (!this.hasStringBytes()) {
                this._pos = pos;
                return false;
            }
            sectionName = this.readStringBytes();
        }
        this.state = 41 /* BinaryReaderState.RELOC_SECTION_HEADER */;
        this.result = {
            id: sectionId,
            name: sectionName,
        };
        return true;
    };
    BinaryReader.prototype.readLinkingEntry = function () {
        if (this._sectionEntriesLeft === 0) {
            this.skipSection();
            return this.read();
        }
        if (!this.hasVarIntBytes())
            return false;
        var pos = this._pos;
        var type = this.readVarUint32();
        var index;
        switch (type) {
            case 1 /* LinkingType.StackPointer */:
                if (!this.hasVarIntBytes()) {
                    this._pos = pos;
                    return false;
                }
                index = this.readVarUint32();
                break;
            default:
                this.error = new Error("Bad linking type: ".concat(type));
                this.state = -1 /* BinaryReaderState.ERROR */;
                return true;
        }
        this.state = 21 /* BinaryReaderState.LINKING_SECTION_ENTRY */;
        this.result = { type: type, index: index };
        this._sectionEntriesLeft--;
        return true;
    };
    BinaryReader.prototype.readSourceMappingURL = function () {
        if (!this.hasStringBytes())
            return false;
        var url = this.readStringBytes();
        this.state = 43 /* BinaryReaderState.SOURCE_MAPPING_URL */;
        this.result = { url: url };
        return true;
    };
    BinaryReader.prototype.readRelocEntry = function () {
        if (this._sectionEntriesLeft === 0) {
            this.skipSection();
            return this.read();
        }
        if (!this.hasVarIntBytes())
            return false;
        var pos = this._pos;
        var type = this.readVarUint7();
        if (!this.hasVarIntBytes()) {
            this._pos = pos;
            return false;
        }
        var offset = this.readVarUint32();
        if (!this.hasVarIntBytes()) {
            this._pos = pos;
            return false;
        }
        var index = this.readVarUint32();
        var addend;
        switch (type) {
            case 0 /* RelocType.FunctionIndex_LEB */:
            case 1 /* RelocType.TableIndex_SLEB */:
            case 2 /* RelocType.TableIndex_I32 */:
            case 6 /* RelocType.TypeIndex_LEB */:
            case 7 /* RelocType.GlobalIndex_LEB */:
                break;
            case 3 /* RelocType.GlobalAddr_LEB */:
            case 4 /* RelocType.GlobalAddr_SLEB */:
            case 5 /* RelocType.GlobalAddr_I32 */:
                if (!this.hasVarIntBytes()) {
                    this._pos = pos;
                    return false;
                }
                addend = this.readVarUint32();
                break;
            default:
                this.error = new Error("Bad relocation type: ".concat(type));
                this.state = -1 /* BinaryReaderState.ERROR */;
                return true;
        }
        this.state = 42 /* BinaryReaderState.RELOC_SECTION_ENTRY */;
        this.result = {
            type: type,
            offset: offset,
            index: index,
            addend: addend,
        };
        this._sectionEntriesLeft--;
        return true;
    };
    BinaryReader.prototype.readCodeOperator_0xfb = function () {
        // The longest instructions have: 2 bytes opcode, 5 bytes type index,
        // 5 bytes field index.
        var MAX_CODE_OPERATOR_0XFB_SIZE = 12;
        if (!this._eof && !this.hasBytes(MAX_CODE_OPERATOR_0XFB_SIZE)) {
            return false;
        }
        var code, brDepth, refType, srcType, fieldIndex, segmentIndex, len, literal;
        code = this._data[this._pos++] | 0xfb00;
        switch (code) {
            case 64280 /* OperatorCode.br_on_cast */:
            case 64281 /* OperatorCode.br_on_cast_fail */:
                literal = this.readUint8();
                brDepth = this.readVarUint32();
                srcType = this.readHeapType();
                refType = this.readHeapType();
                break;
            case 64267 /* OperatorCode.array_get */:
            case 64268 /* OperatorCode.array_get_s */:
            case 64269 /* OperatorCode.array_get_u */:
            case 64270 /* OperatorCode.array_set */:
            case 64262 /* OperatorCode.array_new */:
            case 64263 /* OperatorCode.array_new_default */:
            case 64256 /* OperatorCode.struct_new */:
            case 64257 /* OperatorCode.struct_new_default */:
                refType = this.readVarUint32();
                break;
            case 64264 /* OperatorCode.array_new_fixed */:
                refType = this.readVarUint32();
                len = this.readVarUint32();
                break;
            case 64272 /* OperatorCode.array_fill */:
                refType = this.readVarUint32();
                break;
            case 64273 /* OperatorCode.array_copy */:
                refType = this.readVarUint32();
                srcType = this.readVarUint32();
                break;
            case 64258 /* OperatorCode.struct_get */:
            case 64259 /* OperatorCode.struct_get_s */:
            case 64260 /* OperatorCode.struct_get_u */:
            case 64261 /* OperatorCode.struct_set */:
                refType = this.readVarUint32();
                fieldIndex = this.readVarUint32();
                break;
            case 64265 /* OperatorCode.array_new_data */:
            case 64266 /* OperatorCode.array_new_elem */:
            case 64274 /* OperatorCode.array_init_data */:
            case 64275 /* OperatorCode.array_init_elem */:
                refType = this.readVarUint32();
                segmentIndex = this.readVarUint32();
                break;
            case 64276 /* OperatorCode.ref_test */:
            case 64277 /* OperatorCode.ref_test_null */:
            case 64278 /* OperatorCode.ref_cast */:
            case 64279 /* OperatorCode.ref_cast_null */:
                refType = this.readHeapType();
                break;
            case 64271 /* OperatorCode.array_len */:
            case 64283 /* OperatorCode.extern_convert_any */:
            case 64282 /* OperatorCode.any_convert_extern */:
            case 64284 /* OperatorCode.ref_i31 */:
            case 64285 /* OperatorCode.i31_get_s */:
            case 64286 /* OperatorCode.i31_get_u */:
                break;
            default:
                this.error = new Error("Unknown operator: 0x".concat(code.toString(16).padStart(4, "0")));
                this.state = -1 /* BinaryReaderState.ERROR */;
                return true;
        }
        this.result = {
            code: code,
            blockType: undefined,
            refType: refType,
            srcType: srcType,
            brDepth: brDepth,
            brTable: undefined,
            tableIndex: undefined,
            funcIndex: undefined,
            typeIndex: undefined,
            localIndex: undefined,
            globalIndex: undefined,
            fieldIndex: fieldIndex,
            memoryAddress: undefined,
            literal: literal,
            literalBits: undefined,
            segmentIndex: segmentIndex,
            destinationIndex: undefined,
            len: len,
            lines: undefined,
            lineIndex: undefined,
        };
        return true;
    };
    BinaryReader.prototype.readCodeOperator_0xfc = function () {
        if (!this.hasVarIntBytes()) {
            return false;
        }
        var code = this.readVarUint32() | 0xfc00;
        var segmentIndex, destinationIndex, tableIndex, memoryIndex;
        switch (code) {
            case 64512 /* OperatorCode.i32_trunc_sat_f32_s */:
            case 64513 /* OperatorCode.i32_trunc_sat_f32_u */:
            case 64514 /* OperatorCode.i32_trunc_sat_f64_s */:
            case 64515 /* OperatorCode.i32_trunc_sat_f64_u */:
            case 64516 /* OperatorCode.i64_trunc_sat_f32_s */:
            case 64517 /* OperatorCode.i64_trunc_sat_f32_u */:
            case 64518 /* OperatorCode.i64_trunc_sat_f64_s */:
            case 64519 /* OperatorCode.i64_trunc_sat_f64_u */:
            case 64531 /* OperatorCode.i64_add128 */:
            case 64532 /* OperatorCode.i64_sub128 */:
            case 64533 /* OperatorCode.i64_mul_wide_s */:
            case 64534 /* OperatorCode.i64_mul_wide_u */:
                break;
            case 64522 /* OperatorCode.memory_copy */:
                // Encoded as `memory.copy dst src`: memoryIndex is the source memory.
                destinationIndex = this.readVarUint32();
                memoryIndex = this.readVarUint32();
                break;
            case 64523 /* OperatorCode.memory_fill */:
                memoryIndex = this.readVarUint32();
                break;
            case 64524 /* OperatorCode.table_init */:
                segmentIndex = this.readVarUint32();
                tableIndex = this.readVarUint32();
                break;
            case 64526 /* OperatorCode.table_copy */:
                // Encoded as `table.copy dst src`: tableIndex is the source table.
                destinationIndex = this.readVarUint32();
                tableIndex = this.readVarUint32();
                break;
            case 64527 /* OperatorCode.table_grow */:
            case 64528 /* OperatorCode.table_size */:
            case 64529 /* OperatorCode.table_fill */:
                tableIndex = this.readVarUint32();
                break;
            case 64520 /* OperatorCode.memory_init */:
                segmentIndex = this.readVarUint32();
                memoryIndex = this.readVarUint32();
                break;
            case 64521 /* OperatorCode.data_drop */:
            case 64525 /* OperatorCode.elem_drop */:
                segmentIndex = this.readVarUint32();
                break;
            default:
                this.error = new Error("Unknown operator: 0x".concat(code.toString(16).padStart(4, "0")));
                this.state = -1 /* BinaryReaderState.ERROR */;
                return true;
        }
        this.result = {
            code: code,
            blockType: undefined,
            selectType: undefined,
            refType: undefined,
            srcType: undefined,
            brDepth: undefined,
            brTable: undefined,
            funcIndex: undefined,
            typeIndex: undefined,
            tableIndex: tableIndex,
            localIndex: undefined,
            globalIndex: undefined,
            fieldIndex: undefined,
            memoryAddress: undefined,
            literal: undefined,
            literalBits: undefined,
            segmentIndex: segmentIndex,
            destinationIndex: destinationIndex,
            memoryIndex: memoryIndex,
            len: undefined,
            lines: undefined,
            lineIndex: undefined,
        };
        return true;
    };
    BinaryReader.prototype.readCodeOperator_0xfd = function () {
        var MAX_CODE_OPERATOR_0XFD_SIZE = 24;
        var pos = this._pos;
        if (!this._eof && pos + MAX_CODE_OPERATOR_0XFD_SIZE > this._length) {
            return false;
        }
        if (!this.hasVarIntBytes()) {
            return false;
        }
        var code = this.readVarUint32() | 0xfd000;
        var memoryAddress;
        var literal;
        var lineIndex;
        var lines;
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
            case 1036380 /* OperatorCode.v128_load32_zero */:
            case 1036381 /* OperatorCode.v128_load64_zero */:
                memoryAddress = this.readMemoryImmediate();
                break;
            case 1036300 /* OperatorCode.v128_const */:
                literal = this.readBytes(16);
                break;
            case 1036301 /* OperatorCode.i8x16_shuffle */:
                lines = new Uint8Array(16);
                for (var i = 0; i < lines.length; i++) {
                    lines[i] = this.readUint8();
                }
                break;
            case 1036309 /* OperatorCode.i8x16_extract_lane_s */:
            case 1036310 /* OperatorCode.i8x16_extract_lane_u */:
            case 1036311 /* OperatorCode.i8x16_replace_lane */:
            case 1036312 /* OperatorCode.i16x8_extract_lane_s */:
            case 1036313 /* OperatorCode.i16x8_extract_lane_u */:
            case 1036314 /* OperatorCode.i16x8_replace_lane */:
            case 1036315 /* OperatorCode.i32x4_extract_lane */:
            case 1036316 /* OperatorCode.i32x4_replace_lane */:
            case 1036317 /* OperatorCode.i64x2_extract_lane */:
            case 1036318 /* OperatorCode.i64x2_replace_lane */:
            case 1036319 /* OperatorCode.f32x4_extract_lane */:
            case 1036320 /* OperatorCode.f32x4_replace_lane */:
            case 1036321 /* OperatorCode.f64x2_extract_lane */:
            case 1036322 /* OperatorCode.f64x2_replace_lane */:
                lineIndex = this.readUint8();
                break;
            case 1036372 /* OperatorCode.v128_load8_lane */:
            case 1036373 /* OperatorCode.v128_load16_lane */:
            case 1036374 /* OperatorCode.v128_load32_lane */:
            case 1036375 /* OperatorCode.v128_load64_lane */:
            case 1036376 /* OperatorCode.v128_store8_lane */:
            case 1036377 /* OperatorCode.v128_store16_lane */:
            case 1036378 /* OperatorCode.v128_store32_lane */:
            case 1036379 /* OperatorCode.v128_store64_lane */:
                memoryAddress = this.readMemoryImmediate();
                lineIndex = this.readUint8();
                break;
            case 1036302 /* OperatorCode.i8x16_swizzle */:
            case 1036303 /* OperatorCode.i8x16_splat */:
            case 1036304 /* OperatorCode.i16x8_splat */:
            case 1036305 /* OperatorCode.i32x4_splat */:
            case 1036306 /* OperatorCode.i64x2_splat */:
            case 1036307 /* OperatorCode.f32x4_splat */:
            case 1036308 /* OperatorCode.f64x2_splat */:
            case 1036323 /* OperatorCode.i8x16_eq */:
            case 1036324 /* OperatorCode.i8x16_ne */:
            case 1036325 /* OperatorCode.i8x16_lt_s */:
            case 1036326 /* OperatorCode.i8x16_lt_u */:
            case 1036327 /* OperatorCode.i8x16_gt_s */:
            case 1036328 /* OperatorCode.i8x16_gt_u */:
            case 1036329 /* OperatorCode.i8x16_le_s */:
            case 1036330 /* OperatorCode.i8x16_le_u */:
            case 1036331 /* OperatorCode.i8x16_ge_s */:
            case 1036332 /* OperatorCode.i8x16_ge_u */:
            case 1036333 /* OperatorCode.i16x8_eq */:
            case 1036334 /* OperatorCode.i16x8_ne */:
            case 1036335 /* OperatorCode.i16x8_lt_s */:
            case 1036336 /* OperatorCode.i16x8_lt_u */:
            case 1036337 /* OperatorCode.i16x8_gt_s */:
            case 1036338 /* OperatorCode.i16x8_gt_u */:
            case 1036339 /* OperatorCode.i16x8_le_s */:
            case 1036340 /* OperatorCode.i16x8_le_u */:
            case 1036341 /* OperatorCode.i16x8_ge_s */:
            case 1036342 /* OperatorCode.i16x8_ge_u */:
            case 1036343 /* OperatorCode.i32x4_eq */:
            case 1036344 /* OperatorCode.i32x4_ne */:
            case 1036345 /* OperatorCode.i32x4_lt_s */:
            case 1036346 /* OperatorCode.i32x4_lt_u */:
            case 1036347 /* OperatorCode.i32x4_gt_s */:
            case 1036348 /* OperatorCode.i32x4_gt_u */:
            case 1036349 /* OperatorCode.i32x4_le_s */:
            case 1036350 /* OperatorCode.i32x4_le_u */:
            case 1036351 /* OperatorCode.i32x4_ge_s */:
            case 1036352 /* OperatorCode.i32x4_ge_u */:
            case 1036353 /* OperatorCode.f32x4_eq */:
            case 1036354 /* OperatorCode.f32x4_ne */:
            case 1036355 /* OperatorCode.f32x4_lt */:
            case 1036356 /* OperatorCode.f32x4_gt */:
            case 1036357 /* OperatorCode.f32x4_le */:
            case 1036358 /* OperatorCode.f32x4_ge */:
            case 1036359 /* OperatorCode.f64x2_eq */:
            case 1036360 /* OperatorCode.f64x2_ne */:
            case 1036361 /* OperatorCode.f64x2_lt */:
            case 1036362 /* OperatorCode.f64x2_gt */:
            case 1036363 /* OperatorCode.f64x2_le */:
            case 1036364 /* OperatorCode.f64x2_ge */:
            case 1036365 /* OperatorCode.v128_not */:
            case 1036366 /* OperatorCode.v128_and */:
            case 1036367 /* OperatorCode.v128_andnot */:
            case 1036368 /* OperatorCode.v128_or */:
            case 1036369 /* OperatorCode.v128_xor */:
            case 1036370 /* OperatorCode.v128_bitselect */:
            case 1036371 /* OperatorCode.v128_any_true */:
            case 1036382 /* OperatorCode.f32x4_demote_f64x2_zero */:
            case 1036383 /* OperatorCode.f64x2_promote_low_f32x4 */:
            case 1036384 /* OperatorCode.i8x16_abs */:
            case 1036385 /* OperatorCode.i8x16_neg */:
            case 1036386 /* OperatorCode.i8x16_popcnt */:
            case 1036387 /* OperatorCode.i8x16_all_true */:
            case 1036388 /* OperatorCode.i8x16_bitmask */:
            case 1036389 /* OperatorCode.i8x16_narrow_i16x8_s */:
            case 1036390 /* OperatorCode.i8x16_narrow_i16x8_u */:
            case 1036391 /* OperatorCode.f32x4_ceil */:
            case 1036392 /* OperatorCode.f32x4_floor */:
            case 1036393 /* OperatorCode.f32x4_trunc */:
            case 1036394 /* OperatorCode.f32x4_nearest */:
            case 1036395 /* OperatorCode.i8x16_shl */:
            case 1036396 /* OperatorCode.i8x16_shr_s */:
            case 1036397 /* OperatorCode.i8x16_shr_u */:
            case 1036398 /* OperatorCode.i8x16_add */:
            case 1036399 /* OperatorCode.i8x16_add_sat_s */:
            case 1036400 /* OperatorCode.i8x16_add_sat_u */:
            case 1036401 /* OperatorCode.i8x16_sub */:
            case 1036402 /* OperatorCode.i8x16_sub_sat_s */:
            case 1036403 /* OperatorCode.i8x16_sub_sat_u */:
            case 1036404 /* OperatorCode.f64x2_ceil */:
            case 1036405 /* OperatorCode.f64x2_floor */:
            case 1036406 /* OperatorCode.i8x16_min_s */:
            case 1036407 /* OperatorCode.i8x16_min_u */:
            case 1036408 /* OperatorCode.i8x16_max_s */:
            case 1036409 /* OperatorCode.i8x16_max_u */:
            case 1036410 /* OperatorCode.f64x2_trunc */:
            case 1036411 /* OperatorCode.i8x16_avgr_u */:
            case 1036412 /* OperatorCode.i16x8_extadd_pairwise_i8x16_s */:
            case 1036413 /* OperatorCode.i16x8_extadd_pairwise_i8x16_u */:
            case 1036414 /* OperatorCode.i32x4_extadd_pairwise_i16x8_s */:
            case 1036415 /* OperatorCode.i32x4_extadd_pairwise_i16x8_u */:
            case 1036416 /* OperatorCode.i16x8_abs */:
            case 1036417 /* OperatorCode.i16x8_neg */:
            case 1036418 /* OperatorCode.i16x8_q15mulr_sat_s */:
            case 1036419 /* OperatorCode.i16x8_all_true */:
            case 1036420 /* OperatorCode.i16x8_bitmask */:
            case 1036421 /* OperatorCode.i16x8_narrow_i32x4_s */:
            case 1036422 /* OperatorCode.i16x8_narrow_i32x4_u */:
            case 1036423 /* OperatorCode.i16x8_extend_low_i8x16_s */:
            case 1036424 /* OperatorCode.i16x8_extend_high_i8x16_s */:
            case 1036425 /* OperatorCode.i16x8_extend_low_i8x16_u */:
            case 1036426 /* OperatorCode.i16x8_extend_high_i8x16_u */:
            case 1036427 /* OperatorCode.i16x8_shl */:
            case 1036428 /* OperatorCode.i16x8_shr_s */:
            case 1036429 /* OperatorCode.i16x8_shr_u */:
            case 1036430 /* OperatorCode.i16x8_add */:
            case 1036431 /* OperatorCode.i16x8_add_sat_s */:
            case 1036432 /* OperatorCode.i16x8_add_sat_u */:
            case 1036433 /* OperatorCode.i16x8_sub */:
            case 1036434 /* OperatorCode.i16x8_sub_sat_s */:
            case 1036435 /* OperatorCode.i16x8_sub_sat_u */:
            case 1036436 /* OperatorCode.f64x2_nearest */:
            case 1036437 /* OperatorCode.i16x8_mul */:
            case 1036438 /* OperatorCode.i16x8_min_s */:
            case 1036439 /* OperatorCode.i16x8_min_u */:
            case 1036440 /* OperatorCode.i16x8_max_s */:
            case 1036441 /* OperatorCode.i16x8_max_u */:
            case 1036443 /* OperatorCode.i16x8_avgr_u */:
            case 1036444 /* OperatorCode.i16x8_extmul_low_i8x16_s */:
            case 1036445 /* OperatorCode.i16x8_extmul_high_i8x16_s */:
            case 1036446 /* OperatorCode.i16x8_extmul_low_i8x16_u */:
            case 1036447 /* OperatorCode.i16x8_extmul_high_i8x16_u */:
            case 1036448 /* OperatorCode.i32x4_abs */:
            case 1036449 /* OperatorCode.i32x4_neg */:
            case 1036451 /* OperatorCode.i32x4_all_true */:
            case 1036452 /* OperatorCode.i32x4_bitmask */:
            case 1036455 /* OperatorCode.i32x4_extend_low_i16x8_s */:
            case 1036456 /* OperatorCode.i32x4_extend_high_i16x8_s */:
            case 1036457 /* OperatorCode.i32x4_extend_low_i16x8_u */:
            case 1036458 /* OperatorCode.i32x4_extend_high_i16x8_u */:
            case 1036459 /* OperatorCode.i32x4_shl */:
            case 1036460 /* OperatorCode.i32x4_shr_s */:
            case 1036461 /* OperatorCode.i32x4_shr_u */:
            case 1036462 /* OperatorCode.i32x4_add */:
            case 1036465 /* OperatorCode.i32x4_sub */:
            case 1036469 /* OperatorCode.i32x4_mul */:
            case 1036470 /* OperatorCode.i32x4_min_s */:
            case 1036471 /* OperatorCode.i32x4_min_u */:
            case 1036472 /* OperatorCode.i32x4_max_s */:
            case 1036473 /* OperatorCode.i32x4_max_u */:
            case 1036474 /* OperatorCode.i32x4_dot_i16x8_s */:
            case 1036476 /* OperatorCode.i32x4_extmul_low_i16x8_s */:
            case 1036477 /* OperatorCode.i32x4_extmul_high_i16x8_s */:
            case 1036478 /* OperatorCode.i32x4_extmul_low_i16x8_u */:
            case 1036479 /* OperatorCode.i32x4_extmul_high_i16x8_u */:
            case 1036480 /* OperatorCode.i64x2_abs */:
            case 1036481 /* OperatorCode.i64x2_neg */:
            case 1036483 /* OperatorCode.i64x2_all_true */:
            case 1036484 /* OperatorCode.i64x2_bitmask */:
            case 1036487 /* OperatorCode.i64x2_extend_low_i32x4_s */:
            case 1036488 /* OperatorCode.i64x2_extend_high_i32x4_s */:
            case 1036489 /* OperatorCode.i64x2_extend_low_i32x4_u */:
            case 1036490 /* OperatorCode.i64x2_extend_high_i32x4_u */:
            case 1036491 /* OperatorCode.i64x2_shl */:
            case 1036492 /* OperatorCode.i64x2_shr_s */:
            case 1036493 /* OperatorCode.i64x2_shr_u */:
            case 1036494 /* OperatorCode.i64x2_add */:
            case 1036497 /* OperatorCode.i64x2_sub */:
            case 1036501 /* OperatorCode.i64x2_mul */:
            case 1036502 /* OperatorCode.i64x2_eq */:
            case 1036503 /* OperatorCode.i64x2_ne */:
            case 1036504 /* OperatorCode.i64x2_lt_s */:
            case 1036505 /* OperatorCode.i64x2_gt_s */:
            case 1036506 /* OperatorCode.i64x2_le_s */:
            case 1036507 /* OperatorCode.i64x2_ge_s */:
            case 1036508 /* OperatorCode.i64x2_extmul_low_i32x4_s */:
            case 1036509 /* OperatorCode.i64x2_extmul_high_i32x4_s */:
            case 1036510 /* OperatorCode.i64x2_extmul_low_i32x4_u */:
            case 1036511 /* OperatorCode.i64x2_extmul_high_i32x4_u */:
            case 1036512 /* OperatorCode.f32x4_abs */:
            case 1036513 /* OperatorCode.f32x4_neg */:
            case 1036515 /* OperatorCode.f32x4_sqrt */:
            case 1036516 /* OperatorCode.f32x4_add */:
            case 1036517 /* OperatorCode.f32x4_sub */:
            case 1036518 /* OperatorCode.f32x4_mul */:
            case 1036519 /* OperatorCode.f32x4_div */:
            case 1036520 /* OperatorCode.f32x4_min */:
            case 1036521 /* OperatorCode.f32x4_max */:
            case 1036522 /* OperatorCode.f32x4_pmin */:
            case 1036523 /* OperatorCode.f32x4_pmax */:
            case 1036524 /* OperatorCode.f64x2_abs */:
            case 1036525 /* OperatorCode.f64x2_neg */:
            case 1036527 /* OperatorCode.f64x2_sqrt */:
            case 1036528 /* OperatorCode.f64x2_add */:
            case 1036529 /* OperatorCode.f64x2_sub */:
            case 1036530 /* OperatorCode.f64x2_mul */:
            case 1036531 /* OperatorCode.f64x2_div */:
            case 1036532 /* OperatorCode.f64x2_min */:
            case 1036533 /* OperatorCode.f64x2_max */:
            case 1036534 /* OperatorCode.f64x2_pmin */:
            case 1036535 /* OperatorCode.f64x2_pmax */:
            case 1036536 /* OperatorCode.i32x4_trunc_sat_f32x4_s */:
            case 1036537 /* OperatorCode.i32x4_trunc_sat_f32x4_u */:
            case 1036538 /* OperatorCode.f32x4_convert_i32x4_s */:
            case 1036539 /* OperatorCode.f32x4_convert_i32x4_u */:
            case 1036540 /* OperatorCode.i32x4_trunc_sat_f64x2_s_zero */:
            case 1036541 /* OperatorCode.i32x4_trunc_sat_f64x2_u_zero */:
            case 1036542 /* OperatorCode.f64x2_convert_low_i32x4_s */:
            case 1036543 /* OperatorCode.f64x2_convert_low_i32x4_u */:
                break;
            case 1036544 /* OperatorCode.i8x16_relaxed_swizzle */:
            case 1036545 /* OperatorCode.i32x4_relaxed_trunc_f32x4_s */:
            case 1036546 /* OperatorCode.i32x4_relaxed_trunc_f32x4_u */:
            case 1036547 /* OperatorCode.i32x4_relaxed_trunc_f64x2_s_zero */:
            case 1036548 /* OperatorCode.i32x4_relaxed_trunc_f64x2_u_zero */:
            case 1036549 /* OperatorCode.f32x4_relaxed_madd */:
            case 1036550 /* OperatorCode.f32x4_relaxed_nmadd */:
            case 1036551 /* OperatorCode.f64x2_relaxed_madd */:
            case 1036552 /* OperatorCode.f64x2_relaxed_nmadd */:
            case 1036553 /* OperatorCode.i8x16_relaxed_laneselect */:
            case 1036554 /* OperatorCode.i16x8_relaxed_laneselect */:
            case 1036555 /* OperatorCode.i32x4_relaxed_laneselect */:
            case 1036556 /* OperatorCode.i64x2_relaxed_laneselect */:
            case 1036557 /* OperatorCode.f32x4_relaxed_min */:
            case 1036558 /* OperatorCode.f32x4_relaxed_max */:
            case 1036559 /* OperatorCode.f64x2_relaxed_min */:
            case 1036560 /* OperatorCode.f64x2_relaxed_max */:
            case 1036561 /* OperatorCode.i16x8_relaxed_q15mulr_s */:
            case 1036562 /* OperatorCode.i16x8_relaxed_dot_i8x16_i7x16_s */:
            case 1036563 /* OperatorCode.i32x4_relaxed_dot_i8x16_i7x16_add_s */:
                break;
            default:
                this.error = new Error("Unknown operator: 0x".concat(code.toString(16).padStart(4, "0")));
                this.state = -1 /* BinaryReaderState.ERROR */;
                return true;
        }
        this.result = {
            code: code,
            blockType: undefined,
            selectType: undefined,
            refType: undefined,
            srcType: undefined,
            brDepth: undefined,
            brTable: undefined,
            funcIndex: undefined,
            typeIndex: undefined,
            localIndex: undefined,
            globalIndex: undefined,
            fieldIndex: undefined,
            memoryAddress: memoryAddress,
            literal: literal,
            literalBits: undefined,
            segmentIndex: undefined,
            destinationIndex: undefined,
            len: undefined,
            lines: lines,
            lineIndex: lineIndex,
        };
        return true;
    };
    BinaryReader.prototype.readCodeOperator_0xfe = function () {
        var MAX_CODE_OPERATOR_0XFE_SIZE = 22;
        var pos = this._pos;
        if (!this._eof && pos + MAX_CODE_OPERATOR_0XFE_SIZE > this._length) {
            return false;
        }
        if (!this.hasVarIntBytes()) {
            return false;
        }
        var code = this.readVarUint32() | 0xfe00;
        var memoryAddress;
        switch (code) {
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
                memoryAddress = this.readMemoryImmediate();
                break;
            case 65027 /* OperatorCode.atomic_fence */: {
                var consistency_model = this.readUint8();
                if (consistency_model != 0) {
                    this.error = new Error("atomic.fence consistency model must be 0");
                    this.state = -1 /* BinaryReaderState.ERROR */;
                    return true;
                }
                break;
            }
            default:
                this.error = new Error("Unknown operator: 0x".concat(code.toString(16).padStart(4, "0")));
                this.state = -1 /* BinaryReaderState.ERROR */;
                return true;
        }
        this.result = {
            code: code,
            blockType: undefined,
            selectType: undefined,
            refType: undefined,
            srcType: undefined,
            brDepth: undefined,
            brTable: undefined,
            funcIndex: undefined,
            typeIndex: undefined,
            localIndex: undefined,
            globalIndex: undefined,
            fieldIndex: undefined,
            memoryAddress: memoryAddress,
            literal: undefined,
            literalBits: undefined,
            segmentIndex: undefined,
            destinationIndex: undefined,
            len: undefined,
            lines: undefined,
            lineIndex: undefined,
        };
        return true;
    };
    BinaryReader.prototype.readCodeOperator = function () {
        switch (this.state) {
            case 30 /* BinaryReaderState.CODE_OPERATOR */:
                if (this._pos >= this._functionRange.end) {
                    this.skipFunctionBody();
                    return this.read();
                }
                break;
            case 26 /* BinaryReaderState.INIT_EXPRESSION_OPERATOR */:
                if (this.result &&
                    this.result.code === 11 /* OperatorCode.end */) {
                    this.state = 27 /* BinaryReaderState.END_INIT_EXPRESSION_BODY */;
                    this.result = null;
                    return true;
                }
                break;
            case 45 /* BinaryReaderState.OFFSET_EXPRESSION_OPERATOR */:
                if (this.result &&
                    this.result.code === 11 /* OperatorCode.end */) {
                    this.state = 46 /* BinaryReaderState.END_OFFSET_EXPRESSION_BODY */;
                    this.result = null;
                    return true;
                }
                break;
        }
        var code, blockType, selectType, refType, brDepth, brTable, tryTable, relativeDepth, funcIndex, typeIndex, tableIndex, localIndex, globalIndex, tagIndex, memoryIndex, memoryAddress, literal, literalBits;
        if (this.state === 26 /* BinaryReaderState.INIT_EXPRESSION_OPERATOR */ &&
            this._sectionId === 9 /* SectionCode.Element */ &&
            isExternvalElementSegmentType(this._segmentType)) {
            // We are reading a `vec(funcidx)` here, which is a dense encoding
            // for a sequence of `((ref.func y) end)` instructions.
            if (this.result &&
                this.result.code === 210 /* OperatorCode.ref_func */) {
                code = 11 /* OperatorCode.end */;
            }
            else {
                if (!this.hasVarIntBytes())
                    return false;
                code = 210 /* OperatorCode.ref_func */;
                funcIndex = this.readVarUint32();
            }
        }
        else {
            // load/store with a memory index and a 64-bit offset
            var MAX_CODE_OPERATOR_SIZE = 21;
            var pos = this._pos;
            if (!this._eof && pos + MAX_CODE_OPERATOR_SIZE > this._length) {
                return false;
            }
            code = this._data[this._pos++];
            switch (code) {
                case 2 /* OperatorCode.block */:
                case 3 /* OperatorCode.loop */:
                case 4 /* OperatorCode.if */:
                case 6 /* OperatorCode.try */:
                    blockType = this.readType();
                    break;
                case 12 /* OperatorCode.br */:
                case 13 /* OperatorCode.br_if */:
                case 213 /* OperatorCode.br_on_null */:
                case 214 /* OperatorCode.br_on_non_null */:
                    brDepth = this.readVarUint32();
                    break;
                case 14 /* OperatorCode.br_table */:
                    var tableCount = this.readVarUint32();
                    if (!this.hasBytes(tableCount + 1)) {
                        // We need at least (tableCount + 1) bytes
                        this._pos = pos;
                        return false;
                    }
                    brTable = [];
                    for (var i = 0; i <= tableCount; i++) {
                        // including default
                        if (!this.hasVarIntBytes()) {
                            this._pos = pos;
                            return false;
                        }
                        brTable.push(this.readVarUint32());
                    }
                    break;
                case 9 /* OperatorCode.rethrow */:
                case 24 /* OperatorCode.delegate */:
                    relativeDepth = this.readVarUint32();
                    break;
                case 7 /* OperatorCode.catch */:
                case 8 /* OperatorCode.throw */:
                    tagIndex = this.readVarInt32();
                    break;
                case 31 /* OperatorCode.try_table */:
                    blockType = this.readType();
                    var tableCount = this.readVarUint32();
                    if (!this.hasBytes(2 * tableCount)) {
                        // We need at least (2 * tableCount) bytes
                        this._pos = pos;
                        return false;
                    }
                    tryTable = [];
                    for (var i = 0; i < tableCount; i++) {
                        if (!this.hasVarIntBytes()) {
                            this._pos = pos;
                            return false;
                        }
                        var kind = this.readVarUint32();
                        var tagIndex;
                        if (kind == CatchHandlerKind.Catch ||
                            kind == CatchHandlerKind.CatchRef) {
                            if (!this.hasVarIntBytes()) {
                                this._pos = pos;
                                return false;
                            }
                            tagIndex = this.readVarUint32();
                        }
                        if (!this.hasVarIntBytes()) {
                            this._pos = pos;
                            return false;
                        }
                        var depth = this.readVarUint32();
                        tryTable.push({ kind: kind, depth: depth, tagIndex: tagIndex });
                    }
                    break;
                case 208 /* OperatorCode.ref_null */:
                    refType = this.readHeapType();
                    break;
                case 16 /* OperatorCode.call */:
                case 18 /* OperatorCode.return_call */:
                case 210 /* OperatorCode.ref_func */:
                    funcIndex = this.readVarUint32();
                    break;
                case 17 /* OperatorCode.call_indirect */:
                case 19 /* OperatorCode.return_call_indirect */:
                    typeIndex = this.readVarUint32();
                    tableIndex = this.readVarUint32();
                    break;
                case 32 /* OperatorCode.local_get */:
                case 33 /* OperatorCode.local_set */:
                case 34 /* OperatorCode.local_tee */:
                    localIndex = this.readVarUint32();
                    break;
                case 35 /* OperatorCode.global_get */:
                case 36 /* OperatorCode.global_set */:
                    globalIndex = this.readVarUint32();
                    break;
                case 37 /* OperatorCode.table_get */:
                case 38 /* OperatorCode.table_set */:
                    tableIndex = this.readVarUint32();
                    break;
                case 20 /* OperatorCode.call_ref */:
                case 21 /* OperatorCode.return_call_ref */:
                    typeIndex = this.readHeapType();
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
                    memoryAddress = this.readMemoryImmediate();
                    break;
                case 63 /* OperatorCode.memory_size */:
                case 64 /* OperatorCode.memory_grow */:
                    memoryIndex = this.readVarUint32();
                    break;
                case 65 /* OperatorCode.i32_const */:
                    literal = this.readVarInt32();
                    break;
                case 66 /* OperatorCode.i64_const */:
                    literal = this.readVarInt64();
                    break;
                case 67 /* OperatorCode.f32_const */: {
                    var view = new DataView(this._data.buffer, this._data.byteOffset);
                    literal = view.getFloat32(this._pos, true);
                    literalBits = view.getUint32(this._pos, true);
                    this._pos += 4;
                    break;
                }
                case 68 /* OperatorCode.f64_const */:
                    literal = new DataView(this._data.buffer, this._data.byteOffset).getFloat64(this._pos, true);
                    this._pos += 8;
                    break;
                case 28 /* OperatorCode.select_with_type */:
                    var num_types = this.readVarInt32();
                    // Only 1 is a valid value currently.
                    if (num_types == 1) {
                        selectType = this.readType();
                    }
                    break;
                case 251 /* OperatorCode.prefix_0xfb */:
                    if (this.readCodeOperator_0xfb()) {
                        return true;
                    }
                    this._pos = pos;
                    return false;
                case 252 /* OperatorCode.prefix_0xfc */:
                    if (this.readCodeOperator_0xfc()) {
                        return true;
                    }
                    this._pos = pos;
                    return false;
                case 253 /* OperatorCode.prefix_0xfd */:
                    if (this.readCodeOperator_0xfd()) {
                        return true;
                    }
                    this._pos = pos;
                    return false;
                case 254 /* OperatorCode.prefix_0xfe */:
                    if (this.readCodeOperator_0xfe()) {
                        return true;
                    }
                    this._pos = pos;
                    return false;
                case 0 /* OperatorCode.unreachable */:
                case 1 /* OperatorCode.nop */:
                case 5 /* OperatorCode.else */:
                case 11 /* OperatorCode.end */:
                case 15 /* OperatorCode.return */:
                case 25 /* OperatorCode.catch_all */:
                case 26 /* OperatorCode.drop */:
                case 27 /* OperatorCode.select */:
                case 69 /* OperatorCode.i32_eqz */:
                case 70 /* OperatorCode.i32_eq */:
                case 71 /* OperatorCode.i32_ne */:
                case 72 /* OperatorCode.i32_lt_s */:
                case 73 /* OperatorCode.i32_lt_u */:
                case 74 /* OperatorCode.i32_gt_s */:
                case 75 /* OperatorCode.i32_gt_u */:
                case 76 /* OperatorCode.i32_le_s */:
                case 77 /* OperatorCode.i32_le_u */:
                case 78 /* OperatorCode.i32_ge_s */:
                case 79 /* OperatorCode.i32_ge_u */:
                case 80 /* OperatorCode.i64_eqz */:
                case 81 /* OperatorCode.i64_eq */:
                case 82 /* OperatorCode.i64_ne */:
                case 83 /* OperatorCode.i64_lt_s */:
                case 84 /* OperatorCode.i64_lt_u */:
                case 85 /* OperatorCode.i64_gt_s */:
                case 86 /* OperatorCode.i64_gt_u */:
                case 87 /* OperatorCode.i64_le_s */:
                case 88 /* OperatorCode.i64_le_u */:
                case 89 /* OperatorCode.i64_ge_s */:
                case 90 /* OperatorCode.i64_ge_u */:
                case 91 /* OperatorCode.f32_eq */:
                case 92 /* OperatorCode.f32_ne */:
                case 93 /* OperatorCode.f32_lt */:
                case 94 /* OperatorCode.f32_gt */:
                case 95 /* OperatorCode.f32_le */:
                case 96 /* OperatorCode.f32_ge */:
                case 97 /* OperatorCode.f64_eq */:
                case 98 /* OperatorCode.f64_ne */:
                case 99 /* OperatorCode.f64_lt */:
                case 100 /* OperatorCode.f64_gt */:
                case 101 /* OperatorCode.f64_le */:
                case 102 /* OperatorCode.f64_ge */:
                case 103 /* OperatorCode.i32_clz */:
                case 104 /* OperatorCode.i32_ctz */:
                case 105 /* OperatorCode.i32_popcnt */:
                case 106 /* OperatorCode.i32_add */:
                case 107 /* OperatorCode.i32_sub */:
                case 108 /* OperatorCode.i32_mul */:
                case 109 /* OperatorCode.i32_div_s */:
                case 110 /* OperatorCode.i32_div_u */:
                case 111 /* OperatorCode.i32_rem_s */:
                case 112 /* OperatorCode.i32_rem_u */:
                case 113 /* OperatorCode.i32_and */:
                case 114 /* OperatorCode.i32_or */:
                case 115 /* OperatorCode.i32_xor */:
                case 116 /* OperatorCode.i32_shl */:
                case 117 /* OperatorCode.i32_shr_s */:
                case 118 /* OperatorCode.i32_shr_u */:
                case 119 /* OperatorCode.i32_rotl */:
                case 120 /* OperatorCode.i32_rotr */:
                case 121 /* OperatorCode.i64_clz */:
                case 122 /* OperatorCode.i64_ctz */:
                case 123 /* OperatorCode.i64_popcnt */:
                case 124 /* OperatorCode.i64_add */:
                case 125 /* OperatorCode.i64_sub */:
                case 126 /* OperatorCode.i64_mul */:
                case 127 /* OperatorCode.i64_div_s */:
                case 128 /* OperatorCode.i64_div_u */:
                case 129 /* OperatorCode.i64_rem_s */:
                case 130 /* OperatorCode.i64_rem_u */:
                case 131 /* OperatorCode.i64_and */:
                case 132 /* OperatorCode.i64_or */:
                case 133 /* OperatorCode.i64_xor */:
                case 134 /* OperatorCode.i64_shl */:
                case 135 /* OperatorCode.i64_shr_s */:
                case 136 /* OperatorCode.i64_shr_u */:
                case 137 /* OperatorCode.i64_rotl */:
                case 138 /* OperatorCode.i64_rotr */:
                case 139 /* OperatorCode.f32_abs */:
                case 140 /* OperatorCode.f32_neg */:
                case 141 /* OperatorCode.f32_ceil */:
                case 142 /* OperatorCode.f32_floor */:
                case 143 /* OperatorCode.f32_trunc */:
                case 144 /* OperatorCode.f32_nearest */:
                case 145 /* OperatorCode.f32_sqrt */:
                case 146 /* OperatorCode.f32_add */:
                case 147 /* OperatorCode.f32_sub */:
                case 148 /* OperatorCode.f32_mul */:
                case 149 /* OperatorCode.f32_div */:
                case 150 /* OperatorCode.f32_min */:
                case 151 /* OperatorCode.f32_max */:
                case 152 /* OperatorCode.f32_copysign */:
                case 153 /* OperatorCode.f64_abs */:
                case 154 /* OperatorCode.f64_neg */:
                case 155 /* OperatorCode.f64_ceil */:
                case 156 /* OperatorCode.f64_floor */:
                case 157 /* OperatorCode.f64_trunc */:
                case 158 /* OperatorCode.f64_nearest */:
                case 159 /* OperatorCode.f64_sqrt */:
                case 160 /* OperatorCode.f64_add */:
                case 161 /* OperatorCode.f64_sub */:
                case 162 /* OperatorCode.f64_mul */:
                case 163 /* OperatorCode.f64_div */:
                case 164 /* OperatorCode.f64_min */:
                case 165 /* OperatorCode.f64_max */:
                case 166 /* OperatorCode.f64_copysign */:
                case 167 /* OperatorCode.i32_wrap_i64 */:
                case 168 /* OperatorCode.i32_trunc_f32_s */:
                case 169 /* OperatorCode.i32_trunc_f32_u */:
                case 170 /* OperatorCode.i32_trunc_f64_s */:
                case 171 /* OperatorCode.i32_trunc_f64_u */:
                case 172 /* OperatorCode.i64_extend_i32_s */:
                case 173 /* OperatorCode.i64_extend_i32_u */:
                case 174 /* OperatorCode.i64_trunc_f32_s */:
                case 175 /* OperatorCode.i64_trunc_f32_u */:
                case 176 /* OperatorCode.i64_trunc_f64_s */:
                case 177 /* OperatorCode.i64_trunc_f64_u */:
                case 178 /* OperatorCode.f32_convert_i32_s */:
                case 179 /* OperatorCode.f32_convert_i32_u */:
                case 180 /* OperatorCode.f32_convert_i64_s */:
                case 181 /* OperatorCode.f32_convert_i64_u */:
                case 182 /* OperatorCode.f32_demote_f64 */:
                case 183 /* OperatorCode.f64_convert_i32_s */:
                case 184 /* OperatorCode.f64_convert_i32_u */:
                case 185 /* OperatorCode.f64_convert_i64_s */:
                case 186 /* OperatorCode.f64_convert_i64_u */:
                case 187 /* OperatorCode.f64_promote_f32 */:
                case 188 /* OperatorCode.i32_reinterpret_f32 */:
                case 189 /* OperatorCode.i64_reinterpret_f64 */:
                case 190 /* OperatorCode.f32_reinterpret_i32 */:
                case 191 /* OperatorCode.f64_reinterpret_i64 */:
                case 192 /* OperatorCode.i32_extend8_s */:
                case 193 /* OperatorCode.i32_extend16_s */:
                case 194 /* OperatorCode.i64_extend8_s */:
                case 195 /* OperatorCode.i64_extend16_s */:
                case 196 /* OperatorCode.i64_extend32_s */:
                case 209 /* OperatorCode.ref_is_null */:
                case 212 /* OperatorCode.ref_as_non_null */:
                case 211 /* OperatorCode.ref_eq */:
                case 10 /* OperatorCode.throw_ref */:
                    break;
                default:
                    this.error = new Error("Unknown operator: ".concat(code));
                    this.state = -1 /* BinaryReaderState.ERROR */;
                    return true;
            }
        }
        this.result = {
            code: code,
            blockType: blockType,
            selectType: selectType,
            refType: refType,
            srcType: undefined,
            brDepth: brDepth,
            brTable: brTable,
            tryTable: tryTable,
            relativeDepth: relativeDepth,
            tableIndex: tableIndex,
            funcIndex: funcIndex,
            typeIndex: typeIndex,
            localIndex: localIndex,
            globalIndex: globalIndex,
            fieldIndex: undefined,
            tagIndex: tagIndex,
            memoryIndex: memoryIndex,
            memoryAddress: memoryAddress,
            literal: literal,
            literalBits: literalBits,
            segmentIndex: undefined,
            destinationIndex: undefined,
            len: undefined,
            lines: undefined,
            lineIndex: undefined,
        };
        return true;
    };
    BinaryReader.prototype.readFunctionBody = function () {
        if (this._sectionEntriesLeft === 0) {
            this.skipSection();
            return this.read();
        }
        if (!this.hasVarIntBytes())
            return false;
        var pos = this._pos;
        var size = this.readVarUint32();
        var bodyEnd = this._pos + size;
        if (!this.hasVarIntBytes()) {
            this._pos = pos;
            return false;
        }
        var localCount = this.readVarUint32();
        var locals = [];
        for (var i = 0; i < localCount; i++) {
            if (!this.hasVarIntBytes()) {
                this._pos = pos;
                return false;
            }
            var count = this.readVarUint32();
            if (!this.hasVarIntBytes()) {
                this._pos = pos;
                return false;
            }
            var type = this.readType();
            locals.push({ count: count, type: type });
        }
        var bodyStart = this._pos;
        this.state = 28 /* BinaryReaderState.BEGIN_FUNCTION_BODY */;
        this.result = {
            locals: locals,
        };
        this._functionRange = new DataRange(bodyStart, bodyEnd);
        this._sectionEntriesLeft--;
        return true;
    };
    BinaryReader.prototype.readSectionHeader = function () {
        if (this._pos >= this._length && this._eof) {
            this._sectionId = -1 /* SectionCode.Unknown */;
            this._sectionRange = null;
            this.result = null;
            this.state = 2 /* BinaryReaderState.END_WASM */;
            return true;
        }
        // TODO: Handle _eof.
        if (this._pos < this._length - 4) {
            var magicNumber = this.peekInt32();
            if (magicNumber === WASM_MAGIC_NUMBER) {
                this._sectionId = -1 /* SectionCode.Unknown */;
                this._sectionRange = null;
                this.result = null;
                this.state = 2 /* BinaryReaderState.END_WASM */;
                return true;
            }
        }
        if (!this.hasVarIntBytes())
            return false;
        var sectionStart = this._pos;
        var id = this.readVarUint7();
        if (!this.hasVarIntBytes()) {
            this._pos = sectionStart;
            return false;
        }
        var payloadLength = this.readVarUint32();
        var name = null;
        var payloadEnd = this._pos + payloadLength;
        if (id == 0) {
            if (!this.hasStringBytes()) {
                this._pos = sectionStart;
                return false;
            }
            name = this.readStringBytes();
        }
        this.result = { id: id, name: name };
        this._sectionId = id;
        this._sectionRange = new DataRange(this._pos, payloadEnd);
        this.state = 3 /* BinaryReaderState.BEGIN_SECTION */;
        return true;
    };
    BinaryReader.prototype.readSectionRawData = function () {
        var payloadLength = this._sectionRange.end - this._sectionRange.start;
        if (!this.hasBytes(payloadLength)) {
            return false;
        }
        this.state = 7 /* BinaryReaderState.SECTION_RAW_DATA */;
        this.result = this.readBytes(payloadLength);
        return true;
    };
    BinaryReader.prototype.readSectionBody = function () {
        if (this._pos >= this._sectionRange.end) {
            this.result = null;
            this.state = 4 /* BinaryReaderState.END_SECTION */;
            this._sectionId = -1 /* SectionCode.Unknown */;
            this._sectionRange = null;
            return true;
        }
        var currentSection = this.result;
        switch (currentSection.id) {
            case 1 /* SectionCode.Type */:
                if (!this.hasSectionPayload())
                    return false;
                this._sectionEntriesLeft = this.readVarUint32();
                this._recGroupTypesLeft = -1;
                return this.readTypeEntry();
            case 2 /* SectionCode.Import */:
                if (!this.hasSectionPayload())
                    return false;
                this._sectionEntriesLeft = this.readVarUint32();
                return this.readImportEntry();
            case 7 /* SectionCode.Export */:
                if (!this.hasSectionPayload())
                    return false;
                this._sectionEntriesLeft = this.readVarUint32();
                return this.readExportEntry();
            case 3 /* SectionCode.Function */:
                if (!this.hasSectionPayload())
                    return false;
                this._sectionEntriesLeft = this.readVarUint32();
                return this.readFunctionEntry();
            case 4 /* SectionCode.Table */:
                if (!this.hasSectionPayload())
                    return false;
                this._sectionEntriesLeft = this.readVarUint32();
                return this.readTableEntry();
            case 5 /* SectionCode.Memory */:
                if (!this.hasSectionPayload())
                    return false;
                this._sectionEntriesLeft = this.readVarUint32();
                return this.readMemoryEntry();
            case 6 /* SectionCode.Global */:
                if (!this.hasVarIntBytes())
                    return false;
                this._sectionEntriesLeft = this.readVarUint32();
                return this.readGlobalEntry();
            case 8 /* SectionCode.Start */:
                if (!this.hasVarIntBytes())
                    return false;
                this.state = 22 /* BinaryReaderState.START_SECTION_ENTRY */;
                this.result = { index: this.readVarUint32() };
                return true;
            case 10 /* SectionCode.Code */:
                if (!this.hasVarIntBytes())
                    return false;
                this._sectionEntriesLeft = this.readVarUint32();
                this.state = 29 /* BinaryReaderState.READING_FUNCTION_HEADER */;
                return this.readFunctionBody();
            case 9 /* SectionCode.Element */:
                if (!this.hasVarIntBytes())
                    return false;
                this._sectionEntriesLeft = this.readVarUint32();
                return this.readElementEntry();
            case 11 /* SectionCode.Data */:
                if (!this.hasVarIntBytes())
                    return false;
                this._sectionEntriesLeft = this.readVarUint32();
                return this.readDataEntry();
            case 12 /* SectionCode.DataCount */:
                if (!this.hasVarIntBytes())
                    return false;
                this._sectionEntriesLeft = this.readVarUint32();
                return this.readDataCountEntry();
            case 13 /* SectionCode.Tag */:
                if (!this.hasVarIntBytes())
                    return false;
                this._sectionEntriesLeft = this.readVarUint32();
                return this.readTagEntry();
            case 0 /* SectionCode.Custom */:
                var customSectionName = (0, exports.bytesToString)(currentSection.name);
                if (customSectionName === "name") {
                    return this.readNameEntry();
                }
                if (customSectionName.indexOf("reloc.") === 0) {
                    return this.readRelocHeader();
                }
                if (customSectionName === "linking") {
                    if (!this.hasVarIntBytes())
                        return false;
                    this._sectionEntriesLeft = this.readVarUint32();
                    return this.readLinkingEntry();
                }
                if (customSectionName === "sourceMappingURL") {
                    return this.readSourceMappingURL();
                }
                return this.readSectionRawData();
            default:
                this.error = new Error("Unsupported section: ".concat(this._sectionId));
                this.state = -1 /* BinaryReaderState.ERROR */;
                return true;
        }
    };
    BinaryReader.prototype.read = function () {
        switch (this.state) {
            case 0 /* BinaryReaderState.INITIAL */:
                if (!this.hasBytes(8))
                    return false;
                var magicNumber = this.readUint32();
                if (magicNumber != WASM_MAGIC_NUMBER) {
                    this.error = new Error("Bad magic number");
                    this.state = -1 /* BinaryReaderState.ERROR */;
                    return true;
                }
                var version = this.readUint32();
                if (version != WASM_SUPPORTED_VERSION &&
                    version != WASM_SUPPORTED_EXPERIMENTAL_VERSION) {
                    this.error = new Error("Bad version number ".concat(version));
                    this.state = -1 /* BinaryReaderState.ERROR */;
                    return true;
                }
                this.result = { magicNumber: magicNumber, version: version };
                this.state = 1 /* BinaryReaderState.BEGIN_WASM */;
                return true;
            case 2 /* BinaryReaderState.END_WASM */:
                this.result = null;
                this.state = 1 /* BinaryReaderState.BEGIN_WASM */;
                if (this.hasMoreBytes()) {
                    this.state = 0 /* BinaryReaderState.INITIAL */;
                    return this.read();
                }
                return false;
            case -1 /* BinaryReaderState.ERROR */:
                return true;
            case 1 /* BinaryReaderState.BEGIN_WASM */:
            case 4 /* BinaryReaderState.END_SECTION */:
                return this.readSectionHeader();
            case 3 /* BinaryReaderState.BEGIN_SECTION */:
                return this.readSectionBody();
            case 5 /* BinaryReaderState.SKIPPING_SECTION */:
                if (!this.hasSectionPayload()) {
                    return false;
                }
                this.state = 4 /* BinaryReaderState.END_SECTION */;
                this._pos = this._sectionRange.end;
                this._sectionId = -1 /* SectionCode.Unknown */;
                this._sectionRange = null;
                this.result = null;
                return true;
            case 32 /* BinaryReaderState.SKIPPING_FUNCTION_BODY */:
                this.state = 31 /* BinaryReaderState.END_FUNCTION_BODY */;
                this._pos = this._functionRange.end;
                this._functionRange = null;
                this.result = null;
                return true;
            case 11 /* BinaryReaderState.TYPE_SECTION_ENTRY */:
                if (this._recGroupTypesLeft >= 0) {
                    return this.readRecGroupEntry();
                }
                return this.readTypeEntry();
            case 47 /* BinaryReaderState.BEGIN_REC_GROUP */:
                return this.readRecGroupEntry();
            case 48 /* BinaryReaderState.END_REC_GROUP */:
                return this.readTypeEntry();
            case 12 /* BinaryReaderState.IMPORT_SECTION_ENTRY */:
                return this.readImportEntry();
            case 17 /* BinaryReaderState.EXPORT_SECTION_ENTRY */:
                return this.readExportEntry();
            case 13 /* BinaryReaderState.FUNCTION_SECTION_ENTRY */:
                return this.readFunctionEntry();
            case 14 /* BinaryReaderState.TABLE_SECTION_ENTRY */:
                return this.readTableEntry();
            case 15 /* BinaryReaderState.MEMORY_SECTION_ENTRY */:
                return this.readMemoryEntry();
            case 23 /* BinaryReaderState.TAG_SECTION_ENTRY */:
                return this.readTagEntry();
            case 16 /* BinaryReaderState.GLOBAL_SECTION_ENTRY */:
            case 40 /* BinaryReaderState.END_GLOBAL_SECTION_ENTRY */:
                return this.readGlobalEntry();
            case 39 /* BinaryReaderState.BEGIN_GLOBAL_SECTION_ENTRY */:
                return this.readInitExpressionBody();
            case 20 /* BinaryReaderState.ELEMENT_SECTION_ENTRY */:
            case 35 /* BinaryReaderState.END_ELEMENT_SECTION_ENTRY */:
                return this.readElementEntry();
            case 33 /* BinaryReaderState.BEGIN_ELEMENT_SECTION_ENTRY */:
                if (isActiveElementSegmentType(this._segmentType)) {
                    return this.readOffsetExpressionBody();
                }
                else {
                    // passive or declared element segment
                    return this.readElementEntryBody();
                }
            case 34 /* BinaryReaderState.ELEMENT_SECTION_ENTRY_BODY */:
                if (!this.hasVarIntBytes())
                    return false;
                this._segmentEntriesLeft = this.readVarUint32();
                if (this._segmentEntriesLeft === 0) {
                    this.state = 35 /* BinaryReaderState.END_ELEMENT_SECTION_ENTRY */;
                    this.result = null;
                    return true;
                }
                return this.readInitExpressionBody();
            case 49 /* BinaryReaderState.DATA_COUNT_SECTION_ENTRY */:
                return this.readDataCountEntry();
            case 18 /* BinaryReaderState.DATA_SECTION_ENTRY */:
            case 38 /* BinaryReaderState.END_DATA_SECTION_ENTRY */:
                return this.readDataEntry();
            case 36 /* BinaryReaderState.BEGIN_DATA_SECTION_ENTRY */:
                if (isActiveDataSegmentType(this._segmentType)) {
                    return this.readOffsetExpressionBody();
                }
                else {
                    // passive data segment
                    return this.readDataEntryBody();
                }
            case 37 /* BinaryReaderState.DATA_SECTION_ENTRY_BODY */:
                this.state = 38 /* BinaryReaderState.END_DATA_SECTION_ENTRY */;
                this.result = null;
                return true;
            case 27 /* BinaryReaderState.END_INIT_EXPRESSION_BODY */:
                switch (this._sectionId) {
                    case 6 /* SectionCode.Global */:
                        this.state = 40 /* BinaryReaderState.END_GLOBAL_SECTION_ENTRY */;
                        return true;
                    case 9 /* SectionCode.Element */:
                        if (--this._segmentEntriesLeft > 0) {
                            return this.readInitExpressionBody();
                        }
                        this.state = 35 /* BinaryReaderState.END_ELEMENT_SECTION_ENTRY */;
                        this.result = null;
                        return true;
                }
                this.error = new Error("Unexpected section type: ".concat(this._sectionId));
                this.state = -1 /* BinaryReaderState.ERROR */;
                return true;
            case 46 /* BinaryReaderState.END_OFFSET_EXPRESSION_BODY */:
                if (this._sectionId === 11 /* SectionCode.Data */) {
                    return this.readDataEntryBody();
                }
                else {
                    return this.readElementEntryBody();
                }
            case 19 /* BinaryReaderState.NAME_SECTION_ENTRY */:
                return this.readNameEntry();
            case 41 /* BinaryReaderState.RELOC_SECTION_HEADER */:
                if (!this.hasVarIntBytes())
                    return false;
                this._sectionEntriesLeft = this.readVarUint32();
                return this.readRelocEntry();
            case 21 /* BinaryReaderState.LINKING_SECTION_ENTRY */:
                return this.readLinkingEntry();
            case 43 /* BinaryReaderState.SOURCE_MAPPING_URL */:
                this.state = 4 /* BinaryReaderState.END_SECTION */;
                this.result = null;
                return true;
            case 42 /* BinaryReaderState.RELOC_SECTION_ENTRY */:
                return this.readRelocEntry();
            case 29 /* BinaryReaderState.READING_FUNCTION_HEADER */:
            case 31 /* BinaryReaderState.END_FUNCTION_BODY */:
                return this.readFunctionBody();
            case 28 /* BinaryReaderState.BEGIN_FUNCTION_BODY */:
                this.state = 30 /* BinaryReaderState.CODE_OPERATOR */;
                return this.readCodeOperator();
            case 25 /* BinaryReaderState.BEGIN_INIT_EXPRESSION_BODY */:
                this.state = 26 /* BinaryReaderState.INIT_EXPRESSION_OPERATOR */;
                return this.readCodeOperator();
            case 44 /* BinaryReaderState.BEGIN_OFFSET_EXPRESSION_BODY */:
                this.state = 45 /* BinaryReaderState.OFFSET_EXPRESSION_OPERATOR */;
                return this.readCodeOperator();
            case 30 /* BinaryReaderState.CODE_OPERATOR */:
            case 26 /* BinaryReaderState.INIT_EXPRESSION_OPERATOR */:
            case 45 /* BinaryReaderState.OFFSET_EXPRESSION_OPERATOR */:
                return this.readCodeOperator();
            case 6 /* BinaryReaderState.READING_SECTION_RAW_DATA */:
                return this.readSectionRawData();
            case 22 /* BinaryReaderState.START_SECTION_ENTRY */:
            case 7 /* BinaryReaderState.SECTION_RAW_DATA */:
                this.state = 4 /* BinaryReaderState.END_SECTION */;
                this.result = null;
                return true;
            default:
                this.error = new Error("Unsupported state: ".concat(this.state));
                this.state = -1 /* BinaryReaderState.ERROR */;
                return true;
        }
    };
    BinaryReader.prototype.skipSection = function () {
        if (this.state === -1 /* BinaryReaderState.ERROR */ ||
            this.state === 0 /* BinaryReaderState.INITIAL */ ||
            this.state === 4 /* BinaryReaderState.END_SECTION */ ||
            this.state === 1 /* BinaryReaderState.BEGIN_WASM */ ||
            this.state === 2 /* BinaryReaderState.END_WASM */)
            return;
        this.state = 5 /* BinaryReaderState.SKIPPING_SECTION */;
    };
    BinaryReader.prototype.skipFunctionBody = function () {
        if (this.state !== 28 /* BinaryReaderState.BEGIN_FUNCTION_BODY */ &&
            this.state !== 30 /* BinaryReaderState.CODE_OPERATOR */)
            return;
        this.state = 32 /* BinaryReaderState.SKIPPING_FUNCTION_BODY */;
    };
    BinaryReader.prototype.skipInitExpression = function () {
        while (this.state === 26 /* BinaryReaderState.INIT_EXPRESSION_OPERATOR */)
            this.readCodeOperator();
    };
    BinaryReader.prototype.fetchSectionRawData = function () {
        if (this.state !== 3 /* BinaryReaderState.BEGIN_SECTION */) {
            this.error = new Error("Unsupported state: ".concat(this.state));
            this.state = -1 /* BinaryReaderState.ERROR */;
            return;
        }
        this.state = 6 /* BinaryReaderState.READING_SECTION_RAW_DATA */;
    };
    return BinaryReader;
}());
exports.BinaryReader = BinaryReader;
if (typeof TextDecoder !== "undefined") {
    try {
        exports.bytesToString = (function () {
            var utf8Decoder = new TextDecoder("utf-8");
            utf8Decoder.decode(new Uint8Array([97, 208, 144]));
            return function (b) { return utf8Decoder.decode(b); };
        })();
    }
    catch (_) {
        /* ignore */
    }
}
if (!exports.bytesToString) {
    exports.bytesToString = function (b) {
        var str = String.fromCharCode.apply(null, b);
        return decodeURIComponent(escape(str));
    };
}
