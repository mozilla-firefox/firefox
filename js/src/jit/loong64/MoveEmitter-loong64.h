/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_loong64_MoveEmitter_loong64_h
#define jit_loong64_MoveEmitter_loong64_h

#include "jit/MacroAssembler.h"
#include "jit/MoveResolver.h"

namespace js {
namespace jit {

class MoveEmitterLOONG64 {
  void emitDoubleMove(const MoveOperand& from, const MoveOperand& to);
  void emitSimd128Move(const MoveOperand& from, const MoveOperand& to);
  void breakCycle(const MoveOperand& to, MoveOp::Type type);
  void completeCycle(const MoveOperand& from, const MoveOperand& to,
                     MoveOp::Type type);

  MacroAssembler& masm;

  // Original stack push value.
  uint32_t pushedAtStart_;

  // This stores a stack offset to a spill location, snapshotting
  // `masm.framePushed()` at the time it was allocated. It is -1 if no
  // stack space has been allocated for that particular spill.
  int32_t pushedAtCycle_ = -1;

  // A scratch general register used to break cycles. `InvalidReg` if no cycles
  // are present or no spare scratch registers are available.
  Register cycleGeneralReg_ = InvalidReg;

  bool inCycle_ = false;

  void assertDone() { MOZ_ASSERT(!inCycle_); }
  Address cycleSlot();
  int32_t getAdjustedOffset(const MoveOperand& operand) const;
  Address getAdjustedAddress(const MoveOperand& operand) const;

  void emitMove(const MoveOperand& from, const MoveOperand& to);
  void emitInt32Move(const MoveOperand& from, const MoveOperand& to);
  void emitFloat32Move(const MoveOperand& from, const MoveOperand& to);
  void emit(const MoveOp& move);

 public:
  explicit MoveEmitterLOONG64(MacroAssembler& masm)
      : masm(masm), pushedAtStart_(masm.framePushed()) {}

  ~MoveEmitterLOONG64() { assertDone(); }

  void emit(const MoveResolver& moves);
  void finish();
};

typedef MoveEmitterLOONG64 MoveEmitter;

}  // namespace jit
}  // namespace js

#endif /* jit_loong64_MoveEmitter_loong64_h */
