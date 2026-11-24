/* @ts-self-types="./mappings-glue.d.ts" */

/**
 * A parsed `mappings` string, ready to be queried.
 *
 * JS owns this handle and must call `free()` on it when finished.
 */
class Mappings {
    static __wrap(ptr) {
        const obj = Object.create(Mappings.prototype);
        obj.__wbg_ptr = ptr;
        MappingsFinalization.register(obj, obj.__wbg_ptr, obj);
        return obj;
    }
    __destroy_into_raw() {
        const ptr = this.__wbg_ptr;
        this.__wbg_ptr = 0;
        MappingsFinalization.unregister(this);
        return ptr;
    }
    free() {
        const ptr = this.__destroy_into_raw();
        wasm.__wbg_mappings_free(ptr, 0);
    }
    /**
     * Find all mappings for the given original location and invoke `cb` on
     * each of them.
     *
     * When `has_original_column` is false, `original_column` is ignored and
     * every mapping with a matching source and original line is yielded.
     * @param {number} source
     * @param {number} original_line
     * @param {boolean} has_original_column
     * @param {number} original_column
     * @param {any} cb
     */
    allGeneratedLocationsFor(source, original_line, has_original_column, original_column, cb) {
        const ret = wasm.mappings_allGeneratedLocationsFor(this.__wbg_ptr, source, original_line, has_original_column, original_column, cb);
        if (ret[1]) {
            throw takeFromExternrefTable0(ret[0]);
        }
    }
    /**
     * Invoke `cb` on each mapping, in order of generated location.
     * @param {any} cb
     */
    byGeneratedLocation(cb) {
        const ret = wasm.mappings_byGeneratedLocation(this.__wbg_ptr, cb);
        if (ret[1]) {
            throw takeFromExternrefTable0(ret[0]);
        }
    }
    /**
     * Invoke `cb` on each mapping that has original location information, in
     * order of original location.
     * @param {any} cb
     */
    byOriginalLocation(cb) {
        const ret = wasm.mappings_byOriginalLocation(this.__wbg_ptr, cb);
        if (ret[1]) {
            throw takeFromExternrefTable0(ret[0]);
        }
    }
    /**
     * Compute column spans for the mappings.
     */
    computeColumnSpans() {
        wasm.mappings_computeColumnSpans(this.__wbg_ptr);
    }
    /**
     * Find the mapping for the given original location, if any exists, and
     * invoke `cb` with it once.
     * @param {number} source
     * @param {number} original_line
     * @param {number} original_column
     * @param {number} bias
     * @param {any} cb
     */
    generatedLocationFor(source, original_line, original_column, bias, cb) {
        const ret = wasm.mappings_generatedLocationFor(this.__wbg_ptr, source, original_line, original_column, bias, cb);
        if (ret[1]) {
            throw takeFromExternrefTable0(ret[0]);
        }
    }
    /**
     * Find the mapping for the given generated location, if any exists, and
     * invoke `cb` with it once.
     * @param {number} generated_line
     * @param {number} generated_column
     * @param {number} bias
     * @param {any} cb
     */
    originalLocationFor(generated_line, generated_column, bias, cb) {
        const ret = wasm.mappings_originalLocationFor(this.__wbg_ptr, generated_line, generated_column, bias, cb);
        if (ret[1]) {
            throw takeFromExternrefTable0(ret[0]);
        }
    }
}
if (Symbol.dispose) Mappings.prototype[Symbol.dispose] = Mappings.prototype.free;
exports.Mappings = Mappings;

/**
 * Parse a source map's `mappings` string.
 *
 * On failure, rejects with the numeric `source_map_mappings::Error` code.
 * Replaces the old `allocate_mappings` / `parse_mappings` / `get_last_error`
 * trio: `wasm-bindgen` copies the string into wasm memory itself, so JS no
 * longer has to hand-manage a buffer.
 * @param {string} mappings
 * @returns {Mappings}
 */
