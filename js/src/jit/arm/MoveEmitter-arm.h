/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_arm_MoveEmitter_arm_h
#define jit_arm_MoveEmitter_arm_h

#include <stdint.h>

#include "jit/arm/Assembler-arm.h"
#include "jit/MoveResolver.h"
#include "jit/Registers.h"

namespace js {
namespace jit {

struct Address;
class MacroAssembler;

class MoveEmitterARM {
  uint32_t inCycle_ = 0;
  MacroAssembler& masm;

  // Original stack push value.
  uint32_t pushedAtStart_;

  // These store stack offsets to spill locations, snapshotting
  // `masm.framePushed()` at the time they were allocated. They are -1 if no
  // stack space has been allocated for that particular spill.
  int32_t pushedAtCycle_ = -1;
  int32_t pushedAtSpill_ = -1;

  // Scratch register available for temporary use. It may be assigned
  // InvalidReg. If no corresponding spill space has been assigned, then it
  // doesn't need to be spilled.
  Register spilledReg_ = InvalidReg;

  void assertDone() { MOZ_ASSERT(inCycle_ == 0); }
  Register tempReg();
  Address cycleSlot(uint32_t slot, uint32_t subslot) const;
  Address spillSlot() const;
  Address toAddress(const MoveOperand& operand) const;

  void emitMove(const MoveOperand& from, const MoveOperand& to);
  void emitFloat32Move(const MoveOperand& from, const MoveOperand& to);
  void emitDoubleMove(const MoveOperand& from, const MoveOperand& to);
  void breakCycle(const MoveOperand& to, MoveOp::Type type, uint32_t slot);
  void completeCycle(const MoveOperand& from, const MoveOperand& to,
                     MoveOp::Type type, uint32_t slot);
  void emit(const MoveOp& move);

 public:
  explicit MoveEmitterARM(MacroAssembler& masm);
  ~MoveEmitterARM() { assertDone(); }
  void emit(const MoveResolver& moves);
  void finish();
};

using MoveEmitter = MoveEmitterARM;

}  // namespace jit
}  // namespace js

#endif /* jit_arm_MoveEmitter_arm_h */
