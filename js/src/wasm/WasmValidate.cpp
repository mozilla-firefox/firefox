/*
 * Copyright 2016 Mozilla Foundation
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

#include "wasm/WasmValidate.h"

#include "mozilla/CheckedInt.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Span.h"
#include "mozilla/Utf8.h"

#include "js/Printf.h"
#include "js/String.h"  // JS::MaxStringLength
#include "vm/JSContext.h"
#include "vm/Realm.h"
#include "wasm/WasmCompile.h"
#include "wasm/WasmConstants.h"
#include "wasm/WasmDump.h"
#include "wasm/WasmInitExpr.h"
#include "wasm/WasmOpIter.h"
#include "wasm/WasmTypeDecls.h"

using namespace js;
using namespace js::jit;
using namespace js::wasm;

using mozilla::AsChars;
using mozilla::CheckedInt;
using mozilla::IsUtf8;
using mozilla::Maybe;
using mozilla::Nothing;
using mozilla::Some;
using mozilla::Span;

// Misc helpers.

bool wasm::EncodeLocalEntries(Encoder& e, const ValTypeVector& locals) {
  if (locals.length() > MaxLocals) {
    return false;
  }

  uint32_t numLocalEntries = 0;
  if (locals.length()) {
    ValType prev = locals[0];
    numLocalEntries++;
    for (ValType t : locals) {
      if (t != prev) {
        numLocalEntries++;
        prev = t;
      }
    }
  }

  if (!e.writeVarU32(numLocalEntries)) {
    return false;
  }

  if (numLocalEntries) {
    ValType prev = locals[0];
    uint32_t count = 1;
    for (uint32_t i = 1; i < locals.length(); i++, count++) {
      if (prev != locals[i]) {
        if (!e.writeVarU32(count)) {
          return false;
        }
        if (!e.writeValType(prev)) {
          return false;
        }
        prev = locals[i];
        count = 0;
      }
    }
    if (!e.writeVarU32(count)) {
      return false;
    }
    if (!e.writeValType(prev)) {
      return false;
    }
  }

  return true;
}

bool wasm::DecodeLocalEntriesWithParams(Decoder& d,
                                        const CodeMetadata& codeMeta,
                                        uint32_t funcIndex,
                                        ValTypeVector* locals) {
  uint32_t numLocalEntries;
  if (!d.readVarU32(&numLocalEntries)) {
    return d.fail("failed to read number of local entries");
  }

  if (!locals->appendAll(codeMeta.getFuncType(funcIndex).args())) {
    return false;
  }

  for (uint32_t i = 0; i < numLocalEntries; i++) {
    uint32_t count;
    if (!d.readVarU32(&count)) {
      return d.fail("failed to read local entry count");
    }

    if (MaxLocals - locals->length() < count) {
      return d.fail("too many locals");
    }

    ValType type;
    if (!d.readValType(*codeMeta.types, codeMeta.features(), &type)) {
      return false;
    }

    if (!locals->appendN(type, count)) {
      return false;
    }
  }

  return true;
}

bool wasm::DecodeValidatedLocalEntries(const TypeContext& types, Decoder& d,
                                       ValTypeVector* locals) {
  uint32_t numLocalEntries;
  MOZ_ALWAYS_TRUE(d.readVarU32(&numLocalEntries));

  for (uint32_t i = 0; i < numLocalEntries; i++) {
    uint32_t count = d.uncheckedReadVarU32();
    MOZ_ASSERT(MaxLocals - locals->length() >= count);
    if (!locals->appendN(d.uncheckedReadValType(types), count)) {
      return false;
    }
  }

  return true;
}

bool wasm::CheckIsSubtypeOf(Decoder& d, const CodeMetadata& codeMeta,
                            size_t opcodeOffset, ResultType subType,
                            ResultType superType) {
  if (subType.length() != superType.length()) {
    UniqueChars error(
        JS_smprintf("type mismatch: expected %zu values, got %zu values",
                    superType.length(), subType.length()));
    if (!error) {
      return false;
    }
    MOZ_ASSERT(!ResultType::isSubTypeOf(subType, superType));
    return d.fail(opcodeOffset, error.get());
  }
  for (uint32_t i = 0; i < subType.length(); i++) {
    StorageType sub = subType[i].storageType();
    StorageType super = superType[i].storageType();
    if (!CheckIsSubtypeOf(d, codeMeta, opcodeOffset, sub, super)) {
      MOZ_ASSERT(!ResultType::isSubTypeOf(subType, superType));
      return false;
    }
  }
  MOZ_ASSERT(ResultType::isSubTypeOf(subType, superType));
  return true;
}

bool wasm::CheckIsSubtypeOf(Decoder& d, const CodeMetadata& codeMeta,
                            size_t opcodeOffset, StorageType subType,
                            StorageType superType) {
  if (StorageType::isSubTypeOf(subType, superType)) {
    return true;
  }

  UniqueChars subText = ToString(subType, codeMeta.types);
  if (!subText) {
    return false;
  }

  UniqueChars superText = ToString(superType, codeMeta.types);
  if (!superText) {
    return false;
  }

  UniqueChars error(
      JS_smprintf("type mismatch: expression has type %s but expected %s",
                  subText.get(), superText.get()));
  if (!error) {
    return false;
  }

  return d.fail(opcodeOffset, error.get());
}

// Function body validation.

template <class T>
bool wasm::ValidateOps(ValidatingOpIter& iter, T& dumper,
                       const CodeMetadata& codeMeta) {
  while (true) {
    OpBytes op;
    if (!iter.readOp(&op)) {
      return false;
    }

    // End instructions get handled differently since we don't actually want to
    // dump the final `end`. Also, Else instructions need to have their
    // indentation managed when dumping.
    if (op.b0 != uint16_t(Op::End)) {
      if (op.b0 == uint64_t(Op::Else)) {
        dumper.endScope();
      }
      dumper.dumpOpBegin(op);
      if (op.b0 == uint64_t(Op::Else)) {
        dumper.startScope();
      }
    }

    Nothing nothing;
    NothingVector nothings{};
    BlockType blockType;
    ResultType resultType;

    switch (op.b0) {
      case uint16_t(Op::End): {
        LabelKind unusedKind;
        if (!iter.readEnd(&unusedKind, &resultType, &nothings, &nothings)) {
          return false;
        }
        iter.popEnd();
        if (iter.controlStackEmpty()) {
          return true;
        }

        // Only dump `end` if it was not the final `end` of the expression.
        dumper.endScope();
        dumper.dumpOpBegin(op);

        break;
      }
      case uint16_t(Op::Nop): {
        if (!iter.readNop()) {
          return false;
        }
        break;
      }
      case uint16_t(Op::Drop): {
        if (!iter.readDrop()) {
          return false;
        }
        break;
      }
      case uint16_t(Op::Call): {
        uint32_t funcIndex;
        NothingVector unusedArgs{};
        if (!iter.readCall(&funcIndex, &unusedArgs)) {
          return false;
        }
        dumper.dumpFuncIndex(funcIndex);
        break;
      }
      case uint16_t(Op::CallIndirect): {
        uint32_t funcTypeIndex, tableIndex;
        NothingVector unusedArgs{};
        if (!iter.readCallIndirect(&funcTypeIndex, &tableIndex, &nothing,
                                   &unusedArgs)) {
          return false;
        }
        dumper.dumpTableIndex(tableIndex);
        dumper.dumpTypeIndex(funcTypeIndex, /*asTypeUse=*/true);
        break;
      }
      case uint16_t(Op::ReturnCall): {
        uint32_t funcIndex;
        NothingVector unusedArgs{};
        if (!iter.readReturnCall(&funcIndex, &unusedArgs)) {
          return false;
        }
        dumper.dumpFuncIndex(funcIndex);
        break;
      }
      case uint16_t(Op::ReturnCallIndirect): {
        uint32_t funcTypeIndex, tableIndex;
        NothingVector unusedArgs{};
        if (!iter.readReturnCallIndirect(&funcTypeIndex, &tableIndex, &nothing,
                                         &unusedArgs)) {
          return false;
        }
        dumper.dumpTableIndex(tableIndex);
        dumper.dumpTypeIndex(funcTypeIndex, /*asTypeUse=*/true);
        break;
      }
      case uint16_t(Op::CallRef): {
        uint32_t funcTypeIndex;
        NothingVector unusedArgs{};
        if (!iter.readCallRef(&funcTypeIndex, &nothing, &unusedArgs)) {
          return false;
        }
        dumper.dumpTypeIndex(funcTypeIndex);
        break;
      }
      case uint16_t(Op::ReturnCallRef): {
        uint32_t funcTypeIndex;
        NothingVector unusedArgs{};
        if (!iter.readReturnCallRef(&funcTypeIndex, &nothing, &unusedArgs)) {
          return false;
        }
        dumper.dumpTypeIndex(funcTypeIndex);
        break;
      }
      case uint16_t(Op::I32Const): {
        int32_t constant;
        if (!iter.readI32Const(&constant)) {
          return false;
        }
        dumper.dumpI32Const(constant);
        break;
      }
      case uint16_t(Op::I64Const): {
        int64_t constant;
        if (!iter.readI64Const(&constant)) {
          return false;
        }
        dumper.dumpI64Const(constant);
        break;
      }
      case uint16_t(Op::F32Const): {
        float constant;
        if (!iter.readF32Const(&constant)) {
          return false;
        }
        dumper.dumpF32Const(constant);
        break;
      }
      case uint16_t(Op::F64Const): {
        double constant;
        if (!iter.readF64Const(&constant)) {
          return false;
        }
        dumper.dumpF64Const(constant);
        break;
      }
      case uint16_t(Op::LocalGet): {
        uint32_t localIndex;
        if (!iter.readGetLocal(&localIndex)) {
          return false;
        }
        dumper.dumpLocalIndex(localIndex);
        break;
      }
      case uint16_t(Op::LocalSet): {
        uint32_t localIndex;
        if (!iter.readSetLocal(&localIndex, &nothing)) {
          return false;
        }
        dumper.dumpLocalIndex(localIndex);
        break;
      }
      case uint16_t(Op::LocalTee): {
        uint32_t localIndex;
        if (!iter.readTeeLocal(&localIndex, &nothing)) {
          return false;
        }
        dumper.dumpLocalIndex(localIndex);
        break;
      }
      case uint16_t(Op::GlobalGet): {
        uint32_t globalIndex;
        if (!iter.readGetGlobal(&globalIndex)) {
          return false;
        }
        dumper.dumpGlobalIndex(globalIndex);
        break;
      }
      case uint16_t(Op::GlobalSet): {
        uint32_t globalIndex;
        if (!iter.readSetGlobal(&globalIndex, &nothing)) {
          return false;
        }
        dumper.dumpGlobalIndex(globalIndex);
        break;
      }
      case uint16_t(Op::TableGet): {
        uint32_t tableIndex;
        if (!iter.readTableGet(&tableIndex, &nothing)) {
          return false;
        }
        dumper.dumpTableIndex(tableIndex);
        break;
      }
      case uint16_t(Op::TableSet): {
        uint32_t tableIndex;
        if (!iter.readTableSet(&tableIndex, &nothing, &nothing)) {
          return false;
        }
        dumper.dumpTableIndex(tableIndex);
        break;
      }
      case uint16_t(Op::SelectNumeric): {
        StackType unused;
        if (!iter.readSelect(/*typed*/ false, &unused, &nothing, &nothing,
                             &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::SelectTyped): {
        StackType type;
        if (!iter.readSelect(/*typed*/ true, &type, &nothing, &nothing,
                             &nothing)) {
          return false;
        }
        dumper.dumpValType(type.valType());
        break;
      }
      case uint16_t(Op::Block): {
        if (!iter.readBlock(&blockType)) {
          return false;
        }
        dumper.dumpBlockType(blockType);
        dumper.startScope();
        break;
      }
      case uint16_t(Op::Loop): {
        if (!iter.readLoop(&blockType)) {
          return false;
        }
        dumper.dumpBlockType(blockType);
        dumper.startScope();
        break;
      }
      case uint16_t(Op::If): {
        if (!iter.readIf(&blockType, &nothing)) {
          return false;
        }
        dumper.dumpBlockType(blockType);
        dumper.startScope();
        break;
      }
      case uint16_t(Op::Else): {
        if (!iter.readElse(&resultType, &resultType, &nothings)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I32Clz):
      case uint16_t(Op::I32Ctz):
      case uint16_t(Op::I32Popcnt): {
        if (!iter.readUnary(ValType::I32, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I64Clz):
      case uint16_t(Op::I64Ctz):
      case uint16_t(Op::I64Popcnt): {
        if (!iter.readUnary(ValType::I64, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::F32Abs):
      case uint16_t(Op::F32Neg):
      case uint16_t(Op::F32Ceil):
      case uint16_t(Op::F32Floor):
      case uint16_t(Op::F32Sqrt):
      case uint16_t(Op::F32Trunc):
      case uint16_t(Op::F32Nearest): {
        if (!iter.readUnary(ValType::F32, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::F64Abs):
      case uint16_t(Op::F64Neg):
      case uint16_t(Op::F64Ceil):
      case uint16_t(Op::F64Floor):
      case uint16_t(Op::F64Sqrt):
      case uint16_t(Op::F64Trunc):
      case uint16_t(Op::F64Nearest): {
        if (!iter.readUnary(ValType::F64, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I32Add):
      case uint16_t(Op::I32Sub):
      case uint16_t(Op::I32Mul):
      case uint16_t(Op::I32DivS):
      case uint16_t(Op::I32DivU):
      case uint16_t(Op::I32RemS):
      case uint16_t(Op::I32RemU):
      case uint16_t(Op::I32And):
      case uint16_t(Op::I32Or):
      case uint16_t(Op::I32Xor):
      case uint16_t(Op::I32Shl):
      case uint16_t(Op::I32ShrS):
      case uint16_t(Op::I32ShrU):
      case uint16_t(Op::I32Rotl):
      case uint16_t(Op::I32Rotr): {
        if (!iter.readBinary(ValType::I32, &nothing, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I64Add):
      case uint16_t(Op::I64Sub):
      case uint16_t(Op::I64Mul):
      case uint16_t(Op::I64DivS):
      case uint16_t(Op::I64DivU):
      case uint16_t(Op::I64RemS):
      case uint16_t(Op::I64RemU):
      case uint16_t(Op::I64And):
      case uint16_t(Op::I64Or):
      case uint16_t(Op::I64Xor):
      case uint16_t(Op::I64Shl):
      case uint16_t(Op::I64ShrS):
      case uint16_t(Op::I64ShrU):
      case uint16_t(Op::I64Rotl):
      case uint16_t(Op::I64Rotr): {
        if (!iter.readBinary(ValType::I64, &nothing, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::F32Add):
      case uint16_t(Op::F32Sub):
      case uint16_t(Op::F32Mul):
      case uint16_t(Op::F32Div):
      case uint16_t(Op::F32Min):
      case uint16_t(Op::F32Max):
      case uint16_t(Op::F32CopySign): {
        if (!iter.readBinary(ValType::F32, &nothing, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::F64Add):
      case uint16_t(Op::F64Sub):
      case uint16_t(Op::F64Mul):
      case uint16_t(Op::F64Div):
      case uint16_t(Op::F64Min):
      case uint16_t(Op::F64Max):
      case uint16_t(Op::F64CopySign): {
        if (!iter.readBinary(ValType::F64, &nothing, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I32Eq):
      case uint16_t(Op::I32Ne):
      case uint16_t(Op::I32LtS):
      case uint16_t(Op::I32LtU):
      case uint16_t(Op::I32LeS):
      case uint16_t(Op::I32LeU):
      case uint16_t(Op::I32GtS):
      case uint16_t(Op::I32GtU):
      case uint16_t(Op::I32GeS):
      case uint16_t(Op::I32GeU): {
        if (!iter.readComparison(ValType::I32, &nothing, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I64Eq):
      case uint16_t(Op::I64Ne):
      case uint16_t(Op::I64LtS):
      case uint16_t(Op::I64LtU):
      case uint16_t(Op::I64LeS):
      case uint16_t(Op::I64LeU):
      case uint16_t(Op::I64GtS):
      case uint16_t(Op::I64GtU):
      case uint16_t(Op::I64GeS):
      case uint16_t(Op::I64GeU): {
        if (!iter.readComparison(ValType::I64, &nothing, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::F32Eq):
      case uint16_t(Op::F32Ne):
      case uint16_t(Op::F32Lt):
      case uint16_t(Op::F32Le):
      case uint16_t(Op::F32Gt):
      case uint16_t(Op::F32Ge): {
        if (!iter.readComparison(ValType::F32, &nothing, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::F64Eq):
      case uint16_t(Op::F64Ne):
      case uint16_t(Op::F64Lt):
      case uint16_t(Op::F64Le):
      case uint16_t(Op::F64Gt):
      case uint16_t(Op::F64Ge): {
        if (!iter.readComparison(ValType::F64, &nothing, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I32Eqz): {
        if (!iter.readConversion(ValType::I32, ValType::I32, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I64Eqz):
      case uint16_t(Op::I32WrapI64): {
        if (!iter.readConversion(ValType::I64, ValType::I32, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I32TruncF32S):
      case uint16_t(Op::I32TruncF32U):
      case uint16_t(Op::I32ReinterpretF32): {
        if (!iter.readConversion(ValType::F32, ValType::I32, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I32TruncF64S):
      case uint16_t(Op::I32TruncF64U): {
        if (!iter.readConversion(ValType::F64, ValType::I32, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I64ExtendI32S):
      case uint16_t(Op::I64ExtendI32U): {
        if (!iter.readConversion(ValType::I32, ValType::I64, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I64TruncF32S):
      case uint16_t(Op::I64TruncF32U): {
        if (!iter.readConversion(ValType::F32, ValType::I64, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I64TruncF64S):
      case uint16_t(Op::I64TruncF64U):
      case uint16_t(Op::I64ReinterpretF64): {
        if (!iter.readConversion(ValType::F64, ValType::I64, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::F32ConvertI32S):
      case uint16_t(Op::F32ConvertI32U):
      case uint16_t(Op::F32ReinterpretI32): {
        if (!iter.readConversion(ValType::I32, ValType::F32, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::F32ConvertI64S):
      case uint16_t(Op::F32ConvertI64U): {
        if (!iter.readConversion(ValType::I64, ValType::F32, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::F32DemoteF64): {
        if (!iter.readConversion(ValType::F64, ValType::F32, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::F64ConvertI32S):
      case uint16_t(Op::F64ConvertI32U): {
        if (!iter.readConversion(ValType::I32, ValType::F64, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::F64ConvertI64S):
      case uint16_t(Op::F64ConvertI64U):
      case uint16_t(Op::F64ReinterpretI64): {
        if (!iter.readConversion(ValType::I64, ValType::F64, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::F64PromoteF32): {
        if (!iter.readConversion(ValType::F32, ValType::F64, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I32Extend8S):
      case uint16_t(Op::I32Extend16S): {
        if (!iter.readConversion(ValType::I32, ValType::I32, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I64Extend8S):
      case uint16_t(Op::I64Extend16S):
      case uint16_t(Op::I64Extend32S): {
        if (!iter.readConversion(ValType::I64, ValType::I64, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::I32Load8S):
      case uint16_t(Op::I32Load8U): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readLoad(ValType::I32, 1, &addr)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::I32Load16S):
      case uint16_t(Op::I32Load16U): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readLoad(ValType::I32, 2, &addr)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::I32Load): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readLoad(ValType::I32, 4, &addr)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::I64Load8S):
      case uint16_t(Op::I64Load8U): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readLoad(ValType::I64, 1, &addr)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::I64Load16S):
      case uint16_t(Op::I64Load16U): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readLoad(ValType::I64, 2, &addr)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::I64Load32S):
      case uint16_t(Op::I64Load32U): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readLoad(ValType::I64, 4, &addr)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::I64Load): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readLoad(ValType::I64, 8, &addr)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::F32Load): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readLoad(ValType::F32, 4, &addr)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::F64Load): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readLoad(ValType::F64, 8, &addr)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::I32Store8): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readStore(ValType::I32, 1, &addr, &nothing)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::I32Store16): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readStore(ValType::I32, 2, &addr, &nothing)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::I32Store): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readStore(ValType::I32, 4, &addr, &nothing)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::I64Store8): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readStore(ValType::I64, 1, &addr, &nothing)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::I64Store16): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readStore(ValType::I64, 2, &addr, &nothing)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::I64Store32): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readStore(ValType::I64, 4, &addr, &nothing)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::I64Store): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readStore(ValType::I64, 8, &addr, &nothing)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::F32Store): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readStore(ValType::F32, 4, &addr, &nothing)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::F64Store): {
        LinearMemoryAddress<Nothing> addr;
        if (!iter.readStore(ValType::F64, 8, &addr, &nothing)) {
          return false;
        }
        dumper.dumpLinearMemoryAddress(addr);
        break;
      }
      case uint16_t(Op::MemoryGrow): {
        uint32_t memoryIndex;
        if (!iter.readMemoryGrow(&memoryIndex, &nothing)) {
          return false;
        }
        dumper.dumpMemoryIndex(memoryIndex);
        break;
      }
      case uint16_t(Op::MemorySize): {
        uint32_t memoryIndex;
        if (!iter.readMemorySize(&memoryIndex)) {
          return false;
        }
        dumper.dumpMemoryIndex(memoryIndex);
        break;
      }
      case uint16_t(Op::Br): {
        uint32_t depth;
        if (!iter.readBr(&depth, &resultType, &nothings)) {
          return false;
        }
        dumper.dumpBlockDepth(depth);
        break;
      }
      case uint16_t(Op::BrIf): {
        uint32_t depth;
        if (!iter.readBrIf(&depth, &resultType, &nothings, &nothing)) {
          return false;
        }
        dumper.dumpBlockDepth(depth);
        break;
      }
      case uint16_t(Op::BrTable): {
        Uint32Vector depths;
        uint32_t defaultDepth;
        if (!iter.readBrTable(&depths, &defaultDepth, &resultType, &nothings,
                              &nothing)) {
          return false;
        }
        dumper.dumpBlockDepths(depths);
        dumper.dumpBlockDepth(defaultDepth);
        break;
      }
      case uint16_t(Op::Return): {
        if (!iter.readReturn(&nothings)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::Unreachable): {
        if (!iter.readUnreachable()) {
          return false;
        }
        break;
      }
#ifdef ENABLE_WASM_JSPI
      case uint16_t(Op::ContNew): {
        if (!codeMeta.stackSwitchingEnabled()) {
          return iter.unrecognizedOpcode(&op);
        }
        uint32_t contTypeIndex;
        if (!iter.readContNew(&contTypeIndex, &nothing)) {
          return false;
        }
        dumper.dumpTypeIndex(contTypeIndex);
        break;
      }
      case uint16_t(Op::ContBind): {
        if (!codeMeta.stackSwitchingEnabled()) {
          return iter.unrecognizedOpcode(&op);
        }
        uint32_t inputContTypeIndex;
        uint32_t outputContTypeIndex;
        if (!iter.readContBind(&inputContTypeIndex, &outputContTypeIndex,
                               &nothings, &nothing)) {
          return false;
        }
        dumper.dumpTypeIndex(inputContTypeIndex);
        dumper.dumpTypeIndex(outputContTypeIndex);
        break;
      }
      case uint16_t(Op::Suspend): {
        if (!codeMeta.stackSwitchingEnabled()) {
          return iter.unrecognizedOpcode(&op);
        }
        uint32_t tagIndex;
        if (!iter.readSuspend(&tagIndex, &nothings)) {
          return false;
        }
        dumper.dumpTagIndex(tagIndex);
        break;
      }
      case uint16_t(Op::Resume): {
        if (!codeMeta.stackSwitchingEnabled()) {
          return iter.unrecognizedOpcode(&op);
        }
        HandlerExprVector handlers;
        uint32_t contTypeIndex;
        if (!iter.readResume(&contTypeIndex, &handlers, &nothings, &nothing)) {
          return false;
        }
        dumper.dumpTypeIndex(contTypeIndex);
        break;
      }
      case uint16_t(Op::ResumeThrow): {
        if (!codeMeta.stackSwitchingEnabled()) {
          return iter.unrecognizedOpcode(&op);
        }
        HandlerExprVector handlers;
        uint32_t contTypeIndex;
        uint32_t tagIndex;
        if (!iter.readResumeThrow(&contTypeIndex, &tagIndex, &handlers,
                                  &nothings, &nothing)) {
          return false;
        }
        dumper.dumpTypeIndex(contTypeIndex);
        dumper.dumpTagIndex(tagIndex);
        break;
      }
      case uint16_t(Op::ResumeThrowRef): {
        if (!codeMeta.stackSwitchingEnabled()) {
          return iter.unrecognizedOpcode(&op);
        }
        HandlerExprVector handlers;
        uint32_t contTypeIndex;
        if (!iter.readResumeThrowRef(&contTypeIndex, &handlers, &nothing,
                                     &nothing)) {
          return false;
        }
        dumper.dumpTypeIndex(contTypeIndex);
        break;
      }
      case uint16_t(Op::Switch): {
        if (!codeMeta.stackSwitchingEnabled()) {
          return iter.unrecognizedOpcode(&op);
        }
        uint32_t contTypeIndex;
        uint32_t tagIndex;
        if (!iter.readSwitch(&contTypeIndex, &tagIndex, &nothings, &nothing)) {
          return false;
        }
        dumper.dumpTypeIndex(contTypeIndex);
        dumper.dumpTagIndex(tagIndex);
        break;
      }
#endif  // ENABLE_WASM_JSPI
      case uint16_t(Op::GcPrefix): {
        switch (op.b1) {
          case uint32_t(GcOp::StructNew): {
            uint32_t typeIndex;
            NothingVector unusedArgs{};
            if (!iter.readStructNew(&typeIndex, &unusedArgs)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            break;
          }
          case uint32_t(GcOp::StructNewDefault): {
            uint32_t typeIndex;
            if (!iter.readStructNewDefault(&typeIndex)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            break;
          }
          case uint32_t(GcOp::StructGet): {
            uint32_t typeIndex, fieldIndex;
            if (!iter.readStructGet(&typeIndex, &fieldIndex,
                                    FieldWideningOp::None, &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            dumper.dumpFieldIndex(fieldIndex);
            break;
          }
          case uint32_t(GcOp::StructGetS): {
            uint32_t typeIndex, fieldIndex;
            if (!iter.readStructGet(&typeIndex, &fieldIndex,
                                    FieldWideningOp::Signed, &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            dumper.dumpFieldIndex(fieldIndex);
            break;
          }
          case uint32_t(GcOp::StructGetU): {
            uint32_t typeIndex, fieldIndex;
            if (!iter.readStructGet(&typeIndex, &fieldIndex,
                                    FieldWideningOp::Unsigned, &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            dumper.dumpFieldIndex(fieldIndex);
            break;
          }
          case uint32_t(GcOp::StructSet): {
            uint32_t typeIndex, fieldIndex;
            if (!iter.readStructSet(&typeIndex, &fieldIndex, &nothing,
                                    &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            dumper.dumpFieldIndex(fieldIndex);
            break;
          }
          case uint32_t(GcOp::ArrayNew): {
            uint32_t typeIndex;
            if (!iter.readArrayNew(&typeIndex, &nothing, &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            break;
          }
          case uint32_t(GcOp::ArrayNewFixed): {
            uint32_t typeIndex, numElements;
            if (!iter.readArrayNewFixed(&typeIndex, &numElements, &nothings)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            dumper.dumpNumElements(numElements);
            break;
          }
          case uint32_t(GcOp::ArrayNewDefault): {
            uint32_t typeIndex;
            if (!iter.readArrayNewDefault(&typeIndex, &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            break;
          }
          case uint32_t(GcOp::ArrayNewData): {
            uint32_t typeIndex, dataIndex;
            if (!iter.readArrayNewData(&typeIndex, &dataIndex, &nothing,
                                       &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            dumper.dumpDataIndex(dataIndex);
            break;
          }
          case uint32_t(GcOp::ArrayNewElem): {
            uint32_t typeIndex, elemIndex;
            if (!iter.readArrayNewElem(&typeIndex, &elemIndex, &nothing,
                                       &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            dumper.dumpElemIndex(elemIndex);
            break;
          }
          case uint32_t(GcOp::ArrayInitData): {
            uint32_t typeIndex, dataIndex;
            if (!iter.readArrayInitData(&typeIndex, &dataIndex, &nothing,
                                        &nothing, &nothing, &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            dumper.dumpDataIndex(dataIndex);
            break;
          }
          case uint32_t(GcOp::ArrayInitElem): {
            uint32_t typeIndex, elemIndex;
            if (!iter.readArrayInitElem(&typeIndex, &elemIndex, &nothing,
                                        &nothing, &nothing, &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            dumper.dumpElemIndex(elemIndex);
            break;
          }
          case uint32_t(GcOp::ArrayGet): {
            uint32_t typeIndex;
            if (!iter.readArrayGet(&typeIndex, FieldWideningOp::None, &nothing,
                                   &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            break;
          }
          case uint32_t(GcOp::ArrayGetS): {
            uint32_t typeIndex;
            if (!iter.readArrayGet(&typeIndex, FieldWideningOp::Signed,
                                   &nothing, &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            break;
          }
          case uint32_t(GcOp::ArrayGetU): {
            uint32_t typeIndex;
            if (!iter.readArrayGet(&typeIndex, FieldWideningOp::Unsigned,
                                   &nothing, &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            break;
          }
          case uint32_t(GcOp::ArraySet): {
            uint32_t typeIndex;
            if (!iter.readArraySet(&typeIndex, &nothing, &nothing, &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            break;
          }
          case uint32_t(GcOp::ArrayLen): {
            if (!iter.readArrayLen(&nothing)) {
              return false;
            }
            break;
          }
          case uint32_t(GcOp::ArrayCopy): {
            uint32_t dstArrayTypeIndex;
            uint32_t srcArrayTypeIndex;
            if (!iter.readArrayCopy(&dstArrayTypeIndex, &srcArrayTypeIndex,
                                    &nothing, &nothing, &nothing, &nothing,
                                    &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(dstArrayTypeIndex);
            dumper.dumpTypeIndex(srcArrayTypeIndex);
            break;
          }
          case uint32_t(GcOp::ArrayFill): {
            uint32_t typeIndex;
            if (!iter.readArrayFill(&typeIndex, &nothing, &nothing, &nothing,
                                    &nothing)) {
              return false;
            }
            dumper.dumpTypeIndex(typeIndex);
            break;
          }
          case uint32_t(GcOp::RefI31): {
            if (!iter.readConversion(ValType::I32,
                                     ValType(RefType::i31().asNonNullable()),
                                     &nothing)) {
              return false;
            }
            break;
          }
          case uint32_t(GcOp::I31GetS): {
            if (!iter.readConversion(ValType(RefType::i31()), ValType::I32,
                                     &nothing)) {
              return false;
            }
            break;
          }
          case uint32_t(GcOp::I31GetU): {
            if (!iter.readConversion(ValType(RefType::i31()), ValType::I32,
                                     &nothing)) {
              return false;
            }
            break;
          }
          case uint16_t(GcOp::RefTest): {
            RefType srcType;
            RefType destType;
            if (!iter.readRefTest(false, &srcType, &destType, &nothing)) {
              return false;
            }
            dumper.dumpRefType(destType);
            break;
          }
          case uint16_t(GcOp::RefTestNull): {
            RefType srcType;
            RefType destType;
            if (!iter.readRefTest(true, &srcType, &destType, &nothing)) {
              return false;
            }
            dumper.dumpRefType(srcType);
            dumper.dumpRefType(destType);
            break;
          }
          case uint16_t(GcOp::RefCast): {
            RefType srcType;
            RefType destType;
            if (!iter.readRefCast(false, &srcType, &destType, &nothing)) {
              return false;
            }
            dumper.dumpRefType(destType);
            break;
          }
          case uint16_t(GcOp::RefCastNull): {
            RefType srcType;
            RefType destType;
            if (!iter.readRefCast(true, &srcType, &destType, &nothing)) {
              return false;
            }
            dumper.dumpRefType(destType);
            break;
          }
          case uint16_t(GcOp::BrOnCast): {
            uint32_t relativeDepth;
            RefType srcType;
            RefType destType;
            if (!iter.readBrOnCast(true, &relativeDepth, &srcType, &destType,
                                   &resultType, &nothings)) {
              return false;
            }
            dumper.dumpBlockDepth(relativeDepth);
            dumper.dumpRefType(srcType);
            dumper.dumpRefType(destType);
            break;
          }
          case uint16_t(GcOp::BrOnCastFail): {
            uint32_t relativeDepth;
            RefType srcType;
            RefType destType;
            if (!iter.readBrOnCast(false, &relativeDepth, &srcType, &destType,
                                   &resultType, &nothings)) {
              return false;
            }
            dumper.dumpBlockDepth(relativeDepth);
            dumper.dumpRefType(srcType);
            dumper.dumpRefType(destType);
            break;
          }
          case uint16_t(GcOp::AnyConvertExtern): {
            if (!iter.readRefConversion(RefType::extern_(), RefType::any(),
                                        &nothing)) {
              return false;
            }
            break;
          }
          case uint16_t(GcOp::ExternConvertAny): {
            if (!iter.readRefConversion(RefType::any(), RefType::extern_(),
                                        &nothing)) {
              return false;
            }
            break;
          }
          default:
            return iter.unrecognizedOpcode(&op);
        }
        break;
      }

#ifdef ENABLE_JIT_SIMD
      case uint16_t(Op::SimdPrefix): {
        if (!codeMeta.simdAvailable()) {
          return iter.unrecognizedOpcode(&op);
        }
        uint32_t laneIndex;
        switch (op.b1) {
          case uint32_t(SimdOp::I8x16ExtractLaneS):
          case uint32_t(SimdOp::I8x16ExtractLaneU): {
            if (!iter.readExtractLane(ValType::I32, 16, &laneIndex, &nothing)) {
              return false;
            }
            dumper.dumpLaneIndex(laneIndex);
            break;
          }
          case uint32_t(SimdOp::I16x8ExtractLaneS):
          case uint32_t(SimdOp::I16x8ExtractLaneU): {
            if (!iter.readExtractLane(ValType::I32, 8, &laneIndex, &nothing)) {
              return false;
            }
            dumper.dumpLaneIndex(laneIndex);
            break;
          }
          case uint32_t(SimdOp::I32x4ExtractLane): {
            if (!iter.readExtractLane(ValType::I32, 4, &laneIndex, &nothing)) {
              return false;
            }
            dumper.dumpLaneIndex(laneIndex);
            break;
          }
          case uint32_t(SimdOp::I64x2ExtractLane): {
            if (!iter.readExtractLane(ValType::I64, 2, &laneIndex, &nothing)) {
              return false;
            }
            dumper.dumpLaneIndex(laneIndex);
            break;
          }
          case uint32_t(SimdOp::F32x4ExtractLane): {
            if (!iter.readExtractLane(ValType::F32, 4, &laneIndex, &nothing)) {
              return false;
            }
            dumper.dumpLaneIndex(laneIndex);
            break;
          }
          case uint32_t(SimdOp::F64x2ExtractLane): {
            if (!iter.readExtractLane(ValType::F64, 2, &laneIndex, &nothing)) {
              return false;
            }
            dumper.dumpLaneIndex(laneIndex);
            break;
          }

          case uint32_t(SimdOp::I8x16Splat):
          case uint32_t(SimdOp::I16x8Splat):
          case uint32_t(SimdOp::I32x4Splat): {
            if (!iter.readConversion(ValType::I32, ValType::V128, &nothing)) {
              return false;
            }
            break;
          }
          case uint32_t(SimdOp::I64x2Splat): {
            if (!iter.readConversion(ValType::I64, ValType::V128, &nothing)) {
              return false;
            }
            break;
          }
          case uint32_t(SimdOp::F32x4Splat): {
            if (!iter.readConversion(ValType::F32, ValType::V128, &nothing)) {
              return false;
            }
            break;
          }
          case uint32_t(SimdOp::F64x2Splat): {
            if (!iter.readConversion(ValType::F64, ValType::V128, &nothing)) {
              return false;
            }
            break;
          }

          case uint32_t(SimdOp::V128AnyTrue):
          case uint32_t(SimdOp::I8x16AllTrue):
          case uint32_t(SimdOp::I16x8AllTrue):
          case uint32_t(SimdOp::I32x4AllTrue):
          case uint32_t(SimdOp::I64x2AllTrue):
          case uint32_t(SimdOp::I8x16Bitmask):
          case uint32_t(SimdOp::I16x8Bitmask):
          case uint32_t(SimdOp::I32x4Bitmask):
          case uint32_t(SimdOp::I64x2Bitmask): {
            if (!iter.readConversion(ValType::V128, ValType::I32, &nothing)) {
              return false;
            }
            break;
          }

          case uint32_t(SimdOp::I8x16ReplaceLane): {
            if (!iter.readReplaceLane(ValType::I32, 16, &laneIndex, &nothing,
                                      &nothing)) {
              return false;
            }
            dumper.dumpLaneIndex(laneIndex);
            break;
          }
          case uint32_t(SimdOp::I16x8ReplaceLane): {
            if (!iter.readReplaceLane(ValType::I32, 8, &laneIndex, &nothing,
                                      &nothing)) {
              return false;
            }
            dumper.dumpLaneIndex(laneIndex);
            break;
          }
          case uint32_t(SimdOp::I32x4ReplaceLane): {
            if (!iter.readReplaceLane(ValType::I32, 4, &laneIndex, &nothing,
                                      &nothing)) {
              return false;
            }
            dumper.dumpLaneIndex(laneIndex);
            break;
          }
          case uint32_t(SimdOp::I64x2ReplaceLane): {
            if (!iter.readReplaceLane(ValType::I64, 2, &laneIndex, &nothing,
                                      &nothing)) {
              return false;
            }
            dumper.dumpLaneIndex(laneIndex);
            break;
          }
          case uint32_t(SimdOp::F32x4ReplaceLane): {
            if (!iter.readReplaceLane(ValType::F32, 4, &laneIndex, &nothing,
                                      &nothing)) {
              return false;
            }
            dumper.dumpLaneIndex(laneIndex);
            break;
          }
          case uint32_t(SimdOp::F64x2ReplaceLane): {
            if (!iter.readReplaceLane(ValType::F64, 2, &laneIndex, &nothing,
                                      &nothing)) {
              return false;
            }
            dumper.dumpLaneIndex(laneIndex);
            break;
          }

          case uint32_t(SimdOp::I8x16Eq):
          case uint32_t(SimdOp::I8x16Ne):
          case uint32_t(SimdOp::I8x16LtS):
          case uint32_t(SimdOp::I8x16LtU):
          case uint32_t(SimdOp::I8x16GtS):
          case uint32_t(SimdOp::I8x16GtU):
          case uint32_t(SimdOp::I8x16LeS):
          case uint32_t(SimdOp::I8x16LeU):
          case uint32_t(SimdOp::I8x16GeS):
          case uint32_t(SimdOp::I8x16GeU):
          case uint32_t(SimdOp::I16x8Eq):
          case uint32_t(SimdOp::I16x8Ne):
          case uint32_t(SimdOp::I16x8LtS):
          case uint32_t(SimdOp::I16x8LtU):
          case uint32_t(SimdOp::I16x8GtS):
          case uint32_t(SimdOp::I16x8GtU):
          case uint32_t(SimdOp::I16x8LeS):
          case uint32_t(SimdOp::I16x8LeU):
          case uint32_t(SimdOp::I16x8GeS):
          case uint32_t(SimdOp::I16x8GeU):
          case uint32_t(SimdOp::I32x4Eq):
          case uint32_t(SimdOp::I32x4Ne):
          case uint32_t(SimdOp::I32x4LtS):
          case uint32_t(SimdOp::I32x4LtU):
          case uint32_t(SimdOp::I32x4GtS):
          case uint32_t(SimdOp::I32x4GtU):
          case uint32_t(SimdOp::I32x4LeS):
          case uint32_t(SimdOp::I32x4LeU):
          case uint32_t(SimdOp::I32x4GeS):
          case uint32_t(SimdOp::I32x4GeU):
          case uint32_t(SimdOp::I64x2Eq):
          case uint32_t(SimdOp::I64x2Ne):
          case uint32_t(SimdOp::I64x2LtS):
          case uint32_t(SimdOp::I64x2GtS):
          case uint32_t(SimdOp::I64x2LeS):
          case uint32_t(SimdOp::I64x2GeS):
          case uint32_t(SimdOp::F32x4Eq):
          case uint32_t(SimdOp::F32x4Ne):
          case uint32_t(SimdOp::F32x4Lt):
          case uint32_t(SimdOp::F32x4Gt):
          case uint32_t(SimdOp::F32x4Le):
          case uint32_t(SimdOp::F32x4Ge):
          case uint32_t(SimdOp::F64x2Eq):
          case uint32_t(SimdOp::F64x2Ne):
          case uint32_t(SimdOp::F64x2Lt):
          case uint32_t(SimdOp::F64x2Gt):
          case uint32_t(SimdOp::F64x2Le):
          case uint32_t(SimdOp::F64x2Ge):
          case uint32_t(SimdOp::V128And):
          case uint32_t(SimdOp::V128Or):
          case uint32_t(SimdOp::V128Xor):
          case uint32_t(SimdOp::V128AndNot):
          case uint32_t(SimdOp::I8x16AvgrU):
          case uint32_t(SimdOp::I16x8AvgrU):
          case uint32_t(SimdOp::I8x16Add):
          case uint32_t(SimdOp::I8x16AddSatS):
          case uint32_t(SimdOp::I8x16AddSatU):
          case uint32_t(SimdOp::I8x16Sub):
          case uint32_t(SimdOp::I8x16SubSatS):
          case uint32_t(SimdOp::I8x16SubSatU):
          case uint32_t(SimdOp::I8x16MinS):
          case uint32_t(SimdOp::I8x16MinU):
          case uint32_t(SimdOp::I8x16MaxS):
          case uint32_t(SimdOp::I8x16MaxU):
          case uint32_t(SimdOp::I16x8Add):
          case uint32_t(SimdOp::I16x8AddSatS):
          case uint32_t(SimdOp::I16x8AddSatU):
          case uint32_t(SimdOp::I16x8Sub):
          case uint32_t(SimdOp::I16x8SubSatS):
          case uint32_t(SimdOp::I16x8SubSatU):
          case uint32_t(SimdOp::I16x8Mul):
          case uint32_t(SimdOp::I16x8MinS):
          case uint32_t(SimdOp::I16x8MinU):
          case uint32_t(SimdOp::I16x8MaxS):
          case uint32_t(SimdOp::I16x8MaxU):
          case uint32_t(SimdOp::I32x4Add):
          case uint32_t(SimdOp::I32x4Sub):
          case uint32_t(SimdOp::I32x4Mul):
          case uint32_t(SimdOp::I32x4MinS):
          case uint32_t(SimdOp::I32x4MinU):
          case uint32_t(SimdOp::I32x4MaxS):
          case uint32_t(SimdOp::I32x4MaxU):
          case uint32_t(SimdOp::I64x2Add):
          case uint32_t(SimdOp::I64x2Sub):
          case uint32_t(SimdOp::I64x2Mul):
          case uint32_t(SimdOp::F32x4Add):
          case uint32_t(SimdOp::F32x4Sub):
          case uint32_t(SimdOp::F32x4Mul):
          case uint32_t(SimdOp::F32x4Div):
          case uint32_t(SimdOp::F32x4Min):
          case uint32_t(SimdOp::F32x4Max):
          case uint32_t(SimdOp::F64x2Add):
          case uint32_t(SimdOp::F64x2Sub):
          case uint32_t(SimdOp::F64x2Mul):
          case uint32_t(SimdOp::F64x2Div):
          case uint32_t(SimdOp::F64x2Min):
          case uint32_t(SimdOp::F64x2Max):
          case uint32_t(SimdOp::I8x16NarrowI16x8S):
          case uint32_t(SimdOp::I8x16NarrowI16x8U):
          case uint32_t(SimdOp::I16x8NarrowI32x4S):
          case uint32_t(SimdOp::I16x8NarrowI32x4U):
          case uint32_t(SimdOp::I8x16Swizzle):
          case uint32_t(SimdOp::F32x4PMax):
          case uint32_t(SimdOp::F32x4PMin):
          case uint32_t(SimdOp::F64x2PMax):
          case uint32_t(SimdOp::F64x2PMin):
          case uint32_t(SimdOp::I32x4DotI16x8S):
          case uint32_t(SimdOp::I16x8ExtmulLowI8x16S):
          case uint32_t(SimdOp::I16x8ExtmulHighI8x16S):
          case uint32_t(SimdOp::I16x8ExtmulLowI8x16U):
          case uint32_t(SimdOp::I16x8ExtmulHighI8x16U):
          case uint32_t(SimdOp::I32x4ExtmulLowI16x8S):
          case uint32_t(SimdOp::I32x4ExtmulHighI16x8S):
          case uint32_t(SimdOp::I32x4ExtmulLowI16x8U):
          case uint32_t(SimdOp::I32x4ExtmulHighI16x8U):
          case uint32_t(SimdOp::I64x2ExtmulLowI32x4S):
          case uint32_t(SimdOp::I64x2ExtmulHighI32x4S):
          case uint32_t(SimdOp::I64x2ExtmulLowI32x4U):
          case uint32_t(SimdOp::I64x2ExtmulHighI32x4U):
          case uint32_t(SimdOp::I16x8Q15MulrSatS): {
            if (!iter.readBinary(ValType::V128, &nothing, &nothing)) {
              return false;
            }
            break;
          }

          case uint32_t(SimdOp::I8x16Neg):
          case uint32_t(SimdOp::I16x8Neg):
          case uint32_t(SimdOp::I16x8ExtendLowI8x16S):
          case uint32_t(SimdOp::I16x8ExtendHighI8x16S):
          case uint32_t(SimdOp::I16x8ExtendLowI8x16U):
          case uint32_t(SimdOp::I16x8ExtendHighI8x16U):
          case uint32_t(SimdOp::I32x4Neg):
          case uint32_t(SimdOp::I32x4ExtendLowI16x8S):
          case uint32_t(SimdOp::I32x4ExtendHighI16x8S):
          case uint32_t(SimdOp::I32x4ExtendLowI16x8U):
          case uint32_t(SimdOp::I32x4ExtendHighI16x8U):
          case uint32_t(SimdOp::I32x4TruncSatF32x4S):
          case uint32_t(SimdOp::I32x4TruncSatF32x4U):
          case uint32_t(SimdOp::I64x2Neg):
          case uint32_t(SimdOp::I64x2ExtendLowI32x4S):
          case uint32_t(SimdOp::I64x2ExtendHighI32x4S):
          case uint32_t(SimdOp::I64x2ExtendLowI32x4U):
          case uint32_t(SimdOp::I64x2ExtendHighI32x4U):
          case uint32_t(SimdOp::F32x4Abs):
          case uint32_t(SimdOp::F32x4Neg):
          case uint32_t(SimdOp::F32x4Sqrt):
          case uint32_t(SimdOp::F32x4ConvertI32x4S):
          case uint32_t(SimdOp::F32x4ConvertI32x4U):
          case uint32_t(SimdOp::F64x2Abs):
          case uint32_t(SimdOp::F64x2Neg):
          case uint32_t(SimdOp::F64x2Sqrt):
          case uint32_t(SimdOp::V128Not):
          case uint32_t(SimdOp::I8x16Popcnt):
          case uint32_t(SimdOp::I8x16Abs):
          case uint32_t(SimdOp::I16x8Abs):
          case uint32_t(SimdOp::I32x4Abs):
          case uint32_t(SimdOp::I64x2Abs):
          case uint32_t(SimdOp::F32x4Ceil):
          case uint32_t(SimdOp::F32x4Floor):
          case uint32_t(SimdOp::F32x4Trunc):
          case uint32_t(SimdOp::F32x4Nearest):
          case uint32_t(SimdOp::F64x2Ceil):
          case uint32_t(SimdOp::F64x2Floor):
          case uint32_t(SimdOp::F64x2Trunc):
          case uint32_t(SimdOp::F64x2Nearest):
          case uint32_t(SimdOp::F32x4DemoteF64x2Zero):
          case uint32_t(SimdOp::F64x2PromoteLowF32x4):
          case uint32_t(SimdOp::F64x2ConvertLowI32x4S):
          case uint32_t(SimdOp::F64x2ConvertLowI32x4U):
          case uint32_t(SimdOp::I32x4TruncSatF64x2SZero):
          case uint32_t(SimdOp::I32x4TruncSatF64x2UZero):
          case uint32_t(SimdOp::I16x8ExtaddPairwiseI8x16S):
          case uint32_t(SimdOp::I16x8ExtaddPairwiseI8x16U):
          case uint32_t(SimdOp::I32x4ExtaddPairwiseI16x8S):
          case uint32_t(SimdOp::I32x4ExtaddPairwiseI16x8U): {
            if (!iter.readUnary(ValType::V128, &nothing)) {
              return false;
            }
            break;
          }

          case uint32_t(SimdOp::I8x16Shl):
          case uint32_t(SimdOp::I8x16ShrS):
          case uint32_t(SimdOp::I8x16ShrU):
          case uint32_t(SimdOp::I16x8Shl):
          case uint32_t(SimdOp::I16x8ShrS):
          case uint32_t(SimdOp::I16x8ShrU):
          case uint32_t(SimdOp::I32x4Shl):
          case uint32_t(SimdOp::I32x4ShrS):
          case uint32_t(SimdOp::I32x4ShrU):
          case uint32_t(SimdOp::I64x2Shl):
          case uint32_t(SimdOp::I64x2ShrS):
          case uint32_t(SimdOp::I64x2ShrU): {
            if (!iter.readVectorShift(&nothing, &nothing)) {
              return false;
            }
            break;
          }

          case uint32_t(SimdOp::V128Bitselect): {
            if (!iter.readTernary(ValType::V128, &nothing, &nothing,
                                  &nothing)) {
              return false;
            }
            break;
          }

          case uint32_t(SimdOp::I8x16Shuffle): {
            V128 mask;
            if (!iter.readVectorShuffle(&nothing, &nothing, &mask)) {
              return false;
            }
            dumper.dumpVectorMask(mask);
            break;
          }

          case uint32_t(SimdOp::V128Const): {
            V128 constant;
            if (!iter.readV128Const(&constant)) {
              return false;
            }
            dumper.dumpV128Const(constant);
            break;
          }

          case uint32_t(SimdOp::V128Load): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readLoad(ValType::V128, 16, &addr)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }

          case uint32_t(SimdOp::V128Load8Splat): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readLoadSplat(1, &addr)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }

          case uint32_t(SimdOp::V128Load16Splat): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readLoadSplat(2, &addr)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }

          case uint32_t(SimdOp::V128Load32Splat): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readLoadSplat(4, &addr)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }

          case uint32_t(SimdOp::V128Load64Splat): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readLoadSplat(8, &addr)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }

          case uint32_t(SimdOp::V128Load8x8S):
          case uint32_t(SimdOp::V128Load8x8U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readLoadExtend(&addr)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }

          case uint32_t(SimdOp::V128Load16x4S):
          case uint32_t(SimdOp::V128Load16x4U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readLoadExtend(&addr)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }

          case uint32_t(SimdOp::V128Load32x2S):
          case uint32_t(SimdOp::V128Load32x2U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readLoadExtend(&addr)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }

          case uint32_t(SimdOp::V128Store): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readStore(ValType::V128, 16, &addr, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }

          case uint32_t(SimdOp::V128Load32Zero): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readLoadSplat(4, &addr)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }

          case uint32_t(SimdOp::V128Load64Zero): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readLoadSplat(8, &addr)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }

          case uint32_t(SimdOp::V128Load8Lane): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readLoadLane(1, &addr, &laneIndex, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            dumper.dumpLaneIndex(laneIndex);
            break;
          }

          case uint32_t(SimdOp::V128Load16Lane): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readLoadLane(2, &addr, &laneIndex, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            dumper.dumpLaneIndex(laneIndex);
            break;
          }

          case uint32_t(SimdOp::V128Load32Lane): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readLoadLane(4, &addr, &laneIndex, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            dumper.dumpLaneIndex(laneIndex);
            break;
          }

          case uint32_t(SimdOp::V128Load64Lane): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readLoadLane(8, &addr, &laneIndex, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            dumper.dumpLaneIndex(laneIndex);
            break;
          }

          case uint32_t(SimdOp::V128Store8Lane): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readStoreLane(1, &addr, &laneIndex, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            dumper.dumpLaneIndex(laneIndex);
            break;
          }

          case uint32_t(SimdOp::V128Store16Lane): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readStoreLane(2, &addr, &laneIndex, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            dumper.dumpLaneIndex(laneIndex);
            break;
          }

          case uint32_t(SimdOp::V128Store32Lane): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readStoreLane(4, &addr, &laneIndex, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            dumper.dumpLaneIndex(laneIndex);
            break;
          }

          case uint32_t(SimdOp::V128Store64Lane): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readStoreLane(8, &addr, &laneIndex, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            dumper.dumpLaneIndex(laneIndex);
            break;
          }

#  ifdef ENABLE_WASM_RELAXED_SIMD
          case uint32_t(SimdOp::F32x4RelaxedMadd):
          case uint32_t(SimdOp::F32x4RelaxedNmadd):
          case uint32_t(SimdOp::F64x2RelaxedMadd):
          case uint32_t(SimdOp::F64x2RelaxedNmadd):
          case uint32_t(SimdOp::I8x16RelaxedLaneSelect):
          case uint32_t(SimdOp::I16x8RelaxedLaneSelect):
          case uint32_t(SimdOp::I32x4RelaxedLaneSelect):
          case uint32_t(SimdOp::I64x2RelaxedLaneSelect):
          case uint32_t(SimdOp::I32x4RelaxedDotI8x16I7x16AddS): {
            if (!codeMeta.v128RelaxedEnabled()) {
              return iter.unrecognizedOpcode(&op);
            }
            if (!iter.readTernary(ValType::V128, &nothing, &nothing,
                                  &nothing)) {
              return false;
            }
            break;
          }
          case uint32_t(SimdOp::F32x4RelaxedMin):
          case uint32_t(SimdOp::F32x4RelaxedMax):
          case uint32_t(SimdOp::F64x2RelaxedMin):
          case uint32_t(SimdOp::F64x2RelaxedMax):
          case uint32_t(SimdOp::I16x8RelaxedQ15MulrS):
          case uint32_t(SimdOp::I16x8RelaxedDotI8x16I7x16S): {
            if (!codeMeta.v128RelaxedEnabled()) {
              return iter.unrecognizedOpcode(&op);
            }
            if (!iter.readBinary(ValType::V128, &nothing, &nothing)) {
              return false;
            }
            break;
          }
          case uint32_t(SimdOp::I32x4RelaxedTruncF32x4S):
          case uint32_t(SimdOp::I32x4RelaxedTruncF32x4U):
          case uint32_t(SimdOp::I32x4RelaxedTruncF64x2SZero):
          case uint32_t(SimdOp::I32x4RelaxedTruncF64x2UZero): {
            if (!codeMeta.v128RelaxedEnabled()) {
              return iter.unrecognizedOpcode(&op);
            }
            if (!iter.readUnary(ValType::V128, &nothing)) {
              return false;
            }
            break;
          }
          case uint32_t(SimdOp::I8x16RelaxedSwizzle): {
            if (!codeMeta.v128RelaxedEnabled()) {
              return iter.unrecognizedOpcode(&op);
            }
            if (!iter.readBinary(ValType::V128, &nothing, &nothing)) {
              return false;
            }
            break;
          }
#  endif

          default:
            return iter.unrecognizedOpcode(&op);
        }
        break;
      }
#endif  // ENABLE_JIT_SIMD

      case uint16_t(Op::MiscPrefix): {
        switch (op.b1) {
          case uint32_t(MiscOp::I32TruncSatF32S):
          case uint32_t(MiscOp::I32TruncSatF32U): {
            if (!iter.readConversion(ValType::F32, ValType::I32, &nothing)) {
              return false;
            }
            break;
          }
          case uint32_t(MiscOp::I32TruncSatF64S):
          case uint32_t(MiscOp::I32TruncSatF64U): {
            if (!iter.readConversion(ValType::F64, ValType::I32, &nothing)) {
              return false;
            }
            break;
          }
          case uint32_t(MiscOp::I64TruncSatF32S):
          case uint32_t(MiscOp::I64TruncSatF32U): {
            if (!iter.readConversion(ValType::F32, ValType::I64, &nothing)) {
              return false;
            }
            break;
          }
          case uint32_t(MiscOp::I64TruncSatF64S):
          case uint32_t(MiscOp::I64TruncSatF64U): {
            if (!iter.readConversion(ValType::F64, ValType::I64, &nothing)) {
              return false;
            }
            break;
          }
          case uint32_t(MiscOp::MemoryCopy): {
            uint32_t destMemIndex;
            uint32_t srcMemIndex;
            if (!iter.readMemOrTableCopy(/*isMem=*/true, &destMemIndex,
                                         &nothing, &srcMemIndex, &nothing,
                                         &nothing)) {
              return false;
            }
            dumper.dumpMemoryIndex(destMemIndex);
            dumper.dumpMemoryIndex(srcMemIndex);
            break;
          }
          case uint32_t(MiscOp::DataDrop): {
            uint32_t dataIndex;
            if (!iter.readDataOrElemDrop(/*isData=*/true, &dataIndex)) {
              return false;
            }
            dumper.dumpDataIndex(dataIndex);
            break;
          }
          case uint32_t(MiscOp::MemoryFill): {
            uint32_t memoryIndex;
            if (!iter.readMemFill(&memoryIndex, &nothing, &nothing, &nothing)) {
              return false;
            }
            dumper.dumpMemoryIndex(memoryIndex);
            break;
          }
          case uint32_t(MiscOp::MemoryInit): {
            uint32_t dataIndex;
            uint32_t memoryIndex;
            if (!iter.readMemOrTableInit(/*isMem=*/true, &dataIndex,
                                         &memoryIndex, &nothing, &nothing,
                                         &nothing)) {
              return false;
            }
            dumper.dumpMemoryIndex(memoryIndex);
            dumper.dumpDataIndex(dataIndex);
            break;
          }
          case uint32_t(MiscOp::TableCopy): {
            uint32_t destTableIndex;
            uint32_t srcTableIndex;
            if (!iter.readMemOrTableCopy(
                    /*isMem=*/false, &destTableIndex, &nothing, &srcTableIndex,
                    &nothing, &nothing)) {
              return false;
            }
            dumper.dumpTableIndex(destTableIndex);
            dumper.dumpTableIndex(srcTableIndex);
            break;
          }
          case uint32_t(MiscOp::ElemDrop): {
            uint32_t elemIndex;
            if (!iter.readDataOrElemDrop(/*isData=*/false, &elemIndex)) {
              return false;
            }
            dumper.dumpElemIndex(elemIndex);
            break;
          }
          case uint32_t(MiscOp::TableInit): {
            uint32_t elemIndex;
            uint32_t tableIndex;
            if (!iter.readMemOrTableInit(/*isMem=*/false, &elemIndex,
                                         &tableIndex, &nothing, &nothing,
                                         &nothing)) {
              return false;
            }
            dumper.dumpTableIndex(tableIndex);
            dumper.dumpElemIndex(elemIndex);
            break;
          }
          case uint32_t(MiscOp::TableFill): {
            uint32_t tableIndex;
            if (!iter.readTableFill(&tableIndex, &nothing, &nothing,
                                    &nothing)) {
              return false;
            }
            dumper.dumpTableIndex(tableIndex);
            break;
          }
#ifdef ENABLE_WASM_MEMORY_CONTROL
          case uint32_t(MiscOp::MemoryDiscard): {
            if (!codeMeta.memoryControlEnabled()) {
              return iter.unrecognizedOpcode(&op);
            }
            uint32_t memoryIndex;
            if (!iter.readMemDiscard(&memoryIndex, &nothing, &nothing)) {
              return false;
            }
            dumper.dumpMemoryIndex(memoryIndex);
            break;
          }
#endif
          case uint32_t(MiscOp::TableGrow): {
            uint32_t tableIndex;
            if (!iter.readTableGrow(&tableIndex, &nothing, &nothing)) {
              return false;
            }
            dumper.dumpTableIndex(tableIndex);
            break;
          }
          case uint32_t(MiscOp::TableSize): {
            uint32_t tableIndex;
            if (!iter.readTableSize(&tableIndex)) {
              return false;
            }
            dumper.dumpTableIndex(tableIndex);
            break;
          }
          case uint32_t(MiscOp::I64Add128):
          case uint32_t(MiscOp::I64Sub128): {
            if (!codeMeta.wideArithmeticEnabled()) {
              return iter.unrecognizedOpcode(&op);
            }
            if (!iter.readBinaryI128(&nothing, &nothing, &nothing, &nothing)) {
              return false;
            }
            break;
          }
          case uint32_t(MiscOp::I64MulWideS):
          case uint32_t(MiscOp::I64MulWideU): {
            if (!codeMeta.wideArithmeticEnabled()) {
              return iter.unrecognizedOpcode(&op);
            }
            if (!iter.readBinaryI64Wide(&nothing, &nothing)) {
              return false;
            }
            break;
          }
          default:
            return iter.unrecognizedOpcode(&op);
        }
        break;
      }
      case uint16_t(Op::RefAsNonNull): {
        if (!iter.readRefAsNonNull(&nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::BrOnNull): {
        uint32_t depth;
        if (!iter.readBrOnNull(&depth, &resultType, &nothings, &nothing)) {
          return false;
        }
        dumper.dumpBlockDepth(depth);
        break;
      }
      case uint16_t(Op::BrOnNonNull): {
        uint32_t depth;
        if (!iter.readBrOnNonNull(&depth, &resultType, &nothings, &nothing)) {
          return false;
        }
        dumper.dumpBlockDepth(depth);
        break;
      }
      case uint16_t(Op::RefEq): {
        if (!iter.readComparison(RefType::eq(), &nothing, &nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::RefFunc): {
        uint32_t funcIndex;
        if (!iter.readRefFunc(&funcIndex)) {
          return false;
        }
        dumper.dumpFuncIndex(funcIndex);
        break;
      }
      case uint16_t(Op::RefNull): {
        RefType type;
        if (!iter.readRefNull(&type)) {
          return false;
        }
        dumper.dumpHeapType(type);
        break;
      }
      case uint16_t(Op::RefIsNull): {
        Nothing nothing;
        RefType unusedRefType;
        if (!iter.readRefIsNull(&nothing, &unusedRefType)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::Try): {
        if (!iter.readTry(&blockType)) {
          return false;
        }
        dumper.dumpBlockType(blockType);
        break;
      }
      case uint16_t(Op::Catch): {
        LabelKind unusedKind;
        uint32_t tagIndex;
        if (!iter.readCatch(&unusedKind, &tagIndex, &resultType, &resultType,
                            &nothings)) {
          return false;
        }
        dumper.dumpTagIndex(tagIndex);
        break;
      }
      case uint16_t(Op::CatchAll): {
        LabelKind unusedKind;
        if (!iter.readCatchAll(&unusedKind, &resultType, &resultType,
                               &nothings)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::Delegate): {
        uint32_t depth;
        if (!iter.readDelegate(&depth, &resultType, &nothings)) {
          return false;
        }
        iter.popDelegate();
        dumper.dumpBlockDepth(depth);
        break;
      }
      case uint16_t(Op::Throw): {
        uint32_t tagIndex;
        if (!iter.readThrow(&tagIndex, &nothings)) {
          return false;
        }
        dumper.dumpTagIndex(tagIndex);
        break;
      }
      case uint16_t(Op::Rethrow): {
        uint32_t depth;
        if (!iter.readRethrow(&depth)) {
          return false;
        }
        dumper.dumpBlockDepth(depth);
        break;
      }
      case uint16_t(Op::ThrowRef): {
        if (!iter.readThrowRef(&nothing)) {
          return false;
        }
        break;
      }
      case uint16_t(Op::TryTable): {
        TryTableCatchVector catches;
        if (!iter.readTryTable(&blockType, &catches)) {
          return false;
        }
        dumper.dumpTryTableCatches(catches);
        break;
      }
      case uint16_t(Op::ThreadPrefix): {
        // Though thread ops can be used on nonshared memories, we make them
        // unavailable if shared memory has been disabled in the prefs, for
        // maximum predictability and safety and consistency with JS.
        if (codeMeta.sharedMemoryEnabled() == Shareable::False) {
          return iter.unrecognizedOpcode(&op);
        }
        switch (op.b1) {
          case uint32_t(ThreadOp::Notify): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readNotify(&addr, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I32Wait): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readWait(&addr, ValType::I32, 4, &nothing, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64Wait): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readWait(&addr, ValType::I64, 8, &nothing, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::Fence): {
            if (!iter.readFence()) {
              return false;
            }
            break;
          }
          case uint32_t(ThreadOp::I32AtomicLoad): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicLoad(&addr, ValType::I32, 4)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicLoad): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicLoad(&addr, ValType::I64, 8)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I32AtomicLoad8U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicLoad(&addr, ValType::I32, 1)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I32AtomicLoad16U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicLoad(&addr, ValType::I32, 2)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicLoad8U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicLoad(&addr, ValType::I64, 1)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicLoad16U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicLoad(&addr, ValType::I64, 2)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicLoad32U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicLoad(&addr, ValType::I64, 4)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I32AtomicStore): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicStore(&addr, ValType::I32, 4, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicStore): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicStore(&addr, ValType::I64, 8, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I32AtomicStore8U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicStore(&addr, ValType::I32, 1, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I32AtomicStore16U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicStore(&addr, ValType::I32, 2, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicStore8U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicStore(&addr, ValType::I64, 1, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicStore16U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicStore(&addr, ValType::I64, 2, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicStore32U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicStore(&addr, ValType::I64, 4, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I32AtomicAdd):
          case uint32_t(ThreadOp::I32AtomicSub):
          case uint32_t(ThreadOp::I32AtomicAnd):
          case uint32_t(ThreadOp::I32AtomicOr):
          case uint32_t(ThreadOp::I32AtomicXor):
          case uint32_t(ThreadOp::I32AtomicXchg): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicRMW(&addr, ValType::I32, 4, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicAdd):
          case uint32_t(ThreadOp::I64AtomicSub):
          case uint32_t(ThreadOp::I64AtomicAnd):
          case uint32_t(ThreadOp::I64AtomicOr):
          case uint32_t(ThreadOp::I64AtomicXor):
          case uint32_t(ThreadOp::I64AtomicXchg): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicRMW(&addr, ValType::I64, 8, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I32AtomicAdd8U):
          case uint32_t(ThreadOp::I32AtomicSub8U):
          case uint32_t(ThreadOp::I32AtomicAnd8U):
          case uint32_t(ThreadOp::I32AtomicOr8U):
          case uint32_t(ThreadOp::I32AtomicXor8U):
          case uint32_t(ThreadOp::I32AtomicXchg8U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicRMW(&addr, ValType::I32, 1, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I32AtomicAdd16U):
          case uint32_t(ThreadOp::I32AtomicSub16U):
          case uint32_t(ThreadOp::I32AtomicAnd16U):
          case uint32_t(ThreadOp::I32AtomicOr16U):
          case uint32_t(ThreadOp::I32AtomicXor16U):
          case uint32_t(ThreadOp::I32AtomicXchg16U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicRMW(&addr, ValType::I32, 2, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicAdd8U):
          case uint32_t(ThreadOp::I64AtomicSub8U):
          case uint32_t(ThreadOp::I64AtomicAnd8U):
          case uint32_t(ThreadOp::I64AtomicOr8U):
          case uint32_t(ThreadOp::I64AtomicXor8U):
          case uint32_t(ThreadOp::I64AtomicXchg8U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicRMW(&addr, ValType::I64, 1, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicAdd16U):
          case uint32_t(ThreadOp::I64AtomicSub16U):
          case uint32_t(ThreadOp::I64AtomicAnd16U):
          case uint32_t(ThreadOp::I64AtomicOr16U):
          case uint32_t(ThreadOp::I64AtomicXor16U):
          case uint32_t(ThreadOp::I64AtomicXchg16U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicRMW(&addr, ValType::I64, 2, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicAdd32U):
          case uint32_t(ThreadOp::I64AtomicSub32U):
          case uint32_t(ThreadOp::I64AtomicAnd32U):
          case uint32_t(ThreadOp::I64AtomicOr32U):
          case uint32_t(ThreadOp::I64AtomicXor32U):
          case uint32_t(ThreadOp::I64AtomicXchg32U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicRMW(&addr, ValType::I64, 4, &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I32AtomicCmpXchg): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicCmpXchg(&addr, ValType::I32, 4, &nothing,
                                        &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicCmpXchg): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicCmpXchg(&addr, ValType::I64, 8, &nothing,
                                        &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I32AtomicCmpXchg8U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicCmpXchg(&addr, ValType::I32, 1, &nothing,
                                        &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I32AtomicCmpXchg16U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicCmpXchg(&addr, ValType::I32, 2, &nothing,
                                        &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicCmpXchg8U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicCmpXchg(&addr, ValType::I64, 1, &nothing,
                                        &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicCmpXchg16U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicCmpXchg(&addr, ValType::I64, 2, &nothing,
                                        &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          case uint32_t(ThreadOp::I64AtomicCmpXchg32U): {
            LinearMemoryAddress<Nothing> addr;
            if (!iter.readAtomicCmpXchg(&addr, ValType::I64, 4, &nothing,
                                        &nothing)) {
              return false;
            }
            dumper.dumpLinearMemoryAddress(addr);
            break;
          }
          default:
            return iter.unrecognizedOpcode(&op);
        }
        break;
      }
      case uint16_t(Op::MozPrefix):
        return iter.unrecognizedOpcode(&op);
      default:
        return iter.unrecognizedOpcode(&op);
    }

    dumper.dumpOpEnd();
  }

  MOZ_CRASH("unreachable");
}

template bool wasm::ValidateOps<NopOpDumper>(ValidatingOpIter& iter,
                                             NopOpDumper& dumper,
                                             const CodeMetadata& codeMeta);
template bool wasm::ValidateOps<OpDumper>(ValidatingOpIter& iter,
                                          OpDumper& dumper,
                                          const CodeMetadata& codeMeta);

bool wasm::ValidateFunctionBody(const CodeMetadata& codeMeta,
                                uint32_t funcIndex, uint32_t bodySize,
                                Decoder& d) {
  const uint8_t* bodyBegin = d.currentPosition();
  const uint8_t* bodyEnd = bodyBegin + bodySize;

  ValTypeVector locals;
  if (!DecodeLocalEntriesWithParams(d, codeMeta, funcIndex, &locals)) {
    return false;
  }

  ValidatingOpIter iter(codeMeta, d, locals);
  NopOpDumper visitor;

  if (!iter.startFunction(funcIndex)) {
    return false;
  }

  if (!ValidateOps(iter, visitor, codeMeta)) {
    return false;
  }

  return iter.endFunction(bodyEnd);
}

// Section macros.

static bool DecodePreamble(Decoder& d, uint32_t expectedVersion) {
  if (d.bytesRemain() > MaxModuleBytes) {
    return d.fail("module too big");
  }

  uint32_t magic;
  if (!d.readFixedU32(&magic) || magic != MagicNumber) {
    return d.fail("failed to match magic number");
  }

  uint32_t version;
  if (!d.readFixedU32(&version)) {
    return d.fail("failed to read version");
  }
  if (version != expectedVersion) {
    return d.failf("binary version 0x%" PRIx32
                   " does not match expected version 0x%" PRIx32,
                   version, expectedVersion);
  }

  return true;
}

static bool DecodeValTypeVector(Decoder& d, CodeMetadata* codeMeta,
                                uint32_t count, ValTypeVector* valTypes) {
  if (!valTypes->resize(count)) {
    return false;
  }

  for (uint32_t i = 0; i < count; i++) {
    if (!d.readValType(*codeMeta->types, codeMeta->features(),
                       &(*valTypes)[i])) {
      return false;
    }
  }
  return true;
}

static bool DecodeFuncType(Decoder& d, CodeMetadata* codeMeta,
                           FuncType* funcType) {
  uint32_t numArgs;
  if (!d.readVarU32(&numArgs)) {
    return d.fail("bad number of function args");
  }
  if (numArgs > MaxParams) {
    return d.fail("too many arguments in signature");
  }
  ValTypeVector args;
  if (!DecodeValTypeVector(d, codeMeta, numArgs, &args)) {
    return false;
  }

  uint32_t numResults;
  if (!d.readVarU32(&numResults)) {
    return d.fail("bad number of function returns");
  }
  if (numResults > MaxResults) {
    return d.fail("too many returns in signature");
  }
  ValTypeVector results;
  if (!DecodeValTypeVector(d, codeMeta, numResults, &results)) {
    return false;
  }

  *funcType = FuncType(std::move(args), std::move(results));
  return true;
}

static bool DecodeStructType(Decoder& d, CodeMetadata* codeMeta,
                             StructType* structType) {
  uint32_t numFields;
  if (!d.readVarU32(&numFields)) {
    return d.fail("Bad number of fields");
  }

  if (numFields > MaxStructFields) {
    return d.fail("too many fields in struct");
  }

  FieldTypeVector fields;
  if (!fields.resize(numFields)) {
    return false;
  }

  for (uint32_t i = 0; i < numFields; i++) {
    if (!d.readStorageType(*codeMeta->types, codeMeta->features(),
                           &fields[i].type)) {
      return false;
    }

    uint8_t flags;
    if (!d.readFixedU8(&flags)) {
      return d.fail("expected flag");
    }
    if ((flags & ~uint8_t(FieldFlags::AllowedMask)) != 0) {
      return d.fail("garbage flag bits");
    }
    fields[i].isMutable = flags & uint8_t(FieldFlags::Mutable);
  }

  *structType = StructType(std::move(fields));

  // Compute the struct layout, and fail if the struct is too large
  if (!structType->init()) {
    return d.fail("too many fields in struct");
  }
  return true;
}

static bool DecodeArrayType(Decoder& d, CodeMetadata* codeMeta,
                            ArrayType* arrayType) {
  StorageType elementType;
  if (!d.readStorageType(*codeMeta->types, codeMeta->features(),
                         &elementType)) {
    return false;
  }

  uint8_t flags;
  if (!d.readFixedU8(&flags)) {
    return d.fail("expected flag");
  }
  if ((flags & ~uint8_t(FieldFlags::AllowedMask)) != 0) {
    return d.fail("garbage flag bits");
  }
  bool isMutable = flags & uint8_t(FieldFlags::Mutable);

  *arrayType = ArrayType(elementType, isMutable);
  return true;
}

#ifdef ENABLE_WASM_JSPI
static bool DecodeContType(Decoder& d, CodeMetadata* codeMeta,
                           ContType* contType) {
  uint32_t typeIndex;
  if (!d.readTypeIndex(&typeIndex)) {
    return false;
  }

  if (typeIndex >= codeMeta->types->length()) {
    return d.fail("type index out of range");
  }

  // We don't validate that a continuation points at a function type until we've
  // decoded the whole recursion group.
  const TypeDef& typeDef = codeMeta->types->type(typeIndex);
  *contType = ContType(&typeDef);
  return true;
}
#endif  // ENABLE_WASM_JSPI

static bool DecodeTypeSection(Decoder& d, CodeMetadata* codeMeta) {
  MaybeBytecodeRange range;
  if (!d.startSection(SectionId::Type, codeMeta, &range, "type")) {
    return false;
  }
  if (!range) {
    return true;
  }

  uint32_t numRecGroups;
  if (!d.readVarU32(&numRecGroups)) {
    return d.fail("expected number of types");
  }

  // Check if we've reached our implementation defined limit of recursion
  // groups.
  if (numRecGroups > MaxRecGroups) {
    return d.fail("too many types");
  }

  for (uint32_t recGroupIndex = 0; recGroupIndex < numRecGroups;
       recGroupIndex++) {
    uint32_t recGroupLength = 1;

    uint8_t firstTypeCode;
    if (!d.peekByte(&firstTypeCode)) {
      return d.fail("expected type form");
    }

    if (firstTypeCode == (uint8_t)TypeCode::RecGroup) {
      // Skip over the prefix byte that was peeked.
      d.uncheckedReadFixedU8();

      // Read the number of types in this recursion group
      if (!d.readVarU32(&recGroupLength)) {
        return d.fail("expected recursion group length");
      }
    }

    // Check if we've reached our implementation defined limit of type
    // definitions.
    mozilla::CheckedUint32 newNumTypes(codeMeta->types->length());
    newNumTypes += recGroupLength;
    if (!newNumTypes.isValid() || newNumTypes.value() > MaxTypes) {
      return d.fail("too many types");
    }

    // Start a recursion group. This will extend the type context with empty
    // type definitions to be filled.
    MutableRecGroup recGroup = codeMeta->types->startRecGroup(recGroupLength);
    if (!recGroup) {
      return false;
    }

    // Cancel the pending rec group if we return early due to a validation
    // error, so that the bad group is not left in recGroups_ for ~TypeContext
    // to hash during cleanup.
    auto cancelRecGroup =
        mozilla::MakeScopeExit([&] { codeMeta->types->cancelStartRecGroup(); });

    // First, iterate over the types, validate them and set super types.
    // Subtyping relationship will be checked in a second iteration.
    for (uint32_t recGroupTypeIndex = 0; recGroupTypeIndex < recGroupLength;
         recGroupTypeIndex++) {
      uint32_t typeIndex =
          codeMeta->types->length() - recGroupLength + recGroupTypeIndex;

      // This is ensured by above
      MOZ_ASSERT(typeIndex < MaxTypes);

      uint8_t form;
      const TypeDef* superTypeDef = nullptr;

      // By default, all types are final unless the sub keyword is specified.
      bool finalTypeFlag = true;

      // Decode an optional declared super type index.
      if (d.peekByte(&form) && (form == (uint8_t)TypeCode::SubNoFinalType ||
                                form == (uint8_t)TypeCode::SubFinalType)) {
        if (form == (uint8_t)TypeCode::SubNoFinalType) {
          finalTypeFlag = false;
        }

        // Skip over the `sub` or `final` prefix byte we peeked.
        d.uncheckedReadFixedU8();

        // Decode the number of super types, which is currently limited to at
        // most one.
        uint32_t numSuperTypes;
        if (!d.readVarU32(&numSuperTypes)) {
          return d.fail("expected number of super types");
        }
        if (numSuperTypes > 1) {
          return d.fail("too many super types");
        }

        // Decode the super type, if any.
        if (numSuperTypes == 1) {
          uint32_t superTypeDefIndex;
          if (!d.readVarU32(&superTypeDefIndex)) {
            return d.fail("expected super type index");
          }

          // A super type index must be strictly less than the current type
          // index in order to avoid cycles.
          if (superTypeDefIndex >= typeIndex) {
            return d.fail("invalid super type index");
          }

          superTypeDef = &codeMeta->types->type(superTypeDefIndex);
        }
      }

      // Decode the kind of type definition
      if (!d.readFixedU8(&form)) {
        return d.fail("expected type form");
      }

      TypeDef* typeDef = &recGroup->type(recGroupTypeIndex);
      switch (form) {
        case uint8_t(TypeCode::Func): {
          FuncType funcType;
          if (!DecodeFuncType(d, codeMeta, &funcType)) {
            return false;
          }
          *typeDef = std::move(funcType);
          break;
        }
        case uint8_t(TypeCode::Struct): {
          StructType structType;
          if (!DecodeStructType(d, codeMeta, &structType)) {
            return false;
          }
          *typeDef = std::move(structType);
          break;
        }
        case uint8_t(TypeCode::Array): {
          ArrayType arrayType;
          if (!DecodeArrayType(d, codeMeta, &arrayType)) {
            return false;
          }
          *typeDef = std::move(arrayType);
          break;
        }
#ifdef ENABLE_WASM_JSPI
        case uint8_t(TypeCode::Cont): {
          if (!codeMeta->stackSwitchingEnabled()) {
            return d.fail("stack switching is not enabled");
          }
          ContType contType;
          if (!DecodeContType(d, codeMeta, &contType)) {
            return false;
          }
          *typeDef = std::move(contType);
          break;
        }
#endif  // ENABLE_WASM_JSPI
        default:
          return d.fail("expected type form");
      }

      typeDef->setFinal(finalTypeFlag);
      if (superTypeDef) {
        // Check that we aren't creating too deep of a subtyping chain
        if (superTypeDef->subTypingDepth() >= MaxSubTypingDepth) {
          return d.fail("type is too deep");
        }

        typeDef->setSuperTypeDef(superTypeDef);
      }

      if (typeDef->isFuncType()) {
        typeDef->funcType().initImmediateTypeId(typeDef->isFinal(),
                                                superTypeDef, recGroupLength);
      }
    }

#ifdef ENABLE_WASM_JSPI
    // Continuation types must refer to function types. We can only validate
    // this after we've decoded all the types in the recursion group though.
    for (uint32_t recGroupTypeIndex = 0; recGroupTypeIndex < recGroupLength;
         recGroupTypeIndex++) {
      TypeDef* typeDef = &recGroup->type(recGroupTypeIndex);
      if (!typeDef->isContType()) {
        continue;
      }

      if (!typeDef->contType().funcTypeDef().isFuncType()) {
        return d.fail("cont must reference a func type");
      }
    }
#endif  // ENABLE_WASM_JSPI

    // Check the super types to make sure they are compatible with their
    // subtypes. This is done in a second iteration to avoid dealing with not
    // yet loaded types.
    for (uint32_t recGroupTypeIndex = 0; recGroupTypeIndex < recGroupLength;
         recGroupTypeIndex++) {
      TypeDef* typeDef = &recGroup->type(recGroupTypeIndex);
      if (typeDef->superTypeDef()) {
        // Check that the super type is compatible with this type
        if (!TypeDef::canBeSubTypeOf(typeDef, typeDef->superTypeDef())) {
          return d.fail("incompatible super type");
        }
      }
    }

    // Finish the recursion group, which will canonicalize the types.
    cancelRecGroup.release();
    if (!codeMeta->types->endRecGroup()) {
      return false;
    }
  }

  return d.finishSection(*range, "type");
}

[[nodiscard]] static bool DecodeName(Decoder& d, CacheableName* name) {
  uint32_t numBytes;
  if (!d.readVarU32(&numBytes)) {
    return false;
  }

  UTF8Bytes utf8Bytes;
  if (!d.readUTF8Bytes(numBytes, &utf8Bytes)) {
    return false;
  }
  *name = CacheableName(std::move(utf8Bytes));
  return true;
}

static bool DecodeFuncTypeIndex(Decoder& d, const SharedTypeContext& types,
                                uint32_t* funcTypeIndex) {
  if (!d.readVarU32(funcTypeIndex)) {
    return d.fail("expected signature index");
  }

  if (*funcTypeIndex >= types->length()) {
    return d.fail("signature index out of range");
  }

  const TypeDef& def = (*types)[*funcTypeIndex];

  if (!def.isFuncType()) {
    return d.fail("signature index references non-signature");
  }

  return true;
}

static bool DecodeLimitBound(Decoder& d, AddressType addressType,
                             uint64_t* bound) {
  if (addressType == AddressType::I64) {
    return d.readVarU64(bound);
  }

  // Spec tests assert that we only decode a LEB32 when address type is I32.
  uint32_t bound32;
  if (!d.readVarU32(&bound32)) {
    return false;
  }
  *bound = bound32;
  return true;
}

static bool DecodeLimits(Decoder& d, const CodeMetadata* codeMeta,
                         LimitsKind kind, Limits* limits) {
  uint8_t flags;
  if (!d.readFixedU8(&flags)) {
    return d.fail("expected flags");
  }

  uint8_t mask = kind == LimitsKind::Memory ? uint8_t(LimitsMask::Memory)
                                            : uint8_t(LimitsMask::Table);

  if (flags & ~uint8_t(mask)) {
    return d.failf("unexpected bits set in flags: %" PRIu32,
                   uint32_t(flags & ~uint8_t(mask)));
  }

  // Memory limits may be shared
  if (kind == LimitsKind::Memory) {
    if ((flags & uint8_t(LimitsFlags::IsShared)) &&
        !(flags & uint8_t(LimitsFlags::HasMaximum))) {
      return d.fail("maximum length required for shared memory");
    }

    limits->shared = (flags & uint8_t(LimitsFlags::IsShared))
                         ? Shareable::True
                         : Shareable::False;
  } else {
    limits->shared = Shareable::False;
  }

  limits->addressType = (flags & uint8_t(LimitsFlags::IsI64))
                            ? AddressType::I64
                            : AddressType::I32;

  uint64_t initial;
  if (!DecodeLimitBound(d, limits->addressType, &initial)) {
    return d.fail("expected initial length");
  }
  limits->initial = initial;

  if (flags & uint8_t(LimitsFlags::HasMaximum)) {
    uint64_t maximum;
    if (!DecodeLimitBound(d, limits->addressType, &maximum)) {
      return d.fail("expected maximum length");
    }

    if (limits->initial > maximum) {
      return d.failf(
          "%s size minimum must not be greater than maximum; "
          "maximum length %" PRIu64 " is less than initial length %" PRIu64,
          kind == LimitsKind::Memory ? "memory" : "table", maximum,
          limits->initial);
    }

    limits->maximum.emplace(maximum);
  }

  if (kind == LimitsKind::Memory) {
    limits->pageSize = PageSize::Standard;
#ifdef ENABLE_WASM_CUSTOM_PAGE_SIZES
    if (flags & uint8_t(LimitsFlags::HasCustomPageSize)) {
      if (!codeMeta->customPageSizesEnabled()) {
        return d.fail("custom page sizes are disabled");
      }

      uint32_t customPageSize;
      if (!d.readVarU32(&customPageSize)) {
        return d.fail("failed to decode custom page size");
      }

      if (customPageSize == static_cast<uint32_t>(PageSize::Tiny)) {
        limits->pageSize = PageSize::Tiny;
      } else if (customPageSize != static_cast<uint32_t>(PageSize::Standard)) {
        return d.fail("bad custom page size");
      }
    }
#endif
  }

  return true;
}

// Combined decoding for both table types and the augmented form of table types
// that can include init expressions:
//
// https://webassembly.github.io/spec/core/binary/types.html#table-types
// https://webassembly.github.io/spec/core/binary/modules.html#table-section
//
// Only defined tables are therefore allowed to have init expressions, not
// imported tables.
static bool DecodeTableType(Decoder& d, const CodeMetadata* codeMeta,
                            bool isImport, TableType* tableType,
                            bool* initExprPresent) {
  *initExprPresent = false;
  uint8_t typeCode;
  if (!d.peekByte(&typeCode)) {
    return d.fail("expected type code");
  }
  if (typeCode == (uint8_t)TypeCode::TableHasInitExpr) {
    if (isImport) {
      return d.fail("imported tables cannot have initializer expressions");
    }
    d.uncheckedReadFixedU8();
    uint8_t flags;
    if (!d.readFixedU8(&flags) || flags != 0) {
      return d.fail("expected reserved byte to be 0");
    }
    *initExprPresent = true;
  }

  if (!d.readRefType(*codeMeta->types, codeMeta->features(),
                     &tableType->elemType)) {
    return false;
  }
  if (!DecodeLimits(d, codeMeta, LimitsKind::Table, &tableType->limits)) {
    return false;
  }

  // If there's a maximum, check it is in range.  The check to exclude
  // initial > maximum is carried out by the DecodeLimits call above, so
  // we don't repeat it here.
  if (tableType->limits.initial >
          MaxTableElemsValidation(tableType->limits.addressType) ||
      ((tableType->limits.maximum.isSome() &&
        tableType->limits.maximum.value() >
            MaxTableElemsValidation(tableType->limits.addressType)))) {
    return d.fail("too many table elements");
  }

  if (!tableType->elemType.isNullable() && !isImport && !*initExprPresent) {
    return d.fail("table with non-nullable references requires initializer");
  }

  return true;
}

static bool DecodeGlobalType(Decoder& d, const CodeMetadata* codeMeta,
                             GlobalType* globalType) {
  if (!d.readValType(*codeMeta->types, codeMeta->features(),
                     &globalType->type)) {
    return d.fail("expected global type");
  }

  uint8_t flags;
  if (!d.readFixedU8(&flags)) {
    return d.fail("expected global flags");
  }

  if (flags & ~uint8_t(GlobalTypeImmediate::AllowedMask)) {
    return d.fail("unexpected bits set in global flags");
  }

  globalType->isMutable = flags & uint8_t(GlobalTypeImmediate::IsMutable);
  return true;
}

static bool DecodeMemoryType(Decoder& d, const CodeMetadata* codeMeta,
                             Limits* limits) {
  if (!DecodeLimits(d, codeMeta, LimitsKind::Memory, limits)) {
    return false;
  }

  uint64_t maxField =
      MaxMemoryPagesValidation(limits->addressType, limits->pageSize);

  if (limits->initial > maxField) {
    return d.fail("initial memory size too big");
  }

  if (limits->maximum && *limits->maximum > maxField) {
    return d.fail("maximum memory size too big");
  }

  if (limits->shared == Shareable::True &&
      codeMeta->sharedMemoryEnabled() == Shareable::False) {
    return d.fail("shared memory is disabled");
  }

  return true;
}

static bool DecodeTagType(Decoder& d, const CodeMetadata* codeMeta,
                          uint32_t* funcTypeIndex) {
  uint32_t tagCode;
  if (!d.readVarU32(&tagCode)) {
    return d.fail("expected tag kind");
  }
  if (TagKind(tagCode) != TagKind::Exception) {
    return d.fail("illegal tag kind");
  }

  if (!d.readVarU32(funcTypeIndex)) {
    return d.fail("expected function index in tag");
  }
  if (*funcTypeIndex >= codeMeta->numTypes()) {
    return d.fail("function type index in tag out of bounds");
  }
  if (!(*codeMeta->types)[*funcTypeIndex].isFuncType()) {
    return d.fail("function type index must index a function type");
  }
  // Stack switching relaxes the restriction that tags cannot have results.
  if (!codeMeta->stackSwitchingEnabled() &&
      (*codeMeta->types)[*funcTypeIndex].funcType().results().length() != 0) {
    return d.fail("tag function types must not return anything");
  }

  return true;
}

struct ExternType {
 private:
  DefinitionKind kind_ = DefinitionKind(-1);
  union {
    uint32_t funcTypeIndex;
    TableType tableType;
    Limits memType;
    GlobalType globalType;
    uint32_t tagFuncTypeIndex;
  };

 public:
  ExternType() : funcTypeIndex() {}

  static ExternType func(uint32_t funcTypeIndex) {
    ExternType result;
    result.kind_ = DefinitionKind::Function;
    result.funcTypeIndex = funcTypeIndex;
    return result;
  }

  static ExternType table(TableType& tableType) {
    ExternType result;
    result.kind_ = DefinitionKind::Table;
    result.tableType = tableType;
    return result;
  }

  static ExternType memory(Limits& memType) {
    ExternType result;
    result.kind_ = DefinitionKind::Memory;
    result.memType = memType;
    return result;
  }

  static ExternType global(GlobalType& globalType) {
    ExternType result;
    result.kind_ = DefinitionKind::Global;
    result.globalType = globalType;
    return result;
  }

  static ExternType tag(uint32_t tagFuncTypeIndex) {
    ExternType result;
    result.kind_ = DefinitionKind::Tag;
    result.tagFuncTypeIndex = tagFuncTypeIndex;
    return result;
  }

  DefinitionKind kind() const { return kind_; }

  uint32_t asFunc() const {
    MOZ_RELEASE_ASSERT(kind_ == DefinitionKind::Function);
    return funcTypeIndex;
  }

  const TableType& asTable() const {
    MOZ_RELEASE_ASSERT(kind_ == DefinitionKind::Table);
    return tableType;
  }

  const Limits& asMemory() const {
    MOZ_RELEASE_ASSERT(kind_ == DefinitionKind::Memory);
    return memType;
  }

  const GlobalType& asGlobal() const {
    MOZ_RELEASE_ASSERT(kind_ == DefinitionKind::Global);
    return globalType;
  }

  uint32_t asTag() const {
    MOZ_RELEASE_ASSERT(kind_ == DefinitionKind::Tag);
    return tagFuncTypeIndex;
  }
};

[[nodiscard]]
static bool DecodeImportType(Decoder& d, DefinitionKind importKind,
                             const CodeMetadata* codeMeta,
                             const ModuleMetadata* moduleMeta,
                             ExternType* importType) {
  switch (importKind) {
    case DefinitionKind::Function: {
      uint32_t funcTypeIndex;
      if (!DecodeFuncTypeIndex(d, codeMeta->types, &funcTypeIndex)) {
        return false;
      }
      *importType = ExternType::func(funcTypeIndex);
      break;
    }
    case DefinitionKind::Table: {
      TableType tableType;
      bool hasInitExpr;
      if (!DecodeTableType(d, codeMeta, /*isImport=*/true, &tableType,
                           &hasInitExpr)) {
        return false;
      }
      MOZ_ASSERT(!hasInitExpr,
                 "we should have failed because imported tables cannot have "
                 "import expressions");
      *importType = ExternType::table(tableType);
      break;
    }
    case DefinitionKind::Memory: {
      Limits memType;
      if (!DecodeMemoryType(d, codeMeta, &memType)) {
        return false;
      }
      *importType = ExternType::memory(memType);
      break;
    }
    case DefinitionKind::Global: {
      GlobalType globalType;
      if (!DecodeGlobalType(d, codeMeta, &globalType)) {
        return false;
      }
      *importType = ExternType::global(globalType);
      break;
    }
    case DefinitionKind::Tag: {
      uint32_t tagFuncTypeIndex;
      if (!DecodeTagType(d, codeMeta, &tagFuncTypeIndex)) {
        return false;
      }
      *importType = ExternType::tag(tagFuncTypeIndex);
      break;
    }
    default:
      return d.fail("unsupported import kind");
  }

  return true;
}

[[nodiscard]]
static bool AddImport(Decoder& d, CacheableName& moduleName,
                      CacheableName& itemName, ExternType importType,
                      CodeMetadata* codeMeta, ModuleMetadata* moduleMeta) {
  uint32_t importIndex = moduleMeta->imports.length();
  if (!moduleMeta->imports.emplaceBack(
          std::move(moduleName), std::move(itemName), importType.kind())) {
    return false;
  }

  switch (importType.kind()) {
    case DefinitionKind::Function: {
      if (codeMeta->funcs.length() >= MaxFuncs) {
        return d.fail("too many functions");
      }
      if (!codeMeta->funcs.emplaceBack(importType.asFunc())) {
        return false;
      }
      break;
    }
    case DefinitionKind::Table: {
      if (codeMeta->numTables() >= MaxTables) {
        return d.fail("too many tables");
      }
      if (!codeMeta->tables.emplaceBack(importType.asTable(),
                                        mozilla::Nothing(),
                                        /*isImported=*/true)) {
        return false;
      }
      break;
    }
    case DefinitionKind::Memory: {
      if (codeMeta->numMemories() >= MaxMemories) {
        return d.fail("too many memories");
      }
      if (!codeMeta->memories.emplaceBack(MemoryDesc(importType.asMemory()))) {
        return false;
      }
      codeMeta->memories.back().importIndex = Some(importIndex);
      break;
    }
    case DefinitionKind::Global: {
      if (codeMeta->globals.length() >= MaxGlobals) {
        return d.fail("too many globals");
      }
      if (!codeMeta->globals.emplaceBack(importType.asGlobal(),
                                         codeMeta->globals.length())) {
        return false;
      }
      break;
    }
    case DefinitionKind::Tag: {
      MutableTagType tagType = js_new<TagType>();
      if (!tagType ||
          !tagType->initialize(&(*codeMeta->types)[importType.asTag()])) {
        return false;
      }
      if (codeMeta->tags.length() >= MaxTags) {
        return d.fail("too many tags");
      }
      if (!codeMeta->tags.emplaceBack(TagKind::Exception, tagType)) {
        return false;
      }
      break;
    }
    default:
      return d.fail("unsupported import kind");
  }

  return true;
}

static bool DecodeImportGroup(Decoder& d, CodeMetadata* codeMeta,
                              ModuleMetadata* moduleMeta) {
  CacheableName moduleName;
  if (!DecodeName(d, &moduleName)) {
    return d.fail("expected valid import module name");
  }
  CacheableName itemName;
  if (!DecodeName(d, &itemName)) {
    return d.fail("expected valid import name");
  }
  uint8_t rawImportKind;
  if (!d.readFixedU8(&rawImportKind)) {
    return d.fail("failed to read import kind");
  }

#ifdef ENABLE_WASM_COMPACT_IMPORTS
  // Compact encoding 1: one module name, many (item name, externtype) pairs
  if (codeMeta->compactImportsEnabled() && itemName.isEmpty() &&
      rawImportKind == uint8_t(CompactImportKind::ModuleName)) {
    uint32_t numImports;
    if (!d.readVarU32(&numImports)) {
      return d.fail("failed to read number of compact imports");
    }

    mozilla::CheckedUint32 numImportsSoFar(moduleMeta->imports.length());
    numImportsSoFar += numImports;
    if (!numImportsSoFar.isValid() || numImportsSoFar.value() > MaxImports) {
      return d.fail("too many imports");
    }

    for (uint32_t i = 0; i < numImports; i++) {
      CacheableName clonedModuleName;
      if (!moduleName.clone(&clonedModuleName)) {
        return false;
      }

      CacheableName compactItemName;
      if (!DecodeName(d, &compactItemName)) {
        return d.fail("expected valid import name");
      }

      uint8_t importKind;
      if (!d.readFixedU8(&importKind)) {
        return d.fail("failed to read import kind");
      }
      ExternType importType;
      if (!DecodeImportType(d, DefinitionKind(importKind), codeMeta, moduleMeta,
                            &importType)) {
        return false;
      }

      if (!AddImport(d, clonedModuleName, compactItemName, importType, codeMeta,
                     moduleMeta)) {
        return false;
      }
    }
    return true;
  }

  // Compact encoding 2: one module name and externtype, many item names
  if (codeMeta->compactImportsEnabled() && itemName.isEmpty() &&
      rawImportKind == uint8_t(CompactImportKind::ModuleNameAndExternType)) {
    uint8_t importKind;
    if (!d.readFixedU8(&importKind)) {
      return d.fail("failed to read import kind");
    }

    ExternType importType;
    if (!DecodeImportType(d, DefinitionKind(importKind), codeMeta, moduleMeta,
                          &importType)) {
      return false;
    }

    uint32_t numImports;
    if (!d.readVarU32(&numImports)) {
      return d.fail("failed to read number of compact imports");
    }

    mozilla::CheckedUint32 numImportsSoFar(moduleMeta->imports.length());
    numImportsSoFar += numImports;
    if (!numImportsSoFar.isValid() || numImportsSoFar.value() > MaxImports) {
      return d.fail("too many imports");
    }

    for (uint32_t i = 0; i < numImports; i++) {
      CacheableName clonedModuleName;
      if (!moduleName.clone(&clonedModuleName)) {
        return false;
      }

      CacheableName compactItemName;
      if (!DecodeName(d, &compactItemName)) {
        return d.fail("expected valid import name");
      }

      if (!AddImport(d, clonedModuleName, compactItemName, importType, codeMeta,
                     moduleMeta)) {
        return false;
      }
    }
    return true;
  }
#endif

  // Single-item encoding
  mozilla::CheckedUint32 numImportsSoFar(moduleMeta->imports.length());
  numImportsSoFar += 1;
  if (!numImportsSoFar.isValid() || numImportsSoFar.value() > MaxImports) {
    return d.fail("too many imports");
  }
  ExternType importType;
  if (!DecodeImportType(d, DefinitionKind(rawImportKind), codeMeta, moduleMeta,
                        &importType)) {
    return false;
  }
  return AddImport(d, moduleName, itemName, importType, codeMeta, moduleMeta);
}

static bool CheckImportsAgainstBuiltinModules(Decoder& d,
                                              CodeMetadata* codeMeta,
                                              ModuleMetadata* moduleMeta) {
  const BuiltinModuleIds& builtinModules = codeMeta->features().builtinModules;

  // Skip this pass if there are no builtin modules enabled
  if (builtinModules.hasNone()) {
    return true;
  }

  uint32_t importFuncIndex = 0;
  uint32_t importGlobalIndex = 0;
  for (auto& import : moduleMeta->imports) {
    Maybe<BuiltinModuleId> builtinModule =
        ImportMatchesBuiltinModule(import.module.utf8Bytes(), builtinModules);

    switch (import.kind) {
      case DefinitionKind::Function: {
        const FuncDesc& func = codeMeta->funcs[importFuncIndex];
        uint32_t funcIndex = importFuncIndex;
        importFuncIndex += 1;
        MOZ_ASSERT(codeMeta->knownFuncImports[funcIndex] ==
                   BuiltinModuleFuncId::None);

        // Skip this import if it doesn't refer to a builtin module. We do have
        // to increment the import function index regardless though.
        if (!builtinModule) {
          continue;
        }

        // Every import from the imported string module must match
        // `global ref extern`, so a function import is always a type mismatch.
        // https://webassembly.github.io/spec/js-api/#validate-builtins-and-imported-string-for-a-webassembly-module
        if (*builtinModule == BuiltinModuleId::JSStringConstants) {
          return d.fail("type mismatch");
        }

        // Check if this import refers to a builtin module function
        const BuiltinModuleFunc* builtinFunc = nullptr;
        BuiltinModuleFuncId builtinFuncId;
        if (!ImportFieldMatchesBuiltinModuleDefinition(
                import.field.utf8Bytes(), *builtinModule,
                DefinitionKind::Function, &builtinFunc, &builtinFuncId)) {
          // Polyfillability: if the field is not found in the builtin module,
          // it will be resolved from the imports object at instantiation.
          continue;
        }

        const TypeDef& importTypeDef = (*codeMeta->types)[func.typeIndex];
        if (!TypeDef::isSubTypeOf(builtinFunc->typeDef(), &importTypeDef)) {
          return d.failf("type mismatch in %s", builtinFunc->exportName());
        }

        codeMeta->knownFuncImports[funcIndex] = builtinFuncId;
        break;
      }
      case DefinitionKind::Global: {
        const GlobalDesc& global = codeMeta->globals[importGlobalIndex];
        importGlobalIndex += 1;

        // Skip this import if it doesn't refer to a builtin module. We do have
        // to increment the import global index regardless though.
        if (!builtinModule) {
          continue;
        }

        // Only the imported string constants module has globals defined.
        if (*builtinModule != BuiltinModuleId::JSStringConstants) {
          return d.fail("unrecognized builtin module field");
        }

        // All imported globals must match a provided global type of
        // `(global (ref extern))`.
        if (global.isMutable() ||
            !ValType::isSubTypeOf(ValType(RefType::extern_().asNonNullable()),
                                  global.type())) {
          return d.failf("type mismatch");
        }

        break;
      }
      default: {
        if (!builtinModule) {
          continue;
        }
        return d.fail("unrecognized builtin import");
      }
    }
  }

  return true;
}

// A standalone validation pass that occurs after we have finished decoding all
// memories and therefore can determine if any imported builtin functions are
// invalid due to lack of memory.
static bool CheckBuiltinImportsHaveMemory(Decoder& d, CodeMetadata* codeMeta) {
  // Skip this pass if there are no builtin modules enabled.
  if (codeMeta->features().builtinModules.hasNone()) {
#ifdef DEBUG
    for (BuiltinModuleFuncId& id : codeMeta->knownFuncImports) {
      MOZ_ASSERT(id == BuiltinModuleFuncId::None);
    }
#endif
    return true;
  }

  for (size_t i = 0; i < codeMeta->knownFuncImports.length(); i++) {
    BuiltinModuleFuncId builtinFuncId = codeMeta->knownFuncImports[i];
    if (builtinFuncId == BuiltinModuleFuncId::None) {
      continue;
    }

    const BuiltinModuleFunc& builtinModuleFunc =
        BuiltinModuleFuncs::getFromId(builtinFuncId);
    if (builtinModuleFunc.usesMemory()) {
      if (codeMeta->memories.length() == 0) {
        return d.failf("func %zu is a builtin function that requires a memory",
                       i);
      }

      // NOTE(bvisness): As of today, no builtins use shared memory. If this
      // changes in the future, you will need to update this to pick up the
      // expected shared-ness from the associated builtin module. Unfortunately
      // this is currently defined exclusively by which BuiltinMemory we
      // construct in CompileBuiltinModule in WasmBuiltinModule.cpp, and there
      // is no straightforward way to map from a BuiltinModuleFuncId to a
      // BuiltinModuleId, much less to know what kind of memory it expects. It
      // would be nice to have some kind of BuiltinModuleDesc that holistically
      // describes what kind of module to construct and what functions it
      // contains, but we are not building this today :)
      if (codeMeta->memories[0].isShared()) {
        return d.fail("builtin funcs are not compatible with shared memories");
      }
    }
  }

  return true;
}

static bool DecodeImportSection(Decoder& d, CodeMetadata* codeMeta,
                                ModuleMetadata* moduleMeta) {
  MaybeBytecodeRange range;
  if (!d.startSection(SectionId::Import, codeMeta, &range, "import")) {
    return false;
  }
  if (!range) {
    return true;
  }

  uint32_t numImportGroups;
  if (!d.readVarU32(&numImportGroups)) {
    return d.fail("failed to read number of imports");
  }
  for (uint32_t i = 0; i < numImportGroups; i++) {
    if (!DecodeImportGroup(d, codeMeta, moduleMeta)) {
      return false;
    }
  }

  if (!d.finishSection(*range, "import")) {
    return false;
  }

  codeMeta->numFuncImports = codeMeta->funcs.length();
  if (!codeMeta->knownFuncImports.resize(codeMeta->numFuncImports)) {
    return false;
  }
  codeMeta->numGlobalImports = codeMeta->globals.length();
  return true;
}

static bool DecodeFunctionSection(Decoder& d, CodeMetadata* codeMeta) {
  MaybeBytecodeRange range;
  if (!d.startSection(SectionId::Function, codeMeta, &range, "function")) {
    return false;
  }
  if (!range) {
    return true;
  }

  uint32_t numDefs;
  if (!d.readVarU32(&numDefs)) {
    return d.fail("expected number of function definitions");
  }

  CheckedInt<uint32_t> numFuncs = codeMeta->funcs.length();
  numFuncs += numDefs;
  if (!numFuncs.isValid() || numFuncs.value() > MaxFuncs) {
    return d.fail("too many functions");
  }

  if (!codeMeta->funcs.reserve(numFuncs.value())) {
    return false;
  }

  for (uint32_t i = 0; i < numDefs; i++) {
    uint32_t funcTypeIndex;
    if (!DecodeFuncTypeIndex(d, codeMeta->types, &funcTypeIndex)) {
      return false;
    }
    codeMeta->funcs.infallibleAppend(funcTypeIndex);
  }

  return d.finishSection(*range, "function");
}

static bool DecodeTableSection(Decoder& d, CodeMetadata* codeMeta) {
  MaybeBytecodeRange range;
  if (!d.startSection(SectionId::Table, codeMeta, &range, "table")) {
    return false;
  }
  if (!range) {
    return true;
  }

  uint32_t numDefs;
  if (!d.readVarU32(&numDefs)) {
    return d.fail("failed to read number of tables");
  }

  CheckedInt<uint32_t> numTables = codeMeta->tables.length();
  numTables += numDefs;
  if (!numTables.isValid() || numTables.value() > MaxTables) {
    return d.fail("too many tables");
  }

  if (!codeMeta->tables.reserve(numTables.value())) {
    return false;
  }

  for (uint32_t i = 0; i < numDefs; ++i) {
    TableType tableType;
    bool initExprPresent;
    if (!DecodeTableType(d, codeMeta, /*isImport=*/false, &tableType,
                         &initExprPresent)) {
      return false;
    }
    mozilla::Maybe<InitExpr> initExpr;
    if (initExprPresent) {
      InitExpr initializer;
      if (!InitExpr::decodeAndValidate(d, codeMeta, tableType.elemType,
                                       &initializer)) {
        return false;
      }
      initExpr = mozilla::Some(std::move(initializer));
    }

    codeMeta->tables.infallibleEmplaceBack(tableType, std::move(initExpr),
                                           /*isImported=*/false);
  }

  return d.finishSection(*range, "table");
}

static bool DecodeMemorySection(Decoder& d, CodeMetadata* codeMeta) {
  MaybeBytecodeRange range;
  if (!d.startSection(SectionId::Memory, codeMeta, &range, "memory")) {
    return false;
  }
  if (!range) {
    return true;
  }

  uint32_t numDefs;
  if (!d.readVarU32(&numDefs)) {
    return d.fail("failed to read number of memories");
  }

  CheckedInt<uint32_t> numMemories = codeMeta->memories.length();
  numMemories += numDefs;
  if (!numMemories.isValid() || numMemories.value() > MaxMemories) {
    return d.fail("too many memories");
  }

  if (!codeMeta->memories.reserve(numMemories.value())) {
    return false;
  }

  for (uint32_t i = 0; i < numDefs; ++i) {
    Limits limits;
    if (!DecodeMemoryType(d, codeMeta, &limits)) {
      return false;
    }
    codeMeta->memories.infallibleEmplaceBack(limits);
  }

  return d.finishSection(*range, "memory");
}

static bool DecodeGlobalSection(Decoder& d, CodeMetadata* codeMeta) {
  MaybeBytecodeRange range;
  if (!d.startSection(SectionId::Global, codeMeta, &range, "global")) {
    return false;
  }
  if (!range) {
    return true;
  }

  uint32_t numDefs;
  if (!d.readVarU32(&numDefs)) {
    return d.fail("expected number of globals");
  }

  CheckedInt<uint32_t> numGlobals = codeMeta->globals.length();
  numGlobals += numDefs;
  if (!numGlobals.isValid() || numGlobals.value() > MaxGlobals) {
    return d.fail("too many globals");
  }

  if (!codeMeta->globals.reserve(numGlobals.value())) {
    return false;
  }

  for (uint32_t i = 0; i < numDefs; i++) {
    GlobalType type;
    if (!DecodeGlobalType(d, codeMeta, &type)) {
      return false;
    }

    InitExpr initializer;
    if (!InitExpr::decodeAndValidate(d, codeMeta, type.type, &initializer)) {
      return false;
    }

    codeMeta->globals.infallibleEmplaceBack(std::move(initializer),
                                            type.isMutable);
  }

  return d.finishSection(*range, "global");
}

static bool DecodeTagSection(Decoder& d, CodeMetadata* codeMeta) {
  MaybeBytecodeRange range;
  if (!d.startSection(SectionId::Tag, codeMeta, &range, "tag")) {
    return false;
  }
  if (!range) {
    return true;
  }

  uint32_t numDefs;
  if (!d.readVarU32(&numDefs)) {
    return d.fail("expected number of tags");
  }

  CheckedInt<uint32_t> numTags = codeMeta->tags.length();
  numTags += numDefs;
  if (!numTags.isValid() || numTags.value() > MaxTags) {
    return d.fail("too many tags");
  }

  if (!codeMeta->tags.reserve(numTags.value())) {
    return false;
  }

  for (uint32_t i = 0; i < numDefs; i++) {
    uint32_t funcTypeIndex;
    if (!DecodeTagType(d, codeMeta, &funcTypeIndex)) {
      return false;
    }
    MutableTagType tagType = js_new<TagType>();
    if (!tagType || !tagType->initialize(&(*codeMeta->types)[funcTypeIndex])) {
      return false;
    }
    codeMeta->tags.infallibleEmplaceBack(TagKind::Exception, tagType);
  }

  return d.finishSection(*range, "tag");
}

using NameSet = HashSet<Span<char>, NameHasher, SystemAllocPolicy>;

[[nodiscard]] static bool DecodeExportName(Decoder& d, NameSet* dupSet,
                                           CacheableName* exportName) {
  if (!DecodeName(d, exportName)) {
    d.fail("expected valid export name");
    return false;
  }

  NameSet::AddPtr p = dupSet->lookupForAdd(exportName->utf8Bytes());
  if (p) {
    d.fail("duplicate export");
    return false;
  }

  return dupSet->add(p, exportName->utf8Bytes());
}

static bool DecodeExport(Decoder& d, CodeMetadata* codeMeta,
                         ModuleMetadata* moduleMeta, NameSet* dupSet) {
  CacheableName fieldName;
  if (!DecodeExportName(d, dupSet, &fieldName)) {
    return false;
  }

  uint8_t exportKind;
  if (!d.readFixedU8(&exportKind)) {
    return d.fail("failed to read export kind");
  }

  switch (DefinitionKind(exportKind)) {
    case DefinitionKind::Function: {
      uint32_t funcIndex;
      if (!d.readVarU32(&funcIndex)) {
        return d.fail("expected function index");
      }

      if (funcIndex >= codeMeta->numFuncs()) {
        return d.fail("exported function index out of bounds");
      }

      codeMeta->funcs[funcIndex].declareFuncExported(/* eager */ true,
                                                     /* canRefFunc */ true);
      return moduleMeta->exports.emplaceBack(std::move(fieldName), funcIndex,
                                             DefinitionKind::Function);
    }
    case DefinitionKind::Table: {
      uint32_t tableIndex;
      if (!d.readVarU32(&tableIndex)) {
        return d.fail("expected table index");
      }

      if (tableIndex >= codeMeta->tables.length()) {
        return d.fail("exported table index out of bounds");
      }
      codeMeta->tables[tableIndex].isExported = true;
      return moduleMeta->exports.emplaceBack(std::move(fieldName), tableIndex,
                                             DefinitionKind::Table);
    }
    case DefinitionKind::Memory: {
      uint32_t memoryIndex;
      if (!d.readVarU32(&memoryIndex)) {
        return d.fail("expected memory index");
      }

      if (memoryIndex >= codeMeta->numMemories()) {
        return d.fail("exported memory index out of bounds");
      }

      return moduleMeta->exports.emplaceBack(std::move(fieldName), memoryIndex,
                                             DefinitionKind::Memory);
    }
    case DefinitionKind::Global: {
      uint32_t globalIndex;
      if (!d.readVarU32(&globalIndex)) {
        return d.fail("expected global index");
      }

      if (globalIndex >= codeMeta->globals.length()) {
        return d.fail("exported global index out of bounds");
      }

      GlobalDesc* global = &codeMeta->globals[globalIndex];
      global->setIsExport();

      return moduleMeta->exports.emplaceBack(std::move(fieldName), globalIndex,
                                             DefinitionKind::Global);
    }
    case DefinitionKind::Tag: {
      uint32_t tagIndex;
      if (!d.readVarU32(&tagIndex)) {
        return d.fail("expected tag index");
      }
      if (tagIndex >= codeMeta->tags.length()) {
        return d.fail("exported tag index out of bounds");
      }

      codeMeta->tags[tagIndex].isExport = true;
      return moduleMeta->exports.emplaceBack(std::move(fieldName), tagIndex,
                                             DefinitionKind::Tag);
    }
    default:
      return d.fail("unexpected export kind");
  }

  MOZ_CRASH("unreachable");
}

static bool DecodeExportSection(Decoder& d, CodeMetadata* codeMeta,
                                ModuleMetadata* moduleMeta) {
  MaybeBytecodeRange range;
  if (!d.startSection(SectionId::Export, codeMeta, &range, "export")) {
    return false;
  }
  if (!range) {
    return true;
  }

  NameSet dupSet;

  uint32_t numExports;
  if (!d.readVarU32(&numExports)) {
    return d.fail("failed to read number of exports");
  }

  if (numExports > MaxExports) {
    return d.fail("too many exports");
  }

  for (uint32_t i = 0; i < numExports; i++) {
    if (!DecodeExport(d, codeMeta, moduleMeta, &dupSet)) {
      return false;
    }
  }

  return d.finishSection(*range, "export");
}

static bool DecodeStartSection(Decoder& d, CodeMetadata* codeMeta,
                               ModuleMetadata* moduleMeta) {
  MaybeBytecodeRange range;
  if (!d.startSection(SectionId::Start, codeMeta, &range, "start")) {
    return false;
  }
  if (!range) {
    return true;
  }

  uint32_t funcIndex;
  if (!d.readVarU32(&funcIndex)) {
    return d.fail("failed to read start func index");
  }

  if (funcIndex >= codeMeta->numFuncs()) {
    return d.fail("unknown start function");
  }

  const FuncType& funcType = codeMeta->getFuncType(funcIndex);
  if (funcType.results().length() > 0) {
    return d.fail("start function must not return anything");
  }

  if (funcType.args().length()) {
    return d.fail("start function must be nullary");
  }

  codeMeta->funcs[funcIndex].declareFuncExported(/* eager */ true,
                                                 /* canFuncRef */ false);
  codeMeta->startFuncIndex = Some(funcIndex);

  return d.finishSection(*range, "start");
}

static inline ModuleElemSegment::Kind NormalizeElemSegmentKind(
    ElemSegmentKind decodedKind) {
  switch (decodedKind) {
    case ElemSegmentKind::Active:
    case ElemSegmentKind::ActiveWithTableIndex: {
      return ModuleElemSegment::Kind::Active;
    }
    case ElemSegmentKind::Passive: {
      return ModuleElemSegment::Kind::Passive;
    }
    case ElemSegmentKind::Declared: {
      return ModuleElemSegment::Kind::Declared;
    }
  }
  MOZ_CRASH("unexpected elem segment kind");
}

static bool DecodeElemSegment(Decoder& d, CodeMetadata* codeMeta,
                              ModuleMetadata* moduleMeta) {
  uint32_t segmentFlags;
  if (!d.readVarU32(&segmentFlags)) {
    return d.fail("expected elem segment flags field");
  }

  Maybe<ElemSegmentFlags> flags = ElemSegmentFlags::construct(segmentFlags);
  if (!flags) {
    return d.fail("invalid elem segment flags field");
  }

  ModuleElemSegment seg = ModuleElemSegment();

  ElemSegmentKind segmentKind = flags->kind();
  seg.kind = NormalizeElemSegmentKind(segmentKind);

  if (segmentKind == ElemSegmentKind::Active ||
      segmentKind == ElemSegmentKind::ActiveWithTableIndex) {
    if (codeMeta->tables.length() == 0) {
      return d.fail("active elem segment requires a table");
    }

    uint32_t tableIndex = 0;
    if (segmentKind == ElemSegmentKind::ActiveWithTableIndex &&
        !d.readVarU32(&tableIndex)) {
      return d.fail("expected table index");
    }
    if (tableIndex >= codeMeta->tables.length()) {
      return d.fail("table index out of range for element segment");
    }
    seg.tableIndex = tableIndex;

    InitExpr offset;
    if (!InitExpr::decodeAndValidate(
            d, codeMeta, ToValType(codeMeta->tables[tableIndex].addressType()),
            &offset)) {
      return false;
    }
    seg.offsetIfActive.emplace(std::move(offset));
  } else {
    // Too many bugs result from keeping this value zero.  For passive
    // or declared segments, there really is no table index, and we should
    // never touch the field.
    MOZ_ASSERT(segmentKind == ElemSegmentKind::Passive ||
               segmentKind == ElemSegmentKind::Declared);
    seg.tableIndex = (uint32_t)-1;
  }

  ElemSegmentPayload payload = flags->payload();
  RefType elemType;

  // `ActiveWithTableIndex`, `Declared`, and `Passive` element segments encode
  // the type or definition kind of the payload. `Active` element segments are
  // restricted to MVP behavior, which assumes only function indices.
  if (segmentKind == ElemSegmentKind::Active) {
    // Bizarrely, the spec prescribes that the default type is (ref func) when
    // encoding function indices, and (ref null func) when encoding expressions.
    elemType = payload == ElemSegmentPayload::Expressions
                   ? RefType::func()
                   : RefType::func().asNonNullable();
  } else {
    switch (payload) {
      case ElemSegmentPayload::Expressions: {
        if (!d.readRefType(*codeMeta->types, codeMeta->features(), &elemType)) {
          return false;
        }
      } break;
      case ElemSegmentPayload::Indices: {
        uint8_t elemKind;
        if (!d.readFixedU8(&elemKind)) {
          return d.fail("expected element kind");
        }

        if (elemKind != uint8_t(DefinitionKind::Function)) {
          return d.fail("invalid element kind");
        }
        elemType = RefType::func().asNonNullable();
      } break;
    }
  }

  // For active segments, check if the element type is compatible with the
  // destination table type.
  if (seg.active()) {
    RefType tblElemType = codeMeta->tables[seg.tableIndex].elemType();
    if (!CheckIsSubtypeOf(d, *codeMeta, d.currentOffset(),
                          ValType(elemType).storageType(),
                          ValType(tblElemType).storageType())) {
      return false;
    }
  }
  seg.elemType = elemType;

  uint32_t numElems;
  if (!d.readVarU32(&numElems)) {
    return d.fail("expected element segment size");
  }

  if (numElems > MaxElemSegmentLength) {
    return d.fail("too many elements in element segment");
  }

  switch (payload) {
    case ElemSegmentPayload::Indices: {
      seg.encoding = ModuleElemSegment::Encoding::Indices;
      if (!seg.elemIndices.reserve(numElems)) {
        return false;
      }

      for (uint32_t i = 0; i < numElems; i++) {
        uint32_t elemIndex;
        if (!d.readVarU32(&elemIndex)) {
          return d.fail("failed to read element index");
        }
        // The only valid type of index right now is a function index.
        if (elemIndex >= codeMeta->numFuncs()) {
          return d.fail("element index out of range");
        }

        seg.elemIndices.infallibleAppend(elemIndex);
        codeMeta->funcs[elemIndex].declareFuncExported(/*eager=*/false,
                                                       /*canRefFunc=*/true);
      }
    } break;
    case ElemSegmentPayload::Expressions: {
      seg.encoding = ModuleElemSegment::Encoding::Expressions;
      const uint8_t* exprsStart = d.currentPosition();
      seg.elemExpressions.count = numElems;
      for (uint32_t i = 0; i < numElems; i++) {
        Maybe<LitVal> unusedLiteral;
        if (!DecodeConstantExpression(d, codeMeta, elemType, &unusedLiteral)) {
          return false;
        }
      }
      const uint8_t* exprsEnd = d.currentPosition();
      if (!seg.elemExpressions.exprBytes.append(exprsStart, exprsEnd)) {
        return false;
      }
    } break;
  }

  codeMeta->elemSegmentTypes.infallibleAppend(seg.elemType);
  moduleMeta->elemSegments.infallibleAppend(std::move(seg));

  return true;
}

static bool DecodeElemSection(Decoder& d, CodeMetadata* codeMeta,
                              ModuleMetadata* moduleMeta) {
  MaybeBytecodeRange range;
  if (!d.startSection(SectionId::Elem, codeMeta, &range, "elem")) {
    return false;
  }
  if (!range) {
    return true;
  }

  uint32_t numSegments;
  if (!d.readVarU32(&numSegments)) {
    return d.fail("failed to read number of elem segments");
  }

  if (numSegments > MaxElemSegments) {
    return d.fail("too many elem segments");
  }

  if (!moduleMeta->elemSegments.reserve(numSegments) ||
      !codeMeta->elemSegmentTypes.reserve(numSegments)) {
    return false;
  }

  for (uint32_t i = 0; i < numSegments; i++) {
    if (!DecodeElemSegment(d, codeMeta, moduleMeta)) {
      return false;
    }
  }

  return d.finishSection(*range, "elem");
}

static bool DecodeDataCountSection(Decoder& d, CodeMetadata* codeMeta) {
  MaybeBytecodeRange range;
  if (!d.startSection(SectionId::DataCount, codeMeta, &range, "datacount")) {
    return false;
  }
  if (!range) {
    return true;
  }

  uint32_t dataCount;
  if (!d.readVarU32(&dataCount)) {
    return d.fail("expected data segment count");
  }

  codeMeta->dataCount.emplace(dataCount);

  return d.finishSection(*range, "datacount");
}

bool wasm::StartsCodeSection(const uint8_t* begin, const uint8_t* end,
                             BytecodeRange* codeSection) {
  UniqueChars unused;
  Decoder d(begin, end, 0, &unused);

  if (!DecodePreamble(d, EncodingVersionModule)) {
    return false;
  }

  while (!d.done()) {
    uint8_t id;
    BytecodeRange range;
    if (!d.readSectionHeader(&id, &range)) {
      return false;
    }

    if (id == uint8_t(SectionId::Code)) {
      if (range.size() > MaxCodeSectionBytes) {
        return false;
      }

      *codeSection = range;
      return true;
    }

    if (!d.readBytes(range.size())) {
      return false;
    }
  }

  return false;
}

#ifdef ENABLE_WASM_BRANCH_HINTING
static bool ParseBranchHintingSection(Decoder& d, CodeMetadata* codeMeta) {
  uint32_t functionCount;
  if (!d.readVarU32(&functionCount)) {
    return d.fail("failed to read function count");
  }

  for (uint32_t i = 0; i < functionCount; i++) {
    uint32_t functionIndex;
    if (!d.readVarU32(&functionIndex)) {
      return d.fail("failed to read function index");
    }

    // Disallow branch hints on imported functions.
    if ((functionIndex >= codeMeta->funcs.length()) ||
        (functionIndex < codeMeta->numFuncImports)) {
      return d.fail("invalid function index in branch hint");
    }

    uint32_t hintCount;
    if (!d.readVarU32(&hintCount)) {
      return d.fail("failed to read hint count");
    }

    BranchHintVector hintVector;
    if (!hintVector.reserve(hintCount)) {
      return false;
    }

    // Branch hint offsets must appear in increasing byte offset order, at most
    // once for each offset.
    uint32_t prevOffsetPlus1 = 0;
    for (uint32_t hintIndex = 0; hintIndex < hintCount; hintIndex++) {
      uint32_t branchOffset;
      if (!d.readVarU32(&branchOffset)) {
        return d.fail("failed to read branch offset");
      }
      if (branchOffset <= prevOffsetPlus1) {
        return d.fail("Invalid offset in code hint");
      }

      uint32_t reserved;
      if (!d.readVarU32(&reserved) || (reserved != 1)) {
        return d.fail("Invalid reserved value for code hint");
      }

      uint32_t branchHintValue;
      if (!d.readVarU32(&branchHintValue) ||
          (branchHintValue >= MaxBranchHintValue)) {
        return d.fail("Invalid branch hint value");
      }

      BranchHint branchHint = static_cast<BranchHint>(branchHintValue);
      BranchHintEntry entry(branchOffset, branchHint);
      hintVector.infallibleAppend(entry);

      prevOffsetPlus1 = branchOffset;
    }

    // Save this collection in the module
    if (!codeMeta->branchHints.addHintsForFunc(functionIndex,
                                               std::move(hintVector))) {
      return false;
    }
  }

  return true;
}

static bool DecodeBranchHintingSection(Decoder& d, CodeMetadata* codeMeta) {
  MaybeBytecodeRange range;
  if (!d.startCustomSection(BranchHintingSectionName, codeMeta, &range)) {
    return false;
  }
  if (!range) {
    return true;
  }

  // Skip this custom section if errors are encountered during parsing.
  if (!ParseBranchHintingSection(d, codeMeta)) {
    codeMeta->branchHints.setFailedAndClear();
  }

  if (!d.finishCustomSection(BranchHintingSectionName, *range)) {
    codeMeta->branchHints.setFailedAndClear();
  }
  return true;
}
#endif

#ifdef ENABLE_WASM_COMPONENTS
bool wasm::IsComponent(Decoder& d) {
  uint32_t magic;
  if (!d.readFixedU32(&magic) || magic != MagicNumber) {
    return false;
  }

  uint32_t version;
  if (!d.readFixedU32(&version)) {
    return false;
  }

  return version == EncodingVersionComponent;
}
#endif

bool wasm::DecodeModuleEnvironment(Decoder& d, CodeMetadata* codeMeta,
                                   ModuleMetadata* moduleMeta) {
  if (!DecodePreamble(d, EncodingVersionModule)) {
    return false;
  }

  if (!DecodeTypeSection(d, codeMeta)) {
    return false;
  }

  if (!DecodeImportSection(d, codeMeta, moduleMeta)) {
    return false;
  }

  // Eagerly check imports for future link errors against any known builtin
  // module.
  if (!CheckImportsAgainstBuiltinModules(d, codeMeta, moduleMeta)) {
    return false;
  }

  if (!DecodeFunctionSection(d, codeMeta)) {
    return false;
  }

  if (!DecodeTableSection(d, codeMeta)) {
    return false;
  }

  if (!DecodeMemorySection(d, codeMeta)) {
    return false;
  }

  if (!CheckBuiltinImportsHaveMemory(d, codeMeta)) {
    return false;
  }

  if (!DecodeTagSection(d, codeMeta)) {
    return false;
  }

  if (!DecodeGlobalSection(d, codeMeta)) {
    return false;
  }

  if (!DecodeExportSection(d, codeMeta, moduleMeta)) {
    return false;
  }

  if (!DecodeStartSection(d, codeMeta, moduleMeta)) {
    return false;
  }

  if (!DecodeElemSection(d, codeMeta, moduleMeta)) {
    return false;
  }

  if (!DecodeDataCountSection(d, codeMeta)) {
    return false;
  }

#ifdef ENABLE_WASM_BRANCH_HINTING
  if (codeMeta->branchHintingEnabled() &&
      !DecodeBranchHintingSection(d, codeMeta)) {
    return false;
  }
#endif

  if (!d.startSection(SectionId::Code, codeMeta, &codeMeta->codeSectionRange,
                      "code")) {
    return false;
  }

  if (codeMeta->codeSectionRange &&
      codeMeta->codeSectionRange->size() > MaxCodeSectionBytes) {
    return d.fail("code section too big");
  }

  return true;
}

static bool DecodeFunctionBody(Decoder& d, const CodeMetadata& codeMeta,
                               uint32_t funcIndex) {
  uint32_t bodySize;
  if (!d.readVarU32(&bodySize)) {
    return d.fail("expected number of function body bytes");
  }

  if (bodySize > MaxFunctionBytes) {
    return d.fail("function body too big");
  }

  if (d.bytesRemain() < bodySize) {
    return d.fail("function body length too big");
  }

  return ValidateFunctionBody(codeMeta, funcIndex, bodySize, d);
}

static bool DecodeCodeSection(Decoder& d, CodeMetadata* codeMeta) {
  if (!codeMeta->codeSectionRange) {
    if (codeMeta->numFuncDefs() != 0) {
      return d.fail("expected code section");
    }
    return true;
  }

  uint32_t numFuncDefs;
  if (!d.readVarU32(&numFuncDefs)) {
    return d.fail("expected function body count");
  }

  if (numFuncDefs != codeMeta->numFuncDefs()) {
    return d.fail(
        "function body count does not match function signature count");
  }

  for (uint32_t funcDefIndex = 0; funcDefIndex < numFuncDefs; funcDefIndex++) {
    if (!DecodeFunctionBody(d, *codeMeta,
                            codeMeta->numFuncImports + funcDefIndex)) {
      return false;
    }
  }

  return d.finishSection(*codeMeta->codeSectionRange, "code");
}

static bool DecodeDataSection(Decoder& d, CodeMetadata* codeMeta,
                              ModuleMetadata* moduleMeta) {
  MaybeBytecodeRange range;
  if (!d.startSection(SectionId::Data, codeMeta, &range, "data")) {
    return false;
  }
  if (!range) {
    if (codeMeta->dataCount.isSome() && *codeMeta->dataCount > 0) {
      return d.fail("number of data segments does not match declared count");
    }
    return true;
  }

  uint32_t numSegments;
  if (!d.readVarU32(&numSegments)) {
    return d.fail("failed to read number of data segments");
  }

  if (numSegments > MaxDataSegments) {
    return d.fail("too many data segments");
  }

  if (codeMeta->dataCount.isSome() && numSegments != *codeMeta->dataCount) {
    return d.fail("number of data segments does not match declared count");
  }

  for (uint32_t i = 0; i < numSegments; i++) {
    uint32_t initializerKindVal;
    if (!d.readVarU32(&initializerKindVal)) {
      return d.fail("expected data initializer-kind field");
    }

    switch (initializerKindVal) {
      case uint32_t(DataSegmentKind::Active):
      case uint32_t(DataSegmentKind::Passive):
      case uint32_t(DataSegmentKind::ActiveWithMemoryIndex):
        break;
      default:
        return d.fail("invalid data initializer-kind field");
    }

    DataSegmentKind initializerKind = DataSegmentKind(initializerKindVal);

    if (initializerKind != DataSegmentKind::Passive &&
        codeMeta->numMemories() == 0) {
      return d.fail("active data segment requires a memory section");
    }

    DataSegmentRange segRange;
    if (initializerKind == DataSegmentKind::ActiveWithMemoryIndex) {
      if (!d.readVarU32(&segRange.memoryIndex)) {
        return d.fail("expected memory index");
      }
    } else if (initializerKind == DataSegmentKind::Active) {
      segRange.memoryIndex = 0;
    } else {
      segRange.memoryIndex = InvalidMemoryIndex;
    }

    if (initializerKind == DataSegmentKind::Active ||
        initializerKind == DataSegmentKind::ActiveWithMemoryIndex) {
      if (segRange.memoryIndex >= codeMeta->numMemories()) {
        return d.fail("invalid memory index");
      }

      InitExpr segOffset;
      ValType exprType =
          ToValType(codeMeta->memories[segRange.memoryIndex].addressType());
      if (!InitExpr::decodeAndValidate(d, codeMeta, exprType, &segOffset)) {
        return false;
      }
      segRange.offsetIfActive.emplace(std::move(segOffset));
    }

    if (!d.readVarU32(&segRange.length)) {
      return d.fail("expected segment size");
    }

    if (segRange.length > MaxDataSegmentLengthPages * StandardPageSizeBytes) {
      return d.fail("segment size too big");
    }

    segRange.bytecodeOffset = d.currentOffset();

    if (!d.readBytes(segRange.length)) {
      return d.fail("data segment shorter than declared");
    }

    if (!moduleMeta->dataSegmentRanges.append(std::move(segRange))) {
      return false;
    }
  }

  return d.finishSection(*range, "data");
}

static bool DecodeNameSectionName(Decoder& d,
                                  const CustomSectionRange& nameSection,
                                  const char* thing, Name* result) {
  uint32_t length;
  if (!d.readVarU32(&length)) {
    return d.failf("failed to read %s name length", thing);
  }

  MOZ_ASSERT(d.currentOffset() >= nameSection.payload.start);
  uint32_t offsetInNamePayload = d.currentOffset() - nameSection.payload.start;

  const uint8_t* nameBytes;
  if (!d.readBytes(length, &nameBytes)) {
    return d.failf("failed to read %s name", thing);
  }

  if (!IsUtf8(AsChars(mozilla::Span(nameBytes, length)))) {
    return d.failf("%s name was not valid UTF-8", thing);
  }

  result->offsetInNamePayload = offsetInNamePayload;
  result->length = length;
  return true;
}

template <typename F>
static bool DecodeNameMapEntries(Decoder& d,
                                 const CustomSectionRange& nameSection,
                                 const char* thing, uint32_t maxThings,
                                 const F& addName) {
  uint32_t numNames;
  if (!d.readVarU32(&numNames)) {
    return d.failf("expected number of %s names", thing);
  }
  if (numNames > maxThings) {
    return d.failf("too many %s names", thing);
  }
  uint32_t minIdx = 0;
  for (uint32_t i = 0; i < numNames; i++) {
    uint32_t idx;
    if (!d.readVarU32(&idx)) {
      return d.failf("expected %s index", thing);
    }
    if (idx >= maxThings) {
      return d.failf("%s index %" PRIu32 " is too large", thing, idx);
    }
    if (idx < minIdx) {
      return d.failf("out of order %s index", thing);
    }
    minIdx = idx + 1;

    Name name;
    if (!DecodeNameSectionName(d, nameSection, thing, &name)) {
      return false;
    }

    if (!addName(idx, name)) {
      return false;
    }
  }

  return true;
}

template <typename F>
static bool DecodeIndirectNameMapEntries(
    Decoder& d, const CustomSectionRange& nameSection, const char* outerThing,
    uint32_t maxOuterThings, const char* innerThing, uint32_t maxInnerThings,
    const F& addName) {
  uint32_t numOuterThingLists;
  if (!d.readVarU32(&numOuterThingLists)) {
    return d.failf("expected number of %s name lists", outerThing);
  }
  if (numOuterThingLists > maxOuterThings) {
    return d.failf("too many %s name lists", outerThing);
  }
  uint32_t minOuterIdx = 0;
  for (uint32_t i = 0; i < numOuterThingLists; i++) {
    uint32_t outerIdx;
    if (!d.readVarU32(&outerIdx)) {
      return d.failf("expected %s index", outerThing);
    }
    if (outerIdx >= maxOuterThings) {
      return d.failf("%s index %" PRIu32 " is too large", outerThing, outerIdx);
    }
    if (outerIdx < minOuterIdx) {
      return d.failf("out of order %s index", outerThing);
    }
    minOuterIdx = outerIdx + 1;

    if (!DecodeNameMapEntries(d, nameSection, innerThing, maxInnerThings,
                              [&](uint32_t innerIdx, const Name& name) {
                                return addName(
                                    IndirectNameKey(outerIdx, innerIdx), name);
                              })) {
      return false;
    }
  }

  return true;
}

static bool DecodeNameMap(Decoder& d, const CustomSectionRange& nameSection,
                          const char* thing, uint32_t maxThings,
                          NameMap& result) {
  return DecodeNameMapEntries(d, nameSection, thing, maxThings,
                              [&](uint32_t idx, const Name& name) -> bool {
                                return result.put(idx, name);
                              });
}

static bool ValidateNameMap(Decoder& d, const CustomSectionRange& nameSection,
                            const char* thing, uint32_t maxThings) {
  return DecodeNameMapEntries(
      d, nameSection, thing, maxThings,
      [&](uint32_t idx, const Name& name) -> bool { return true; });
}

// Right now we have no need to actually save any indirect name maps, but to do
// so in the future you can just copy ValidateIndirectNameMap and change the
// lambda to actually save names into a map.

static bool ValidateIndirectNameMap(
    Decoder& d, const CustomSectionRange& nameSection, const char* outerThing,
    uint32_t maxOuterThings, const char* innerThing, uint32_t maxInnerThings) {
  return DecodeIndirectNameMapEntries(
      d, nameSection, outerThing, maxOuterThings, innerThing, maxInnerThings,
      [&](IndirectNameKey&& key, const Name& name) { return true; });
}

static bool DecodeNameSubsection(Decoder& d,
                                 const CustomSectionRange& nameSection,
                                 CodeMetadata* codeMeta, NameType nameType) {
  switch (NameType(nameType)) {
    case NameType::Module: {
      Name moduleName;
      if (!DecodeNameSectionName(d, nameSection, "module", &moduleName)) {
        return false;
      }
      codeMeta->nameSection->moduleName = moduleName;
    } break;
    case NameType::Function: {
      NameMap funcNames;
      if (!DecodeNameMap(d, nameSection, "function", MaxFuncs, funcNames)) {
        return false;
      }
      codeMeta->nameSection->funcNames = std::move(funcNames);
    } break;
    case NameType::Local: {
      if (!ValidateIndirectNameMap(d, nameSection, "func", MaxFuncs, "local",
                                   MaxLocals)) {
        return false;
      }
    } break;
    case NameType::Label: {
      if (!ValidateIndirectNameMap(d, nameSection, "func", MaxFuncs, "label",
                                   MaxFunctionBytes / 2)) {
        return false;
      }
    } break;
    case NameType::Type: {
      if (!ValidateNameMap(d, nameSection, "type", MaxTypes)) {
        return false;
      }
    } break;
    case NameType::Table: {
      if (!ValidateNameMap(d, nameSection, "table", MaxTables)) {
        return false;
      }
    } break;
    case NameType::Memory: {
      if (!ValidateNameMap(d, nameSection, "memory", MaxMemories)) {
        return false;
      }
    } break;
    case NameType::Global: {
      if (!ValidateNameMap(d, nameSection, "global", MaxGlobals)) {
        return false;
      }
    } break;
    case NameType::ElemSegment: {
      if (!ValidateNameMap(d, nameSection, "elem segment", MaxElemSegments)) {
        return false;
      }
    } break;
    case NameType::DataSegment: {
      if (!ValidateNameMap(d, nameSection, "data segment", MaxDataSegments)) {
        return false;
      }
    } break;
    case NameType::Field: {
      if (!ValidateIndirectNameMap(d, nameSection, "type", MaxTypes, "field",
                                   MaxStructFields)) {
        return false;
      }
    } break;
    case NameType::Tag: {
      if (!ValidateNameMap(d, nameSection, "tag", MaxTags)) {
        return false;
      }
    } break;
    case NameType::Param: {
      if (!ValidateIndirectNameMap(d, nameSection, "type", MaxTypes, "param",
                                   MaxParams)) {
        return false;
      }
    } break;
    case NameType::TagParam: {
      if (!ValidateIndirectNameMap(d, nameSection, "tag", MaxTags, "param",
                                   MaxParams)) {
        return false;
      }
    } break;
    case NameType::Last:
    default:
      MOZ_CRASH();
  }
  return true;
}

static bool DecodeNameSection(Decoder& d, CodeMetadata* codeMeta,
                              ModuleMetadata* moduleMeta) {
  MaybeBytecodeRange range;
  if (!d.startCustomSection(NameSectionName, codeMeta, &range)) {
    return false;
  }
  if (!range) {
    return true;
  }

  codeMeta->nameSection.emplace();
  codeMeta->nameSection->customSectionIndex =
      uint32_t(codeMeta->customSectionRanges.length() - 1);
  const CustomSectionRange& nameSection = codeMeta->customSectionRanges.back();

  Decoder nameSectionDecoder(d.currentPosition(),
                             d.currentPosition() + nameSection.payload.size(),
                             d.currentOffset(), d.error(), d.warnings());
  {
    Decoder& d = nameSectionDecoder;
    // Once started, custom sections do not report validation errors.

    uint8_t minNameType = 0;
    while (true) {
      if (d.done()) {
        break;
      }

      uint8_t nameTypeValue;
      if (!d.readFixedU8(&nameTypeValue)) {
        d.fail("expected name subsection type");
        break;
      }
      if (nameTypeValue < minNameType) {
        d.fail("out of order name subsections");
        break;
      }
      if (nameTypeValue >= uint8_t(NameType::Last)) {
        d.fail("invalid name subsection id");
        break;
      }
      minNameType = nameTypeValue + 1;

      uint32_t payloadLength;
      if (!d.readVarU32(&payloadLength) || payloadLength > d.bytesRemain()) {
        d.fail("bad name subsection payload length");
        break;
      }

      Decoder nameSubsectionDecoder(d.currentPosition(),
                                    d.currentPosition() + payloadLength,
                                    d.currentOffset(), d.error(), d.warnings());
      if (!DecodeNameSubsection(nameSubsectionDecoder, nameSection, codeMeta,
                                NameType(nameTypeValue))) {
        break;
      }
      if (!nameSubsectionDecoder.done()) {
        d.fail("unconsumed bytes in name subsection");
        break;
      }
      MOZ_RELEASE_ASSERT(d.readBytes(payloadLength));
    }
  }
  MOZ_RELEASE_ASSERT(d.readBytes(nameSection.payload.size()));

  if (!d.finishCustomSection(NameSectionName, *range)) {
    codeMeta->nameSection = mozilla::Nothing();
  }
  return true;
}

bool wasm::DecodeModuleTail(Decoder& d, CodeMetadata* codeMeta,
                            ModuleMetadata* moduleMeta) {
  if (!DecodeDataSection(d, codeMeta, moduleMeta)) {
    return false;
  }

  if (!DecodeNameSection(d, codeMeta, moduleMeta)) {
    return false;
  }

  while (!d.done()) {
    if (!d.skipCustomSection(codeMeta)) {
      return false;
    }
  }

  return true;
}

#ifdef ENABLE_WASM_COMPONENTS

enum class ComponentNameFragmentKind {
  Unknown = 0,
  Word,
  Acronym,
};

// In the component model, names consist primarily of
// series-OF-possibly-UPPERCASE-fragments, where each fragment is all lowercase
// or all uppercase. A lowercase fragment is called a "word"; an uppercase
// fragment is called an "acronym". Additionally, names cannot start with
// digits. To give you a flavor, here are some of the grammar rules for names:
//
//     plainname         ::= <label>
//                         | '[constructor]' <label>
//                         | '[method]' <label> '.' <label>
//                         | '[static]' <label> '.' <label>
//     interfacename     ::= <namespace> <words> <projection> ...
//     namespace         ::= <words> ':'
//     projection        ::= '/' <label>
//
//     label             ::= <first-fragment> ( '-' <fragment> )*
//     words             ::= <first-word> ( '-' <word> )*
//
//     first-word        ::= [a-z] [0-9a-z]*
//     first-acronym     ::= [A-Z] [0-9A-Z]*
//     first-fragment    ::= <first-word>
//                         | <first-acronym>
//     word              ::= [0-9a-z]+
//     acronym           ::= [0-9A-Z]+
//     fragment          ::= <word>
//                         | <acronym>
//
// This is a maze, but at the end of the day it boils down to: parse a series of
// hyphen-separated identifiers, sometimes allowing uppercase letters (as in
// `plainname`) and sometimes not (as in `namespace`). For our own sanity, we
// just call everything a "label" in our code and explicitly indicate whether
// uppercase is allowed.
[[nodiscard]] static bool DecodeComponentLabel(Decoder& d, const char* thing,
                                               bool allowUppercase) {
  // The first fragment in a label must start with a letter. Other fragments can
  // start with a digit.
  bool isFirstFragment = true;
  ComponentNameFragmentKind fragmentKind;

  while (true) {
    fragmentKind = ComponentNameFragmentKind::Unknown;

    uint8_t firstChar;
    if (!d.readFixedU8(&firstChar)) {
      return d.failf("%s name ended unexpectedly", thing);
    }
    bool firstLowercase = 'a' <= firstChar && firstChar <= 'z';
    bool firstUppercase = 'A' <= firstChar && firstChar <= 'Z';
    bool firstDigit = '0' <= firstChar && firstChar <= '9';
    if (!firstLowercase && !firstUppercase && !firstDigit) {
      return d.failf("invalid character in %s name", thing);
    }
    if (isFirstFragment && !(firstLowercase || firstUppercase)) {
      return d.failf("%s name must start with a letter", thing);
    }
    if (firstUppercase && !allowUppercase) {
      return d.failf("%s name had unexpected uppercase letter", thing);
    }

    if (firstLowercase) {
      fragmentKind = ComponentNameFragmentKind::Word;
    } else if (firstUppercase) {
      fragmentKind = ComponentNameFragmentKind::Acronym;
    }

    uint8_t b;
    while (d.peekByte(&b)) {
      if (b == '-') {
        break;
      }

      bool lowercase = 'a' <= b && b <= 'z';
      bool uppercase = 'A' <= b && b <= 'Z';
      bool digit = '0' <= b && b <= '9';
      if (!lowercase && !uppercase && !digit) {
        // We are immediately done because we encountered a non-word symbol at
        // the end of something that could be valid.
        return true;
      }
      if ((lowercase && fragmentKind == ComponentNameFragmentKind::Acronym) ||
          (uppercase && fragmentKind == ComponentNameFragmentKind::Word)) {
        return d.failf("mixed case in %s name", thing);
      }

      if (fragmentKind == ComponentNameFragmentKind::Unknown) {
        if (lowercase) {
          fragmentKind = ComponentNameFragmentKind::Word;
        } else if (uppercase) {
          fragmentKind = ComponentNameFragmentKind::Acronym;
        }
        // If a digit, the state remains Unknown.
      }

      MOZ_RELEASE_ASSERT(d.readBytes(1));
    }
    if (d.done()) {
      return true;
    }

    MOZ_RELEASE_ASSERT(d.readLiteral("-"));
    isFirstFragment = false;
  }
}

[[nodiscard]] static bool DecodeComponentName(Decoder& d, const char* thing,
                                              ComponentName* name) {
  using Attr = ComponentNameAttribute;

  uint32_t len;
  if (!d.readVarU32(&len)) {
    return d.fail("expected name");
  }
  if (len == 0) {
    return d.failf("%s name cannot be empty", thing);
  }
  if (len > d.bytesRemain()) {
    return d.fail("over-long name");
  }

  ComponentNameAttributes attrs;
  uint8_t attributesLength = 0;
  uint32_t resourceNameLength = 0;
  Decoder nameDecoder(d.currentPosition(), d.currentPosition() + len,
                      d.currentOffset(), d.error(), d.warnings());
  {
    Decoder& d = nameDecoder;

    // Get some unusual kinds of component names out of the way. In the future
    // we could choose to support some of these.
    if (d.peekLiteral("url=")) {
      return d.fail("URL names are not supported");
    } else if (d.peekLiteral("integrity=")) {
      return d.fail("hash names are not supported");
    } else if (d.peekLiteral("unlocked-dep=") || d.peekLiteral("locked-dep=")) {
      return d.fail("dependency names are not supported");
    }

    // Now all we have to deal with are plain names and interface names.
    // Examples of each would be:
    //
    // - Plain names: foo-BAR-baz, [constructor]FOO-BAR, [method]foo.BAR,
    //   [static]foo-BAR.BEEP-boop, [get]prop, [method][get]foo.prop,
    //   [static][set]foo.prop-2
    // - Interface names: wasi:cli/stdout,
    //   wasi:clocks/imports@0.3.0-rc-2026-03-15,
    //   foo-bar:BEEP-boop/boop-BEEP@<[a-zA-Z0-9.+-]+>
    //
    // For interface names, all three of namespace (e.g. "wasi:"), name (e.g.
    // "cli"), and "projection" (e.g. "/stdout") are required, while the
    // version (e.g. "@0.3.0") is optional.
    //
    // We can't distinguish up front between a plain or interface name (unless
    // there is an annotation like "[constructor]"), so parsing must be ready
    // to accommodate either.
    //
    // TODO(wasm-cm): Today we reject interface names entirely; the parser
    // does not recognize the symbols used to delimit namespaces, projections,
    // or versions.

    const uint8_t* beforeAttributes = d.currentPosition();

    // [constructor]/[method]/[static] must come first and are mutually
    // exclusive
    if (d.readLiteral("[constructor]")) {
      attrs += Attr::Constructor;
    } else if (d.readLiteral("[method]")) {
      attrs += Attr::Method;
    } else if (d.readLiteral("[static]")) {
      attrs += Attr::Static;
    }

    // Then we can see [get] or [set]
    if (d.readLiteral("[get]")) {
      attrs += Attr::Get;
    } else if (d.readLiteral("[set]")) {
      attrs += Attr::Set;
    }

    const uint8_t* afterAttributes = d.currentPosition();
    MOZ_ASSERT(afterAttributes - beforeAttributes <=
               std::numeric_limits<decltype(attributesLength)>::max());
    attributesLength = afterAttributes - beforeAttributes;

    if (attrs.contains(Attr::Constructor)) {
      if (attrs.contains(Attr::Get) || attrs.contains(Attr::Set)) {
        return d.fail("cannot use [get] or [set] with [constructor]");
      }
      if (!DecodeComponentLabel(d, thing, /*allowUppercase=*/true)) {
        return false;
      }
      resourceNameLength = d.currentPosition() - afterAttributes;
    } else if (attrs.contains(Attr::Method) || attrs.contains(Attr::Static)) {
      if (!DecodeComponentLabel(d, thing, /*allowUppercase=*/true)) {
        return false;
      }
      resourceNameLength = d.currentPosition() - afterAttributes;
      if (d.done()) {
        return d.failf("%s name ended unexpectedly", thing);
      } else if (!d.readLiteral(".")) {
        return d.failf("invalid character in %s name", thing);
      }
      if (!DecodeComponentLabel(d, thing, /*allowUppercase=*/true)) {
        return false;
      }
    } else {
      if (!DecodeComponentLabel(d, thing, /*allowUppercase=*/true)) {
        return false;
      }
    }

    if (!d.done()) {
      return d.failf("invalid characters in %s name", thing);
    }
  }

  UTF8Bytes utf8Bytes;
  if (!d.readUTF8Bytes(len, &utf8Bytes)) {
    MOZ_CRASH("full name should have been decoded earlier");
  }
  *name = ComponentName(std::move(utf8Bytes), attrs, attributesLength,
                        resourceNameLength);

  return true;
}

[[nodiscard]] static bool DecodeComponentNameNoAttributes(Decoder& d,
                                                          const char* thing,
                                                          CacheableName* name) {
  ComponentName nameEx;
  if (!DecodeComponentName(d, thing, &nameEx)) {
    return false;
  }
  if (!nameEx.attributes.isEmpty()) {
    return d.failf("attributes are not allowed in %s names", thing);
  }

  *name = std::move(nameEx.name);
  return true;
}

// TODO(wasm-cm): Documentation
//
// Note that this function need not concern itself with canonicalization,
// because primitives don't need to be canonicalized and types already in the
// type section will have been canonicalized on their way in.
static bool DecodeComponentValType(Decoder& d, MutableComponent& c,
                                   ComponentType* t) {
  // Types in the binary are organized so that negative numbers are
  // primitives, while positive numbers are type indices.

  uint8_t nextByte;
  if (!d.peekByte(&nextByte)) {
    return d.fail("expected value type");
  }

  if ((nextByte & SLEB128SignMask) == SLEB128SignBit) {
    uint8_t rawKind;
    if (!d.readFixedU8(&rawKind)) {
      return false;
    }

    ComponentTypeKind primKind = ComponentTypeKind(rawKind);
    if (!ComponentTypeKindIsPrimitive(primKind)) {
      return d.failf("invalid value type 0x%02x", rawKind);
    }
    *t = ComponentType::primitive(primKind);
    return true;
  }

  int32_t typeIndex;
  if (!d.readVarS32(&typeIndex) || typeIndex < 0 ||
      c->types().length() <= size_t(typeIndex)) {
    return d.failf("invalid type index %d", typeIndex);
  }
  ComponentType referencedType = c->getType(typeIndex);
  if (!ComponentTypeKindIsValueType(referencedType.kind())) {
    return d.failf("type %d is not a value type", typeIndex);
  }
  *t = referencedType;
  return true;
}

enum class ComponentTypeKindRaw : uint8_t {
  Bool = 0x7f,
  S8 = 0x7e,
  U8 = 0x7d,
  S16 = 0x7c,
  U16 = 0x7b,
  S32 = 0x7a,
  U32 = 0x79,
  S64 = 0x78,
  U64 = 0x77,
  F32 = 0x76,
  F64 = 0x75,
  Char = 0x74,
  String = 0x73,

  Record = 0x72,
  Variant = 0x71,
  List = 0x70,
  Tuple = 0x6f,
  Flags = 0x6e,
  Enum = 0x6d,
  Option = 0x6b,
  Result = 0x6a,
  Own = 0x69,
  Borrow = 0x68,

  Func = 0x40,
  AsyncFunc = 0x43,

  Component = 0x41,
  Instance = 0x42,

  Resource = 0x3f,
};

[[nodiscard]] static bool DecodeComponentType(Decoder& d, MutableComponent& c) {
  uint8_t kind;
  if (!d.readFixedU8(&kind)) {
    return d.fail("expected type kind");
  }

  ComponentType t;
  switch (kind) {
    case uint8_t(ComponentTypeKindRaw::Bool):
    case uint8_t(ComponentTypeKindRaw::S8):
    case uint8_t(ComponentTypeKindRaw::U8):
    case uint8_t(ComponentTypeKindRaw::S16):
    case uint8_t(ComponentTypeKindRaw::U16):
    case uint8_t(ComponentTypeKindRaw::S32):
    case uint8_t(ComponentTypeKindRaw::U32):
    case uint8_t(ComponentTypeKindRaw::S64):
    case uint8_t(ComponentTypeKindRaw::U64):
    case uint8_t(ComponentTypeKindRaw::F32):
    case uint8_t(ComponentTypeKindRaw::F64):
    case uint8_t(ComponentTypeKindRaw::Char):
    case uint8_t(ComponentTypeKindRaw::String): {
      t = ComponentType::primitive(ComponentTypeKind(kind));
    } break;

    case uint8_t(ComponentTypeKindRaw::Record): {
      ComponentRecordFieldVector fields;

      // Record fields have the same name uniqueness requirement as imports.
      StronglyUniqueNameSet fieldNameDedup;

      uint32_t numFields;
      if (!d.readVarU32(&numFields)) {
        return d.fail("expected number of record fields");
      }
      if (numFields == 0) {
        return d.fail("records must have at least one field");
      }
      if (numFields > MaxComponentRecordFields) {
        return d.failf("too many record fields (max %d)",
                       MaxComponentRecordFields);
      }

      if (!fields.reserve(numFields)) {
        return false;
      }

      for (uint32_t i = 0; i < numFields; i++) {
        CacheableName name;
        if (!DecodeComponentNameNoAttributes(d, "record field", &name)) {
          return false;
        }
        ComponentType type;
        if (!DecodeComponentValType(d, c, &type)) {
          return false;
        }

        bool duplicate;
        if (!fieldNameDedup.add(name.utf8Bytes(), &duplicate)) {
          return false;
        }
        if (duplicate) {
          return d.failf("record field name \"%.*s\" is not strongly-unique",
                         ComponentName_Printf(name));
        }
        fields.infallibleEmplaceBack(std::move(name), type);
      }

      if (!ComponentType::record(std::move(fields), &t)) {
        return false;
      }
    } break;

    case uint8_t(ComponentTypeKindRaw::Variant): {
      ComponentVariantCaseVector cases;

      // Variant cases have the same name uniqueness requirement as imports.
      StronglyUniqueNameSet caseNameDedup;

      uint32_t numCases;
      if (!d.readVarU32(&numCases)) {
        return d.fail("expected number of variant cases");
      }
      if (numCases == 0) {
        return d.fail("variants must have at least one case");
      }
      if (numCases > MaxComponentVariantCases) {
        return d.failf("too many variant cases (max %d)",
                       MaxComponentVariantCases);
      }

      if (!cases.reserve(numCases)) {
        return false;
      }

      for (uint32_t i = 0; i < numCases; i++) {
        CacheableName name;
        bool duplicate;
        if (!DecodeComponentNameNoAttributes(d, "variant case", &name)) {
          return false;
        }
        if (!caseNameDedup.add(name.utf8Bytes(), &duplicate)) {
          return false;
        }
        if (duplicate) {
          return d.failf("variant case name \"%.*s\" is not strongly-unique",
                         ComponentName_Printf(name));
        }

        mozilla::Maybe<ComponentType> type;
        bool hasType;
        if (!d.readBool(&hasType)) {
          return d.fail("expected optional variant case type");
        }
        if (hasType) {
          ComponentType t;
          if (!DecodeComponentValType(d, c, &t)) {
            return false;
          }
          type = mozilla::Some(t);
        }

        uint8_t dummy;
        if (!d.readFixedU8(&dummy) || dummy != 0x00) {
          return d.fail("expected trailing zero on variant case");
        }

        cases.infallibleAppend(ComponentVariantCase{std::move(name), type});
      }

      if (!ComponentType::variant(std::move(cases), &t)) {
        return false;
      }
    } break;

    case uint8_t(ComponentTypeKindRaw::List): {
      ComponentType type;
      if (!DecodeComponentValType(d, c, &type)) {
        return false;
      }
      if (!ComponentType::list(std::move(type), &t)) {
        return false;
      }
    } break;

    case uint8_t(ComponentTypeKindRaw::Tuple): {
      uint32_t numTypes;
      if (!d.readVarU32(&numTypes)) {
        return d.fail("expected number of types in tuple");
      }
      if (numTypes == 0) {
        return d.fail("tuples must have at least one type");
      }
      if (numTypes > MaxComponentTupleTypes) {
        return d.failf("too many types in tuple (max %d)",
                       MaxComponentTupleTypes);
      }

      ComponentTypeVector types;
      if (!types.reserve(numTypes)) {
        return false;
      }
      for (uint32_t i = 0; i < numTypes; i++) {
        ComponentType type;
        if (!DecodeComponentValType(d, c, &type)) {
          return false;
        }
        types.infallibleAppend(type);
      }

      if (!ComponentType::tuple(std::move(types), &t)) {
        return false;
      }
    } break;

    case uint8_t(ComponentTypeKindRaw::Flags): {
      uint32_t numLabels;
      if (!d.readVarU32(&numLabels)) {
        return false;
      }
      if (numLabels == 0) {
        return d.fail("flag type must have at least one label");
      }
      if (numLabels > MaxComponentFlagLabels) {
        return d.fail("too many labels for flag type");
      }

      CacheableNameVector labels;
      StronglyUniqueNameSet labelDedup;
      if (!labels.reserve(numLabels)) {
        return false;
      }
      for (uint32_t i = 0; i < numLabels; i++) {
        CacheableName name;
        if (!DecodeComponentNameNoAttributes(d, "flag label", &name)) {
          return false;
        }
        bool duplicate;
        if (!labelDedup.add(name.utf8Bytes(), &duplicate)) {
          return false;
        }
        if (duplicate) {
          return d.failf("flag label \"%.*s\" is not strongly-unique",
                         ComponentName_Printf(name));
        }

        labels.infallibleAppend(std::move(name));
      }

      if (!ComponentType::flags(std::move(labels), &t)) {
        return false;
      }
    } break;

    case uint8_t(ComponentTypeKindRaw::Enum): {
      uint32_t numCases;
      if (!d.readVarU32(&numCases)) {
        return false;
      }
      if (numCases == 0) {
        return d.fail("enum must have at least one case");
      }
      if (numCases > MaxComponentEnumCases) {
        return d.failf("too many enum cases (max %d)", MaxComponentEnumCases);
      }

      CacheableNameVector labels;
      StronglyUniqueNameSet caseLabelDedup;
      if (!labels.reserve(numCases)) {
        return false;
      }
      for (uint32_t i = 0; i < numCases; i++) {
        CacheableName name;
        if (!DecodeComponentNameNoAttributes(d, "enum case", &name)) {
          return false;
        }
        bool duplicate;
        if (!caseLabelDedup.add(name.utf8Bytes(), &duplicate)) {
          return false;
        }
        if (duplicate) {
          return d.failf("enum case label \"%.*s\" is not strongly-unique",
                         ComponentName_Printf(name));
        }

        labels.infallibleAppend(std::move(name));
      }

      if (!ComponentType::enum_(std::move(labels), &t)) {
        return false;
      }
    } break;

    case uint8_t(ComponentTypeKindRaw::Option): {
      ComponentType type;
      if (!DecodeComponentValType(d, c, &type)) {
        return false;
      }
      if (!ComponentType::option(std::move(type), &t)) {
        return false;
      }
    } break;

    case uint8_t(ComponentTypeKindRaw::Result): {
      mozilla::Maybe<ComponentType> type;
      mozilla::Maybe<ComponentType> errorType;

      bool hasType;
      if (!d.readBool(&hasType)) {
        return d.fail("expected optional result type");
      }
      if (hasType) {
        ComponentType theType;
        if (!DecodeComponentValType(d, c, &theType)) {
          return false;
        }
        type = mozilla::Some(theType);
      }

      bool hasErrorType;
      if (!d.readBool(&hasErrorType)) {
        return d.fail("expected optional result error type");
      }
      if (hasErrorType) {
        ComponentType theErrorType;
        if (!DecodeComponentValType(d, c, &theErrorType)) {
          return false;
        }
        errorType = mozilla::Some(theErrorType);
      }

      if (!ComponentType::result(
              ComponentResultType{.type = type, .errorType = errorType}, &t)) {
        return false;
      }
    } break;

    case uint8_t(ComponentTypeKindRaw::Own):
    case uint8_t(ComponentTypeKindRaw::Borrow): {
      uint32_t typeIndex;
      if (!d.readVarU32(&typeIndex)) {
        return d.fail("expected resource type index");
      }

      if (c->types().length() <= typeIndex) {
        return d.failf("invalid type index %d", typeIndex);
      }
      ComponentType rt = c->getType(typeIndex);
      if (rt.kind() != ComponentTypeKind::Resource &&
          rt.kind() != ComponentTypeKind::SubResource) {
        return d.failf("type %d is not a resource type", typeIndex);
      }

      if (kind == uint8_t(ComponentTypeKindRaw::Own)) {
        if (!ComponentType::own(std::move(rt), &t)) {
          return false;
        }
      } else {
        if (!ComponentType::borrow(std::move(rt), &t)) {
          return false;
        }
      }
    } break;

    case uint8_t(ComponentTypeKindRaw::Func): {
      ComponentFuncType ft;

      uint32_t numParams;
      StronglyUniqueNameSet paramDeduper;
      if (!d.readVarU32(&numParams)) {
        return d.fail("expected number of params");
      }
      if (numParams > MaxComponentParams) {
        return d.failf("too many params (max %d)", MaxComponentParams);
      }
      if (!ft.paramTypes.reserve(numParams) ||
          !ft.paramNames.reserve(numParams)) {
        return false;
      }

      for (uint32_t i = 0; i < numParams; i++) {
        CacheableName name;
        if (!DecodeComponentNameNoAttributes(d, "param", &name)) {
          return false;
        }
        ComponentType type;
        if (!DecodeComponentValType(d, c, &type)) {
          return false;
        }

        bool duplicate;
        if (!paramDeduper.add(name.utf8Bytes(), &duplicate)) {
          return false;
        }
        if (duplicate) {
          return d.failf("param name \"%.*s\" is not strongly-unique",
                         ComponentName_Printf(name));
        }

        ft.paramNames.infallibleAppend(std::move(name));
        ft.paramTypes.infallibleAppend(std::move(type));
      }

      // There is a result type if the byte is zero. It is not clear why this
      // is, but we can only hope it is fixed when the binary format is
      // eventually reshuffled.
      bool hasNoResultType;
      if (!d.readBool(&hasNoResultType)) {
        return d.fail("expected result type");
      }
      if (hasNoResultType) {
        uint8_t dummy;
        if (!d.readFixedU8(&dummy) || dummy != 0) {
          return d.fail("expected result type");
        }
      } else {
        ComponentType resultType;
        if (!DecodeComponentValType(d, c, &resultType)) {
          return false;
        }
        ft.resultType = mozilla::Some(resultType);
      }

      if (!ComponentType::func(std::move(ft), &t)) {
        return false;
      }
    } break;
    case uint8_t(ComponentTypeKindRaw::AsyncFunc):
      return d.fail("async functions are not supported");

    case uint8_t(ComponentTypeKindRaw::Resource): {
      uint8_t repType;
      if (!d.readFixedU8(&repType)) {
        return d.fail("expected rep type for resource type");
      }

      // Require (rep i32)
      // TODO(wasm-cm): Update for memory64 support
      if (repType != 0x7f) {
        return d.failf("unexpected rep type 0x%02x for resource type", repType);
      }

      uint8_t hasDtor;
      mozilla::Maybe<uint32_t> dtorIndex;
      if (!d.readFixedU8(&hasDtor) || hasDtor > 0x01) {
        return d.fail("expected destructor for resource type");
      }
      if (hasDtor) {
        uint32_t dtorIndexRaw;
        if (!d.readVarU32(&dtorIndexRaw)) {
          return d.fail("expected index of destructor for resource type");
        }

        if (c->coreFuncs().length() <= dtorIndexRaw) {
          return d.failf("invalid core func index %d", dtorIndexRaw);
        }
        const FuncType& dtorType =
            c->getTypeForCoreFunc(dtorIndexRaw).funcType();

        if (!dtorType.isValidComponentDestructor()) {
          return d.fail("destructor has invalid signature");
        }

        dtorIndex.emplace(dtorIndexRaw);
      }

      if (!ComponentType::resource(ComponentResourceType(dtorIndex), &t)) {
        return false;
      }
    } break;

    default:
      return d.failf("unexpected type 0x%02x", kind);
  }

  ComponentType canonical;
  if (!CanonicalizeComponentType(t, &canonical)) {
    return false;
  }
  if (!c->addType(std::move(canonical))) {
    return false;
  }

  return true;
}

enum class ComponentSortRaw : uint8_t {
  CoreSort = 0x00,
  Function = 0x01,
  Type = 0x03,
  Component = 0x04,
  Instance = 0x05,
};

enum class ComponentCoreSortRaw : uint8_t {
  Function = 0x00,
  Table = 0x01,
  Memory = 0x02,
  Global = 0x03,
  Tag = 0x04,
  Type = 0x10,
  Module = 0x11,
  Instance = 0x12,
};

[[nodiscard]] static bool DecodeComponentCoreSort(Decoder& d,
                                                  ComponentSort* sort) {
  uint8_t coreSort;
  if (!d.readFixedU8(&coreSort)) {
    return d.fail("expected core sort");
  }

  switch (coreSort) {
    case uint8_t(ComponentCoreSortRaw::Function): {
      *sort = ComponentSort::CoreFunction;
    } break;
    case uint8_t(ComponentCoreSortRaw::Table): {
      *sort = ComponentSort::CoreTable;
    } break;
    case uint8_t(ComponentCoreSortRaw::Memory): {
      *sort = ComponentSort::CoreMemory;
    } break;
    case uint8_t(ComponentCoreSortRaw::Global): {
      *sort = ComponentSort::CoreGlobal;
    } break;
    case uint8_t(ComponentCoreSortRaw::Tag): {
      *sort = ComponentSort::CoreTag;
    } break;
    case uint8_t(ComponentCoreSortRaw::Type): {
      *sort = ComponentSort::CoreType;
    } break;
    case uint8_t(ComponentCoreSortRaw::Module): {
      *sort = ComponentSort::CoreModule;
    } break;
    case uint8_t(ComponentCoreSortRaw::Instance): {
      *sort = ComponentSort::CoreInstance;
    } break;
    default:
      return d.failf("unexpected core externtype %d", coreSort);
  }

  return true;
}

[[nodiscard]] static bool DecodeComponentSort(Decoder& d, ComponentSort* sort,
                                              bool forExterndesc) {
  uint8_t kind;
  if (!d.readFixedU8(&kind)) {
    return d.fail("expected sort");
  }

  switch (kind) {
    case uint8_t(ComponentSortRaw::CoreSort): {
      if (!DecodeComponentCoreSort(d, sort)) {
        return false;
      }
    } break;
    case uint8_t(ComponentSortRaw::Function): {
      *sort = ComponentSort::Func;
    } break;
    case uint8_t(ComponentSortRaw::Type): {
      *sort = ComponentSort::Type;
    } break;
    case uint8_t(ComponentSortRaw::Component): {
      *sort = ComponentSort::Component;
    } break;
    case uint8_t(ComponentSortRaw::Instance): {
      *sort = ComponentSort::Instance;
    } break;
    default:
      return d.failf("unexpected sort 0x%02x", kind);
  }

  if (forExterndesc && !ComponentSortValidForExternDesc(*sort)) {
    return d.failf("unexpected sort 0x%02x", kind);
  }

  return true;
}

enum class ComponentTypeBoundKindRaw : uint8_t {
  Eq = 0x00,
  SubResource = 0x01,
};

[[nodiscard]] static bool DecodeComponentExternDesc(Decoder& d,
                                                    MutableComponent c,
                                                    ComponentExternDesc* desc,
                                                    bool* isNewSubResource) {
  *isNewSubResource = false;

  ComponentSort kind;
  if (!DecodeComponentSort(d, &kind, /*forExterndesc=*/true)) {
    return false;
  }

  switch (kind) {
    case ComponentSort::Func: {
      uint32_t funcTypeIndex;
      if (!d.readVarU32(&funcTypeIndex)) {
        return d.fail("expected func type index");
      }

      if (c->types().length() <= funcTypeIndex) {
        return d.failf("invalid type index %d", funcTypeIndex);
      }
      ComponentType funcType = c->getType(funcTypeIndex);
      if (funcType.kind() != ComponentTypeKind::Func) {
        return d.failf("type %d is not a func type", funcTypeIndex);
      }

      *desc = ComponentExternDesc::func(std::move(funcType));
    } break;
    case ComponentSort::Type: {
      uint8_t kind;
      if (!d.readFixedU8(&kind)) {
        return d.fail("expected kind of type bound");
      }

      switch (kind) {
        case uint8_t(ComponentTypeBoundKindRaw::Eq): {
          uint32_t typeIndex;
          if (!d.readVarU32(&typeIndex)) {
            return d.fail("expected type index");
          }

          if (c->types().length() <= typeIndex) {
            return d.failf("invalid type index %d", typeIndex);
          }

          *desc = ComponentExternDesc::type(c->getType(typeIndex));
        } break;
        case uint8_t(ComponentTypeBoundKindRaw::SubResource): {
          // We do not need to canonicalize this new type, as all resource types
          // are unique anyway.
          ComponentType subResourceType;
          if (!ComponentType::subResource(&subResourceType)) {
            return false;
          }
          *desc = ComponentExternDesc::type(std::move(subResourceType));
          *isNewSubResource = true;
        } break;
        default:
          return d.failf("invalid kind 0x%02x for type bound", kind);
      }
    } break;
    case ComponentSort::Component: {
      // TODO(wasm-cm): Add support for these
      return d.fail("extern components are not supported yet");
    } break;
    case ComponentSort::Instance: {
      // TODO(wasm-cm): Add support for these
      return d.fail("extern instances are not supported yet");
    } break;
    case ComponentSort::CoreModule: {
      // TODO(wasm-cm): Add support for these
      return d.fail("extern core modules are not supported yet");
    } break;
    default:
      MOZ_CRASH(
          "all externdesc-compatible ComponentSorts should have been handled");
  }

  return true;
}

static const char* DefinitionKindName(DefinitionKind kind) {
  switch (kind) {
    case DefinitionKind::Function:
      return "function";
    case DefinitionKind::Table:
      return "table";
    case DefinitionKind::Memory:
      return "memory";
    case DefinitionKind::Global:
      return "global";
    case DefinitionKind::Tag:
      return "tag";
    default:
      MOZ_CRASH();
  }
}

// The name of a core item in errors, distinguishing core functions from
// component functions.
static const char* CoreItemName(DefinitionKind kind) {
  return kind == DefinitionKind::Function ? "core function"
                                          : DefinitionKindName(kind);
}

// Validates that a `(core:)?sortidx` from the component spec corresponds to a
// valid item in the component's index space.
[[nodiscard]] static bool ValidateComponentSortIdx(
    Decoder& d, const Component& c, ComponentSortIndex sortIndex) {
  switch (sortIndex.sort) {
    case ComponentSort::Func: {
      if (c.funcs().length() <= sortIndex.index) {
        return d.failf("invalid function index %d", sortIndex.index);
      }
    } break;
    case ComponentSort::Type: {
      if (c.types().length() <= sortIndex.index) {
        return d.failf("invalid type index %d", sortIndex.index);
      }
    } break;
    case ComponentSort::Component:
      // TODO(wasm-cm): Support nested components
      return d.fail("nested components are not supported yet");
    case ComponentSort::Instance:
      // TODO(wasm-cm): Support nested components
      return d.fail("nested components are not supported yet");

    case ComponentSort::CoreFunction:
    case ComponentSort::CoreTable:
    case ComponentSort::CoreMemory:
    case ComponentSort::CoreGlobal:
    case ComponentSort::CoreTag: {
      if (c.coreIndexSpace(sortIndex.sort).length() <= sortIndex.index) {
        return d.failf("invalid %s index %d",
                       CoreItemName(CoreSortFromComponentSort(sortIndex.sort)),
                       sortIndex.index);
      }
    } break;

    case ComponentSort::CoreType:
      // TODO(wasm-cm): Support core type aliases as part of outer aliases.
      return d.fail("core type aliases are not supported yet");
    case ComponentSort::CoreModule: {
      if (c.coreModules().length() <= sortIndex.index) {
        return d.failf("invalid core module index %d", sortIndex.index);
      }
    } break;
    case ComponentSort::CoreInstance: {
      if (c.coreInstances().length() <= sortIndex.index) {
        return d.failf("invalid core instance index %d", sortIndex.index);
      }
    } break;

    default:
      MOZ_CRASH();
  }

  return true;
}

enum class CoreInstanceExprKind : uint8_t {
  InstantiateModule = 0x00,
  InlineExports = 0x01,
};

[[nodiscard]] static bool ValidateInstantiateArgs(
    Decoder& d, const Component& c, const CoreModuleType& moduleType,
    const CoreInstanceInstantiateArgs& args) {
  for (const CoreImportType& imp : moduleType.imports()) {
    auto p = args.lookup(imp.module.utf8Bytes());
    if (!p) {
      return d.fail("no instantiate arg found");
    }
    mozilla::Maybe<const CoreExternType&> exportType =
        c.getCoreInstance(p->value()).type().getExport(imp.field);
    if (!exportType) {
      return d.fail("no matching export for core module import");
    }
    if (!CoreExternType::matches(*exportType, imp.type)) {
      return d.failf("incompatible %s type for import",
                     DefinitionKindName(imp.type.kind()));
    }
  }
  return true;
}

// Checks that there is room for one more core func, table, memory, global, or
// tag in the component's index space for `sort`.
[[nodiscard]] static bool CheckCanAddCoreItem(Decoder& d, const Component& c,
                                              ComponentSort sort) {
  uint32_t max;
  const char* items;
  switch (sort) {
    case ComponentSort::CoreFunction:
      max = MaxComponentCoreFuncs;
      items = "funcs";
      break;
    case ComponentSort::CoreTable:
      max = MaxComponentCoreTables;
      items = "tables";
      break;
    case ComponentSort::CoreMemory:
      max = MaxComponentCoreMemories;
      items = "memories";
      break;
    case ComponentSort::CoreGlobal:
      max = MaxComponentCoreGlobals;
      items = "globals";
      break;
    case ComponentSort::CoreTag:
      max = MaxComponentCoreTags;
      items = "tags";
      break;
    default:
      MOZ_CRASH();
  }
  if (c.coreIndexSpace(sort).length() >= max) {
    return d.failf("too many core %s (max %u)", items, max);
  }
  return true;
}

[[nodiscard]] static bool DecodeCoreInstance(Decoder& d, MutableComponent& c) {
  uint8_t exprType;
  if (!d.readFixedU8(&exprType)) {
    return false;
  }

  switch (exprType) {
    case uint8_t(CoreInstanceExprKind::InstantiateModule): {
      uint32_t moduleIndex;
      if (!d.readVarU32(&moduleIndex)) {
        return d.fail("expected core module index");
      }
      if (moduleIndex >= c->coreModules().length()) {
        return d.failf("invalid core module index %d", moduleIndex);
      }

      uint32_t numArgs;
      if (!d.readVarU32(&numArgs)) {
        return d.fail("expected number of instantiate arguments");
      }
      if (numArgs > MaxComponentCoreInstantiateArgs) {
        return d.failf("too many core instantiate args (max %d)",
                       MaxComponentCoreInstantiateArgs);
      }

      CoreInstanceInstantiateArgs args;
      for (uint32_t i = 0; i < numArgs; i++) {
        CacheableName importName;
        if (!DecodeName(d, &importName)) {
          return d.fail("expected import name");
        }

        auto p = args.lookupForAdd(importName.utf8Bytes());
        if (p) {
          return d.fail("duplicate core instantiate arg name");
        }

        uint8_t instanceIndicator;
        if (!d.readFixedU8(&instanceIndicator) ||
            instanceIndicator != uint8_t(ComponentCoreSortRaw::Instance)) {
          return d.fail("expected core instance index");
        }

        uint32_t instanceIndex;
        if (!d.readVarU32(&instanceIndex)) {
          return d.fail("expected core instance index");
        }
        if (c->coreInstances().length() <= instanceIndex) {
          return d.failf("invalid core instance index %d", instanceIndex);
        }

        if (!args.add(p, std::move(importName), instanceIndex)) {
          return false;
        }
      }

      // Validate that the instantiate args satisfy the module's imports. (How
      // wonderful it is that we can do this statically for components!)
      const CoreModuleType& moduleType = c->getCoreModuleType(moduleIndex);
      if (!ValidateInstantiateArgs(d, *c, moduleType, args)) {
        return false;
      }

      CoreInstanceDesc desc(
          CoreInstanceDescFromModule{
              .moduleIndex = moduleIndex,
              .args = std::move(args),
          },
          moduleType.instanceType());
      if (!c->addCoreInstance(std::move(desc))) {
        return false;
      }
    } break;
    case uint8_t(CoreInstanceExprKind::InlineExports): {
      uint32_t numExports;
      if (!d.readVarU32(&numExports)) {
        return d.fail("expected number of inline exports");
      }
      if (numExports > MaxComponentInlineExports) {
        return d.failf("too many inline exports (max %d)",
                       MaxComponentInlineExports);
      }

      ComponentInlineExports inlineExports;
      MutableCoreInstanceType instanceType = js_new<CoreInstanceType>();
      if (!instanceType) {
        return false;
      }
      for (uint32_t i = 0; i < numExports; i++) {
        CacheableName name;
        ComponentSortIndex sortIndex;
        {
          ComponentSort sort;
          uint32_t index;
          if (!DecodeName(d, &name)) {
            return d.fail("expected inline export name");
          }
          if (!DecodeComponentCoreSort(d, &sort)) {
            return false;
          }
          if (!d.readVarU32(&index)) {
            return d.fail("expected inline export item index");
          }
          sortIndex = ComponentSortIndex(sort, index);
        }

        if (instanceType->getExport(name)) {
          return d.fail("duplicate name of inline export");
        }

        // Component-level sorts like Func and CoreModule are not allowed for
        // core instances.
        if (!ComponentSortIsCoreSort(sortIndex.sort)) {
          return d.failf("invalid sort %d for core inline export",
                         uint8_t(sortIndex.sort));
        }

        // Check that index refers to a valid item
        if (!ValidateComponentSortIdx(d, *c, sortIndex)) {
          return false;
        }

        CacheableName typeName;
        if (!name.clone(&typeName) ||
            !instanceType->addExport(std::move(typeName),
                                     c->getCoreItemType(sortIndex)) ||
            !inlineExports.addExport(std::move(name), sortIndex)) {
          return false;
        }
      }

      if (!c->addCoreInstance(CoreInstanceDesc(std::move(inlineExports),
                                               std::move(instanceType)))) {
        return false;
      }
    } break;
    default:
      return d.failf("expected type of instance expression but got %d",
                     exprType);
  }

  return true;
}

enum class AliasKindRaw : uint8_t {
  ComponentExport = 0x00,
  CoreExport = 0x01,
  Outer = 0x02,
};

[[nodiscard]] static bool DecodeComponentAlias(Decoder& d,
                                               MutableComponent& c) {
  ComponentSort sort;
  if (!DecodeComponentSort(d, &sort, /*forExterndesc=*/false)) {
    return false;
  }

  uint8_t targetType;
  if (!d.readFixedU8(&targetType)) {
    return d.fail("expected alias target");
  }

  switch (targetType) {
    case uint8_t(AliasKindRaw::ComponentExport): {
      // TODO(wasm-cm)
      return d.fail("component export aliases are not yet supported");
    } break;
    case uint8_t(AliasKindRaw::CoreExport): {
      uint32_t instanceIndex;
      if (!d.readVarU32(&instanceIndex)) {
        return d.fail("expected instance index");
      }

      CacheableName exportName;
      if (!DecodeName(d, &exportName)) {
        return d.fail("expected instance export name");
      }

      if (c->coreInstances().length() <= instanceIndex) {
        return d.failf("invalid core instance index %d", instanceIndex);
      }
      mozilla::Maybe<const CoreExternType&> aliasType =
          c->getCoreInstance(instanceIndex).type().getExport(exportName);
      if (aliasType.isNothing()) {
        return d.failf("core instance %d has no export \"%.*s\"", instanceIndex,
                       ComponentName_Printf(exportName));
      }
      if (!ComponentSortIsCoreSort(sort)) {
        return d.failf("invalid alias sort 0x%02x", uint8_t(sort));
      }
      if (!CheckCanAddCoreItem(d, *c, sort)) {
        return false;
      }
      DefinitionKind kind = CoreSortFromComponentSort(sort);
      if (aliasType->kind() != kind) {
        return d.failf("export \"%.*s\" of core instance %d is not a %s",
                       ComponentName_Printf(exportName), instanceIndex,
                       DefinitionKindName(kind));
      }

      if (!c->addCoreExportAlias(sort, instanceIndex, std::move(exportName))) {
        return false;
      }
    } break;
    case uint8_t(AliasKindRaw::Outer): {
      // TODO(wasm-cm): Support 0-depth outer aliases
      // TODO(wasm-cm): Support all depths of outer aliases once nested
      // components are supported
      return d.fail("outer aliases are not yet supported");
    } break;
    default:
      return d.failf("unexpected alias target 0x%02x", targetType);
  }

  return true;
}

enum class CanonOptKindRaw : uint8_t {
  StringEncodingUTF8 = uint8_t(ComponentStringEncoding::UTF8),
  StringEncodingUTF16 = uint8_t(ComponentStringEncoding::UTF16),
  StringEncodingLatin1PlusUTF16 =
      uint8_t(ComponentStringEncoding::Latin1PlusUTF16),
  Memory = 0x03,
  Realloc = 0x04,
  PostReturn = 0x05,
};

// Decodes a list of canonopts in the binary format.
[[nodiscard]] static bool DecodeCanonOpts(Decoder& d, const Component& c,
                                          ComponentCanonOpts* opts,
                                          CanonMode mode) {
  *opts = {};

  uint32_t count;
  if (!d.readVarU32(&count)) {
    return d.fail("expected number of canonopts");
  }

  // TODO(wasm-cm): Do you need max canonopts when each kind of canonopt can
  // only be set once?
  if (count > MaxComponentCanonOpts) {
    return d.failf("too many canonopts (max %d)", MaxComponentCanonOpts);
  }

  bool hasStringEncoding = false;
  bool hasMemory = false;
  bool hasRealloc = false;
  bool hasPostReturn = false;
  for (uint32_t i = 0; i < count; i++) {
    uint8_t kind;
    if (!d.readFixedU8(&kind)) {
      return d.fail("expected canonopt");
    }
    switch (kind) {
      case uint8_t(CanonOptKindRaw::StringEncodingUTF8):
      case uint8_t(CanonOptKindRaw::StringEncodingUTF16):
      case uint8_t(CanonOptKindRaw::StringEncodingLatin1PlusUTF16): {
        if (hasStringEncoding) {
          return d.fail("string encoding already specified");
        }
        hasStringEncoding = true;
        opts->stringEncoding = ComponentStringEncoding(kind);
      } break;
      case uint8_t(CanonOptKindRaw::Memory): {
        if (hasMemory) {
          return d.fail("memory already specified");
        }
        hasMemory = true;
        uint32_t memoryIndex;
        if (!d.readVarU32(&memoryIndex)) {
          return d.fail("expected memory index");
        }
        if (c.coreMemories().length() <= memoryIndex) {
          return d.failf("invalid memory index %d", memoryIndex);
        }
        if (c.getCoreItemType(
                 ComponentSortIndex(ComponentSort::CoreMemory, memoryIndex))
                .asMemory()
                .addressType != AddressType::I32) {
          return d.fail("memory for canonical ABI must be 32-bit");
        }
        opts->memoryIndex.emplace(memoryIndex);
      } break;
      case uint8_t(CanonOptKindRaw::Realloc): {
        if (hasRealloc) {
          return d.fail("realloc already specified");
        }
        hasRealloc = true;
        uint32_t reallocIndex;
        if (!d.readVarU32(&reallocIndex)) {
          return d.fail("expected realloc index");
        }
        if (c.coreFuncs().length() <= reallocIndex) {
          return d.failf("invalid index %d for realloc function", reallocIndex);
        }
        const FuncType& reallocType =
            c.getTypeForCoreFunc(reallocIndex).funcType();
        if (reallocType.args().length() != 4 ||
            reallocType.args()[0] != ValType::i32() ||
            reallocType.args()[1] != ValType::i32() ||
            reallocType.args()[2] != ValType::i32() ||
            reallocType.args()[3] != ValType::i32() ||
            reallocType.results().length() != 1 ||
            reallocType.results()[0] != ValType::i32()) {
          return d.fail("invalid signature for realloc function");
        }
        opts->reallocIndex.emplace(reallocIndex);
      } break;
      case uint8_t(CanonOptKindRaw::PostReturn): {
        if (mode != CanonMode::Lift) {
          return d.fail("post-return only valid for canon lift");
        }
        if (hasPostReturn) {
          return d.fail("post-return already specified");
        }
        hasPostReturn = true;
        uint32_t postReturnIndex;
        if (!d.readVarU32(&postReturnIndex)) {
          return d.fail("expected post-return index");
        }
        if (c.coreFuncs().length() <= postReturnIndex) {
          return d.failf("invalid index %d for post-return function",
                         postReturnIndex);
        }
        opts->postReturnIndex.emplace(postReturnIndex);
      } break;
      default:
        return d.failf("unexpected canonopt 0x%02x", kind);
    }
  }

  return true;
}

enum class CanonDefKindRaw : uint8_t {
  Lift = 0x00,
  Lower = 0x01,
  ResourceNew = 0x02,
  ResourceDrop = 0x03,
  ResourceRep = 0x04,
};

[[nodiscard]] static bool DecodeComponentCanonDef(Decoder& d,
                                                  MutableComponent& c) {
  uint8_t kind;
  if (!d.readFixedU8(&kind)) {
    return d.fail("expected canonical definition");
  }

  switch (kind) {
    case uint8_t(CanonDefKindRaw::Lift): {
      if (c->funcs().length() >= MaxComponentFuncs) {
        return d.failf("too many funcs (max %d)", MaxComponentFuncs);
      }

      uint8_t dummy;
      if (!d.readFixedU8(&dummy) || dummy != 0) {
        return d.fail("expected canonical definition");
      }

      uint32_t coreFuncIndex;
      if (!d.readVarU32(&coreFuncIndex)) {
        return d.fail("expected core function index");
      }
      if (c->coreFuncs().length() <= coreFuncIndex) {
        return d.failf("invalid core function index %d", coreFuncIndex);
      }

      ComponentCanonOpts opts;
      if (!DecodeCanonOpts(d, *c, &opts, CanonMode::Lift)) {
        return false;
      }

      uint32_t typeIndex;
      if (!d.readVarU32(&typeIndex)) {
        return d.fail("expected type index");
      }
      if (c->types().length() <= typeIndex) {
        return d.failf("invalid type index %d", typeIndex);
      }

      const ComponentType& t = c->getType(typeIndex);
      if (t.kind() != ComponentTypeKind::Func) {
        return d.fail("canon lift requires a func type");
      }

      const ComponentFuncType& ft = t.asFunc();
      bool memoryRequired = false;
      bool reallocRequired = false;
      bool tooDeep = false;
      mozilla::Maybe<FuncType> flattened = FlattenFuncType(
          ft, CanonMode::Lift, &memoryRequired, &reallocRequired, &tooDeep);
      if (flattened.isNothing()) {
        if (tooDeep) {
          return d.fail("flattening exceeded maximum depth");
        }
        return false;
      }

      // Because flattened func types use only primitive types, there will never
      // be any type references and a strict comparison will suffice.
      if (!FuncType::strictlyEquals(
              flattened.ref(),
              c->getTypeForCoreFunc(coreFuncIndex).funcType())) {
        return d.fail(
            "could not lift core func (component func type did not match)");
      }

      // Verify presence of (memory) and (realloc) if required.
      if (memoryRequired && opts.memoryIndex.isNothing()) {
        return d.fail("memory required for canon lift");
      }
      if (reallocRequired && opts.reallocIndex.isNothing()) {
        return d.fail("realloc required for canon lift");
      }

      // Verify post-return signature, if present. The post-return function is
      // required to have params matching the actual function's results (after
      // flattening).
      if (opts.postReturnIndex.isSome()) {
        const FuncType& postReturnType =
            c->getTypeForCoreFunc(*opts.postReturnIndex).funcType();
        if (postReturnType.args().length() != flattened->results().length() ||
            postReturnType.results().length() != 0) {
          return d.fail("invalid signature for post-return function");
        }
        for (size_t i = 0; i < postReturnType.args().length(); i++) {
          if (postReturnType.args()[i] != flattened->results()[i]) {
            return d.fail("invalid signature for post-return function");
          }
        }
      }

      if (!c->addFunc(ComponentLiftedFuncDesc(typeIndex, std::move(opts)))) {
        return false;
      }
    } break;
    case uint8_t(CanonDefKindRaw::Lower): {
      if (!CheckCanAddCoreItem(d, *c, ComponentSort::CoreFunction)) {
        return false;
      }

      uint8_t dummy;
      if (!d.readFixedU8(&dummy) || dummy != 0) {
        return d.fail("expected canonical definition");
      }

      uint32_t funcIndex;
      if (!d.readVarU32(&funcIndex)) {
        return d.fail("expected function index");
      }
      if (c->funcs().length() <= funcIndex) {
        return d.failf("invalid function index %d", funcIndex);
      }

      ComponentCanonOpts opts;
      if (!DecodeCanonOpts(d, *c, &opts, CanonMode::Lower)) {
        return false;
      }

      const ComponentFuncType& ft = c->getTypeForFunc(funcIndex).asFunc();
      bool memoryRequired = false;
      bool reallocRequired = false;
      bool tooDeep = false;
      mozilla::Maybe<FuncType> flattened = FlattenFuncType(
          ft, CanonMode::Lower, &memoryRequired, &reallocRequired, &tooDeep);
      if (flattened.isNothing()) {
        if (tooDeep) {
          return d.fail("flattening exceeded maximum depth");
        }
        return false;
      }
      SharedTypeDef flattenedCanonical =
          TypeContext::canonicalizeSingleType(flattened.extract());
      if (!flattenedCanonical) {
        return false;
      }

      // Verify presence of (memory) and (realloc) if required.
      if (memoryRequired && opts.memoryIndex.isNothing()) {
        return d.fail("memory required for canon lower");
      }
      if (reallocRequired && opts.reallocIndex.isNothing()) {
        return d.fail("realloc required for canon lower");
      }

      if (!c->addDefinedCoreFunc(ComponentCoreFuncDesc::lowered(
              funcIndex, std::move(flattenedCanonical)))) {
        return false;
      }
    } break;
    case uint8_t(CanonDefKindRaw::ResourceNew):
    case uint8_t(CanonDefKindRaw::ResourceDrop):
    case uint8_t(CanonDefKindRaw::ResourceRep): {
      if (!CheckCanAddCoreItem(d, *c, ComponentSort::CoreFunction)) {
        return false;
      }

      uint32_t resourceTypeIndex;
      if (!d.readVarU32(&resourceTypeIndex)) {
        return d.fail("expected resource type index");
      }
      if (c->types().length() <= resourceTypeIndex) {
        return d.failf("invalid type index %d", resourceTypeIndex);
      }

      ComponentItem resourceTypeItem = c->types()[resourceTypeIndex];
      ComponentType resourceType = c->getType(resourceTypeIndex);
      switch (kind) {
        case uint8_t(CanonDefKindRaw::ResourceNew):
        case uint8_t(CanonDefKindRaw::ResourceRep): {
          // resource.new and resource.rep require a resource type defined in
          // this component.
          if (resourceTypeItem.kind() != ComponentItem::ItemKind::Defined) {
            return d.fail("expected a defined resource type");
          }
          if (resourceType.kind() != ComponentTypeKind::Resource) {
            return d.fail("expected a resource type");
          }
        } break;
        case uint8_t(CanonDefKindRaw::ResourceDrop): {
          // resource.drop allows any resource type (including imported).
          if (resourceType.kind() != ComponentTypeKind::Resource &&
              resourceType.kind() != ComponentTypeKind::SubResource) {
            return d.fail("expected a resource type");
          }
        } break;
      }

      // The values for the Kind enum are chosen to align with the binary
      // format.
      ComponentResourceBuiltin builtin(ComponentResourceBuiltin::Kind(kind),
                                       resourceType);

      ValTypeVector args;
      ValTypeVector results;
      switch (builtin.kind()) {
        case ComponentResourceBuiltin::Kind::ResourceNew: {
          // TODO(wasm-cm): Update for memory64 support
          if (!args.append(ValType::i32())) {
            return false;
          }
          if (!results.append(ValType::i32())) {
            return false;
          }
        } break;
        case ComponentResourceBuiltin::Kind::ResourceDrop: {
          if (!args.append(ValType::i32())) {
            return false;
          }
        } break;
        case ComponentResourceBuiltin::Kind::ResourceRep: {
          if (!args.append(ValType::i32())) {
            return false;
          }
          // TODO(wasm-cm): Update for memory64 support
          if (!results.append(ValType::i32())) {
            return false;
          }
        } break;
        default:
          MOZ_CRASH();
      }
      SharedTypeDef coreFuncType = TypeContext::canonicalizeSingleType(
          FuncType(std::move(args), std::move(results)));

      if (!c->addDefinedCoreFunc(ComponentCoreFuncDesc::builtin(
              std::move(builtin), std::move(coreFuncType)))) {
        return false;
      }
    } break;
    default:
      return d.failf("unexpected canonical definition kind 0x%02x", kind);
  }

  return true;
}

// [constructor], [method], and [static] must reference a preceding resource
// type in their name, and [constructor] and [method] have additional
// restrictions on results/params.
template <typename HasFunc, typename GetFunc>
static bool ValidateResourceFuncs(Decoder& d, const ComponentName& name,
                                  const ComponentFuncType& funcType,
                                  const HasFunc& hasItemWithName,
                                  const GetFunc& getItemByName) {
  if (!name.attributes.contains(ComponentNameAttribute::Constructor) &&
      !name.attributes.contains(ComponentNameAttribute::Method) &&
      !name.attributes.contains(ComponentNameAttribute::Static)) {
    return true;
  }

  mozilla::Span<const char> resourceName = name.resourceName();

  bool resourceOk = false;
  ComponentType resourceType;
  if (hasItemWithName(resourceName)) {
    const ComponentExternDesc& resourceTypeDesc = getItemByName(resourceName);
    if (resourceTypeDesc.sort() == ComponentSort::Type &&
        resourceTypeDesc.asType().isAnyResource()) {
      resourceOk = true;
      resourceType = resourceTypeDesc.asType();
    }
  }
  if (!resourceOk) {
    return d.failf(
        "no preceding resource type named \"%.*s\" for func \"%.*s\"",
        ComponentNameSpan_Printf(resourceName),
        ComponentName_Printf(name.name));
  }
  MOZ_ASSERT(resourceType.isValid());

  if (name.attributes.contains(ComponentNameAttribute::Constructor)) {
    bool resultOk = false;
    if (funcType.resultType.isSome()) {
      bool isOwn = funcType.resultType->kind() == ComponentTypeKind::Own &&
                   funcType.resultType->asOwn() == resourceType;
      bool isResultOwn =
          funcType.resultType->kind() == ComponentTypeKind::Result &&
          funcType.resultType->asResult().type.isSome() &&
          funcType.resultType->asResult().type->kind() ==
              ComponentTypeKind::Own &&
          funcType.resultType->asResult().type->asOwn() == resourceType;
      if (isOwn || isResultOwn) {
        resultOk = true;
      }
    }
    if (!resultOk) {
      return d.failf("constructor \"%.*s\" must return (own %.*s)",
                     ComponentName_Printf(name.name),
                     ComponentNameSpan_Printf(resourceName));
    }
  } else if (name.attributes.contains(ComponentNameAttribute::Method)) {
    if (funcType.paramTypes.length() == 0 ||
        funcType.paramNames[0].utf8Bytes() !=
            mozilla::Span<const char>("self", strlen("self")) ||
        funcType.paramTypes[0].kind() != ComponentTypeKind::Borrow ||
        funcType.paramTypes[0].asBorrow() != resourceType) {
      return d.failf(
          "method \"%.*s\" must have a first parameter (param \"self\" "
          "(borrow %.*s))",
          ComponentName_Printf(name.name),
          ComponentNameSpan_Printf(resourceName));
    }
  }

  return true;
}

// [get] and [set] have restrictions on the number and type of their
// params/results.
template <typename HasFunc, typename GetFunc>
static bool ValidateAccessors(Decoder& d, const ComponentName& name,
                              const ComponentFuncType& funcType,
                              const HasFunc& hasItemWithName,
                              const GetFunc& getItemByName) {
  bool isMethod = name.attributes.contains(ComponentNameAttribute::Method);
  if (name.attributes.contains(ComponentNameAttribute::Get)) {
    // [get] must have no parameters (except "self" for [method])
    if (funcType.paramTypes.length() != (isMethod ? 1 : 0)) {
      return d.failf("getter \"%.*s\" must have no parameters%s",
                     ComponentName_Printf(name.name),
                     isMethod ? " besides self" : "");
    }
    // [get] must return something, and if it returns result<T> then it must
    // have an inner type
    if (funcType.resultType.isNothing() ||
        (funcType.resultType->kind() == ComponentTypeKind::Result &&
         funcType.resultType->asResult().type.isNothing())) {
      return d.failf("getter \"%.*s\" must return a value",
                     ComponentName_Printf(name.name));
    }
  } else if (name.attributes.contains(ComponentNameAttribute::Set)) {
    // [set] must be preceded by a matching getter
    CacheableName getterName;
    if (!name.getterForSetter(&getterName)) {
      return false;
    }
    if (!hasItemWithName(getterName.utf8Bytes())) {
      return d.failf("setter \"%.*s\" must be preceded by getter \"%.*s\"",
                     ComponentName_Printf(name.name),
                     ComponentName_Printf(getterName));
    }

    // We know the getter previously validated.
    const ComponentFuncType& getterType =
        getItemByName(getterName.utf8Bytes()).asFunc().asFunc();
    ComponentType getterPropertyType =
        getterType.resultType->kind() == ComponentTypeKind::Result
            ? getterType.resultType->asResult().type.value()
            : getterType.resultType.value();

    // [set] must have exactly one parameter (besides "self" for [method]),
    // and this parameter must match the getter's property type
    if (funcType.paramTypes.length() != (isMethod ? 2 : 1)) {
      return d.failf("setter \"%.*s\" must have only one parameter%s",
                     ComponentName_Printf(name.name),
                     isMethod ? " besides self" : "");
    }
    if (funcType.paramTypes[isMethod ? 1 : 0] != getterPropertyType) {
      return d.failf("setter \"%.*s\"'s parameter must match its getter",
                     ComponentName_Printf(name.name));
    }

    // [set] must return nothing or result<E?> (no value type)
    if (funcType.resultType.isSome() &&
        (funcType.resultType->kind() != ComponentTypeKind::Result ||
         funcType.resultType->asResult().type.isSome())) {
      return d.failf("setter \"%.*s\" must return nothing",
                     ComponentName_Printf(name.name));
    }
  }

  return true;
}

// Validate extra conditions from name annotations like [constructor], [method],
// [static], [get], and [set].
template <typename HasFunc, typename GetFunc>
static bool ValidateComponentNameAnnotations(
    Decoder& d, const ComponentName& name,
    const ComponentExternDesc& externDesc, const HasFunc& hasItemWithName,
    const GetFunc& getItemByName) {
  if (name.attributes.isEmpty()) {
    return true;
  }

  if (externDesc.sort() != ComponentSort::Func) {
    return d.fail("name annotations can only be used with functions");
  }
  const ComponentFuncType& funcType = externDesc.asFunc().asFunc();

  if (!ValidateResourceFuncs(d, name, funcType, hasItemWithName,
                             getItemByName)) {
    return false;
  }

  if (!ValidateAccessors(d, name, funcType, hasItemWithName, getItemByName)) {
    return false;
  }

  return true;
}

enum class ComponentImportFlagsRaw : uint8_t {
  // Strangely, the binary encoding currently allows either 0x00 or 0x01 for the
  // flags. Both do exactly the same thing. This is supposed to be cleaned up
  // eventually.
  Plain1 = 0x00,
  Plain2 = 0x01,
  VersionSuffix = 0x02,
};

static bool DecodeComponentImport(Decoder& d, MutableComponent& c,
                                  StronglyUniqueNameSet& nameDedup) {
  uint8_t importFlags;
  if (!d.readFixedU8(&importFlags)) {
    return d.fail("expected import flags");
  }

  switch (importFlags) {
    case uint8_t(ComponentImportFlagsRaw::Plain1):
    case uint8_t(ComponentImportFlagsRaw::Plain2):
      break;
    case uint8_t(ComponentImportFlagsRaw::VersionSuffix):
      // TODO(wasm-cm): Support semver?
      return d.fail("version suffixes on imports are not allowed");
    default:
      return d.failf("invalid import flags %#x", importFlags);
  }

  ComponentName importName;
  if (!DecodeComponentName(d, "import", &importName)) {
    return false;
  }

  ComponentExternDesc externDesc;
  bool unused;
  if (!DecodeComponentExternDesc(d, c, &externDesc, &unused)) {
    return false;
  }
  if (externDesc.sort() == ComponentSort::Type) {
    ComponentType t = externDesc.asType();
    if (t.kind() == ComponentTypeKind::Resource) {
      return d.fail("cannot import a type equal to a defined resource type");
    }
  }

  bool duplicate;
  if (!nameDedup.add(importName.name.utf8Bytes(), &duplicate)) {
    return false;
  }
  if (duplicate) {
    return d.failf("import name \"%.*s\" is not strongly-unique",
                   ComponentName_Printf(importName.name));
  }

  if (!ValidateComponentNameAnnotations(
          d, importName, externDesc,
          [&](mozilla::Span<const char> name) -> bool {
            return c->hasImportWithName(name);
          },
          [&](mozilla::Span<const char> name) -> const ComponentExternDesc& {
            return c->getImportByName(name).externDesc();
          })) {
    return false;
  }

  return c->addImport(ComponentImport(std::move(importName), externDesc));
}

enum class ComponentExportFlagsRaw : uint8_t {
  // As with imports, 0x00 and 0x01 are equivalent flags For Now.
  Plain1 = 0x00,
  Plain2 = 0x01,
  VersionSuffix = 0x02,
};

[[nodiscard]] static bool DecodeComponentExport(
    Decoder& d, MutableComponent& c, StronglyUniqueNameSet& nameDedup) {
  uint8_t exportFlags;
  if (!d.readFixedU8(&exportFlags)) {
    return d.fail("expected export flags");
  }

  switch (exportFlags) {
    case uint8_t(ComponentImportFlagsRaw::Plain1):
    case uint8_t(ComponentImportFlagsRaw::Plain2):
      break;
    case uint8_t(ComponentImportFlagsRaw::VersionSuffix):
      // TODO(wasm-cm): Support semver?
      return d.fail("version suffixes on exports are not allowed");
    default:
      return d.failf("invalid export flags %#x", exportFlags);
  }

  ComponentName exportName;
  if (!DecodeComponentName(d, "export", &exportName)) {
    return false;
  }

  ComponentSortIndex exported;
  {
    ComponentSort exportSort;
    if (!DecodeComponentSort(d, &exportSort, /*forExterndesc=*/true)) {
      return false;
    }
    uint32_t exportIndex;
    if (!d.readVarU32(&exportIndex)) {
      return d.fail("expected export index");
    }
    exported = ComponentSortIndex(exportSort, exportIndex);
  }

  if (!ValidateComponentSortIdx(d, *c, exported)) {
    return false;
  }

  ComponentExternDesc externDesc;
  switch (exported.sort) {
    case ComponentSort::Func: {
      externDesc = ComponentExternDesc::func(c->getTypeForFunc(exported.index));
    } break;
    case ComponentSort::Type: {
      externDesc = ComponentExternDesc::type(c->getType(exported.index));
    } break;
    case ComponentSort::Component: {
      // TODO(wasm-cm): Support all export sorts
      return d.fail("exported components are not supported yet");
    } break;
    case ComponentSort::Instance: {
      // TODO(wasm-cm): Support all export sorts
      return d.fail("exported component instances are not supported yet");
    } break;
    case ComponentSort::CoreModule: {
      externDesc = ComponentExternDesc::coreModule(exported.index);
    } break;
    default:
      MOZ_CRASH("all cases from DecodeComponentSort should have been handled");
  }

  uint8_t hasExplicitExternDesc;
  if (!d.readFixedU8(&hasExplicitExternDesc) || hasExplicitExternDesc > 0x01) {
    return d.fail("expected possible explicit external type");
  }
  if (hasExplicitExternDesc) {
    ComponentExternDesc explicitExternDesc;
    bool isNewSubResource;
    if (!DecodeComponentExternDesc(d, c, &explicitExternDesc,
                                   &isNewSubResource)) {
      return false;
    }

    if (!ComponentExternDesc::compatible(externDesc, explicitExternDesc,
                                         isNewSubResource)) {
      return d.fail(
          "exported item's type did not match explicitly-provided type");
    }
    externDesc = explicitExternDesc;
  }

  // TODO(wasm-cm): Validate that all resource types used (transitively!) in the
  // exported thing's type came from a preceding import or were previously
  // exported. (From talking with Luke, it sounds like actually some (but not
  // all) value types are considered "tricky" enough to fall under this
  // restriction as well, including e.g. records but excluding e.g. s32. What is
  // this list? Who knows.)

  bool duplicate;
  if (!nameDedup.add(exportName.name.utf8Bytes(), &duplicate)) {
    return false;
  }
  if (duplicate) {
    return d.failf("export name \"%.*s\" is not strongly-unique",
                   ComponentName_Printf(exportName.name));
  }

  if (!ValidateComponentNameAnnotations(
          d, exportName, externDesc,
          [&](mozilla::Span<const char> name) -> bool {
            return c->hasExportWithName(name);
          },
          [&](mozilla::Span<const char> name) -> const ComponentExternDesc& {
            return c->getExportByName(name).externDesc();
          })) {
    return false;
  }

  return c->addExport(ComponentExport(std::move(exportName), externDesc));
}

[[nodiscard]] static bool DecodeComponentCoreModuleSection(
    Decoder& d, MutableComponent& c, const BytecodeSpan& moduleBytes,
    const CompileArgs& args, JS::OptimizedEncodingListener* listener) {
  if (c->coreModules().length() >= MaxComponentCoreModules) {
    return d.failf("too many core modules (max %d)", MaxComponentCoreModules);
  }

  BytecodeSource moduleSource(moduleBytes.data(), moduleBytes.size());
  SharedModule module =
      CompileModule(args, BytecodeBufferOrSource(moduleSource), d.error(),
                    d.warnings(), listener);
  if (!module) {
    return false;
  }
  if (!c->addCoreModule(module)) {
    return false;
  }

  MOZ_RELEASE_ASSERT(d.readBytes(moduleBytes.Length()));

  return true;
}

[[nodiscard]] static bool DecodeComponentCoreInstanceSection(
    Decoder& d, MutableComponent& c) {
  uint32_t numInstances;
  if (!d.readVarU32(&numInstances)) {
    return d.fail("expected number of instances");
  }
  if (c->coreInstances().length() + uint64_t(numInstances) >
      MaxComponentCoreInstances) {
    return d.failf("too many core instances (max %d)",
                   MaxComponentCoreInstances);
  }

  for (uint32_t i = 0; i < numInstances; i++) {
    if (!DecodeCoreInstance(d, c)) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] static bool DecodeComponentAliasSection(Decoder& d,
                                                      MutableComponent& c) {
  uint32_t numAliases;
  if (!d.readVarU32(&numAliases)) {
    return d.fail("expected number of aliases");
  }
  // We do not check an implementation limit here because each alias
  // adds entries to a different index space with its own limit.

  for (uint32_t i = 0; i < numAliases; i++) {
    if (!DecodeComponentAlias(d, c)) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] static bool DecodeComponentTypeSection(Decoder& d,
                                                     MutableComponent& c) {
  uint32_t numTypes;
  if (!d.readVarU32(&numTypes)) {
    return d.fail("expected number of types");
  }
  if (c->types().length() + uint64_t(numTypes) > MaxComponentTypes) {
    return d.failf("too many types (max %d)", MaxComponentTypes);
  }

  for (uint32_t i = 0; i < numTypes; i++) {
    if (!DecodeComponentType(d, c)) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] static bool DecodeComponentCanonSection(Decoder& d,
                                                      MutableComponent& c) {
  uint32_t numCanonDefs;
  if (!d.readVarU32(&numCanonDefs)) {
    return d.fail("expected number of canonical definitions");
  }
  // Implementation limits are checked in DecodeComponentCanonDef.

  for (uint32_t i = 0; i < numCanonDefs; i++) {
    if (!DecodeComponentCanonDef(d, c)) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] static bool DecodeComponentImportSection(
    Decoder& d, MutableComponent& c, StronglyUniqueNameSet& nameDedup) {
  uint32_t numImports;
  if (!d.readVarU32(&numImports)) {
    return d.fail("expected number of imports");
  }
  if (c->imports().length() + uint64_t(numImports) > MaxComponentImports) {
    return d.failf("too many imports (max %d)", MaxComponentImports);
  }

  for (uint32_t i = 0; i < numImports; i++) {
    if (!DecodeComponentImport(d, c, nameDedup)) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] static bool DecodeComponentExportSection(
    Decoder& d, MutableComponent& c, StronglyUniqueNameSet& nameDedup) {
  uint32_t numExports;
  if (!d.readVarU32(&numExports)) {
    return d.fail("expected number of exports");
  }
  if (c->exports().length() + uint64_t(numExports) > MaxComponentExports) {
    return d.failf("too many exports (max %d)", MaxComponentExports);
  }

  for (uint32_t i = 0; i < numExports; i++) {
    if (!DecodeComponentExport(d, c, nameDedup)) {
      return false;
    }
  }

  return true;
}

bool wasm::DecodeComponent(Decoder& d, MutableComponent c,
                           const CompileArgs& args,
                           JS::OptimizedEncodingListener* listener) {
  if (!DecodePreamble(d, EncodingVersionComponent)) {
    return false;
  }

  StronglyUniqueNameSet importNameDedup;
  StronglyUniqueNameSet exportNameDedup;

  while (!d.done()) {
    uint8_t sectionID;
    if (!d.readFixedU8(&sectionID)) {
      return d.fail("expected section ID");
    }

    uint32_t sectionLength;
    if (!d.readVarU32(&sectionLength)) {
      return d.fail("expected section length");
    }

    BytecodeSpan sectionBytes;
    size_t sectionOffset;
    if (!d.readBytesSpan(sectionLength, &sectionBytes, &sectionOffset)) {
      return d.failf("invalid section length: expected %" PRIu64
                     " bytes, but only %" PRIu64 " remain",
                     uint64_t(sectionLength), uint64_t(d.bytesRemain()));
    }

    // Decode the section with its own decoder.
    Decoder sectionDecoder(sectionBytes, sectionOffset, d.error(),
                           d.warnings());
    {
      Decoder& d = sectionDecoder;

      switch (sectionID) {
        case uint8_t(ComponentSectionId::Custom): {
          if (!d.readBytes(sectionLength)) {
            return d.fail("expected custom section");
          }

          // TODO(wasm-cm): Parse custom section name, warn if it is "malformed"
          // TODO(wasm-cm): Parse component name section
        } break;

        case uint8_t(ComponentSectionId::CoreModule): {
          if (!DecodeComponentCoreModuleSection(d, c, sectionBytes, args,
                                                listener)) {
            return false;
          }
        } break;
        case uint8_t(ComponentSectionId::CoreInstance): {
          if (!DecodeComponentCoreInstanceSection(d, c)) {
            return false;
          }
        } break;
        case uint8_t(ComponentSectionId::Alias): {
          if (!DecodeComponentAliasSection(d, c)) {
            return false;
          }
        } break;
        case uint8_t(ComponentSectionId::Type): {
          if (!DecodeComponentTypeSection(d, c)) {
            return false;
          }
        } break;
        case uint8_t(ComponentSectionId::Canon): {
          if (!DecodeComponentCanonSection(d, c)) {
            return false;
          }
        } break;
        case uint8_t(ComponentSectionId::Import): {
          if (!DecodeComponentImportSection(d, c, importNameDedup)) {
            return false;
          }
        } break;
        case uint8_t(ComponentSectionId::Export): {
          if (!DecodeComponentExportSection(d, c, exportNameDedup)) {
            return false;
          }
        } break;

        default: {
          return d.failf("unexpected section ID %d", sectionID);
        }
      }

      if (!d.done()) {
        return d.failf("too many bytes in section (%zu extra)",
                       d.bytesRemain());
      }
    }
  }

  return true;
}

#endif  // ENABLE_WASM_COMPONENTS

// Validate algorithm.

[[nodiscard]] static bool ValidateModule(JSContext* cx,
                                         const BytecodeSource& bytecode,
                                         const FeatureArgs& features,
                                         const FeatureOptions& options,
                                         UniqueChars* error) {
  SharedCompileArgs compileArgs = CompileArgs::buildForValidation(features);
  if (!compileArgs) {
    return false;
  }
  MutableModuleMetadata moduleMeta = js_new<ModuleMetadata>();
  if (!moduleMeta || !moduleMeta->init(*compileArgs)) {
    return false;
  }
  MutableCodeMetadata codeMeta = moduleMeta->codeMeta;

  Decoder envDecoder(bytecode.envSpan(), bytecode.envRange().start, error);
  if (!DecodeModuleEnvironment(envDecoder, codeMeta, moduleMeta)) {
    return false;
  }

  if (bytecode.hasCodeSection()) {
    // DecodeModuleEnvironment will stop and return true if there is an unknown
    // section before the code section. We must check this and return an error.
    if (!moduleMeta->codeMeta->codeSectionRange) {
      envDecoder.fail("unknown section before code section");
      return false;
    }

    // Our pre-parse that split the module should ensure that after we've
    // parsed the environment there are no bytes left.
    MOZ_RELEASE_ASSERT(envDecoder.done());

    Decoder codeDecoder(bytecode.codeSpan(), bytecode.codeRange().start, error);
    if (!DecodeCodeSection(codeDecoder, codeMeta)) {
      return false;
    }
    // Our pre-parse that split the module should ensure that after we've
    // parsed the code section there are no bytes left.
    MOZ_RELEASE_ASSERT(codeDecoder.done());

    Decoder tailDecoder(bytecode.tailSpan(), bytecode.tailRange().start, error);
    if (!DecodeModuleTail(tailDecoder, codeMeta, moduleMeta)) {
      return false;
    }
    // Decoding the module tail should consume all remaining bytes.
    MOZ_RELEASE_ASSERT(tailDecoder.done());
  } else {
    if (!DecodeCodeSection(envDecoder, codeMeta)) {
      return false;
    }
    if (!DecodeModuleTail(envDecoder, codeMeta, moduleMeta)) {
      return false;
    }
    // Decoding the module tail should consume all remaining bytes.
    MOZ_RELEASE_ASSERT(envDecoder.done());
  }

  MOZ_ASSERT(!*error, "unreported error in decoding");
  return true;
}

#ifdef ENABLE_WASM_COMPONENTS
[[nodiscard]] static bool ValidateComponent(JSContext* cx,
                                            const BytecodeSource& bytecode,
                                            const FeatureOptions& options,
                                            UniqueChars* error) {
  MutableComponent c = js_new<Component>();
  if (!c) {
    return false;
  }

  CompileArgsError compileArgsError;
  SharedCompileArgs compileArgs =
      CompileArgs::build(cx, ScriptedCaller(), options, &compileArgsError);
  if (!compileArgs) {
    return false;
  }
  Decoder d(bytecode.envSpan(), bytecode.envRange().start, error);
  if (!DecodeComponent(d, c, *compileArgs)) {
    return false;
  }

  MOZ_ASSERT(!*error, "unreported error in decoding");
  return true;
}
#endif  // ENABLE_WASM_COMPONENTS

bool wasm::Validate(JSContext* cx, const BytecodeSource& bytecode,
                    const FeatureOptions& options, UniqueChars* error) {
  FeatureArgs features = FeatureArgs::build(cx, options);

#ifdef ENABLE_WASM_COMPONENTS
  if (features.components) {
    Decoder preambleDecoder(bytecode.envSpan(), bytecode.envRange().start,
                            error);
    if (IsComponent(preambleDecoder)) {
      return ValidateComponent(cx, bytecode, options, error);
    }
  }
#endif
  return ValidateModule(cx, bytecode, features, options, error);
}