function parseMappings(mappings) {
    const ptr0 = passStringToWasm0(mappings, wasm.__wbindgen_malloc, wasm.__wbindgen_realloc);
    const len0 = WASM_VECTOR_LEN;
    const ret = wasm.parseMappings(ptr0, len0);
    if (ret[2]) {
        throw takeFromExternrefTable0(ret[1]);
    }
    return Mappings.__wrap(ret[0]);
}
exports.parseMappings = parseMappings;
function __wbg_get_imports() {
    const import0 = {
        __proto__: null,
        __wbg___wbindgen_throw_5d9e815e6fdf150f: function(arg0, arg1) {
            throw new Error(getStringFromWasm0(arg0, arg1));
        },
        __wbg_call_3434fc50d361c5d3: function() { return handleError(function (arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11) {
            arg0.call(arg1, arg2 >>> 0, arg3 >>> 0, arg4 !== 0, arg5 >>> 0, arg6 !== 0, arg7 >>> 0, arg8 >>> 0, arg9 >>> 0, arg10 !== 0, arg11 >>> 0);
        }, arguments); },
        __wbindgen_generic_0000000000000001: function(arg0) {
            // Cast intrinsic for `F64 -> Externref`.
            const ret = arg0;
            return ret;
        },
        __wbindgen_init_externref_table: function() {
            const table = wasm.__wbindgen_externrefs;
            const offset = table.grow(4);
            table.set(0, undefined);
            table.set(offset + 0, undefined);
            table.set(offset + 1, null);
            table.set(offset + 2, true);
            table.set(offset + 3, false);
        },
    };
    return {
        __proto__: null,
        "./mappings_bg.js": import0,
    };
}

const MappingsFinalization = (typeof FinalizationRegistry === 'undefined')
    ? { register: () => {}, unregister: () => {} }
    : new FinalizationRegistry(ptr => wasm.__wbg_mappings_free(ptr, 1));

function addToExternrefTable0(obj) {
    const idx = wasm.__externref_table_alloc();
    wasm.__wbindgen_externrefs.set(idx, obj);
    return idx;
}

function getStringFromWasm0(ptr, len) {
    return decodeText(ptr >>> 0, len);
}

let cachedUint8ArrayMemory0 = null;
function getUint8ArrayMemory0() {
    if (cachedUint8ArrayMemory0 === null || cachedUint8ArrayMemory0.byteLength === 0) {
        cachedUint8ArrayMemory0 = new Uint8Array(wasm.memory.buffer);
    }
    return cachedUint8ArrayMemory0;
}

function handleError(f, args) {
    try {
        return f.apply(this, args);
    } catch (e) {
        const idx = addToExternrefTable0(e);
        wasm.__wbindgen_exn_store(idx);
    }
}

function passStringToWasm0(arg, malloc, realloc) {
    if (realloc === undefined) {
        const buf = cachedTextEncoder.encode(arg);
        const ptr = malloc(buf.length, 1) >>> 0;
        getUint8ArrayMemory0().subarray(ptr, ptr + buf.length).set(buf);
        WASM_VECTOR_LEN = buf.length;
        return ptr;
    }

    let len = arg.length;
    let ptr = malloc(len, 1) >>> 0;

    const mem = getUint8ArrayMemory0();

    let offset = 0;

    for (; offset < len; offset++) {
        const code = arg.charCodeAt(offset);
        if (code > 0x7F) break;
        mem[ptr + offset] = code;
    }
    if (offset !== len) {
        if (offset !== 0) {
            arg = arg.slice(offset);
        }
        ptr = realloc(ptr, len, len = offset + arg.length * 3, 1) >>> 0;
        const view = getUint8ArrayMemory0().subarray(ptr + offset, ptr + len);
        const ret = cachedTextEncoder.encodeInto(arg, view);

        offset += ret.written;
        ptr = realloc(ptr, len, offset, 1) >>> 0;
    }

    WASM_VECTOR_LEN = offset;
    return ptr;
}

function takeFromExternrefTable0(idx) {
    const value = wasm.__wbindgen_externrefs.get(idx);
    wasm.__externref_table_dealloc(idx);
    return value;
}

let cachedTextDecoder = new TextDecoder('utf-8', { ignoreBOM: true, fatal: true });
cachedTextDecoder.decode();
function decodeText(ptr, len) {
    return cachedTextDecoder.decode(getUint8ArrayMemory0().subarray(ptr, ptr + len));
}

const cachedTextEncoder = new TextEncoder();

if (!('encodeInto' in cachedTextEncoder)) {
    cachedTextEncoder.encodeInto = function (arg, view) {
        const buf = cachedTextEncoder.encode(arg);
        view.set(buf);
        return {
            read: arg.length,
            written: buf.length
        };
    };
}

let WASM_VECTOR_LEN = 0;

const wasmPath = `${__dirname}/mappings.wasm`;
const wasmBytes = require('fs').readFileSync(wasmPath);
const wasmModule = new WebAssembly.Module(wasmBytes);
let wasmInstance = new WebAssembly.Instance(wasmModule, __wbg_get_imports());
let wasm = wasmInstance.exports;
wasm.__wbindgen_start();
