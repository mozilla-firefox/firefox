/*
 * Copyright (c) 2009-2010 Mozilla Foundation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * THIS IS A GENERATED FILE. PLEASE DO NOT EDIT.
 * Please edit StateSnapshot.java instead and regenerate.
 */

#ifndef nsHtml5StateSnapshot_h
#define nsHtml5StateSnapshot_h

#include "nsAtom.h"
#include "nsHtml5AtomTable.h"
#include "nsHtml5String.h"
#include "nsNameSpaceManager.h"
#include "nsIContent.h"
#include "nsTraceRefcnt.h"
#include "jArray.h"
#include "nsHtml5ArrayCopy.h"
#include "nsAHtml5TreeBuilderState.h"
#include "nsGkAtoms.h"
#include "nsHtml5ByteReadable.h"
#include "nsHtml5Macros.h"
#include "nsIContentHandle.h"
#include "nsHtml5Portability.h"
#include "nsHtml5ContentCreatorFunction.h"

class nsHtml5StreamParser;

class nsHtml5AttributeName;
class nsHtml5ElementName;
class nsHtml5Tokenizer;
class nsHtml5TreeBuilder;
class nsHtml5UTF16Buffer;
class nsHtml5Portability;

class nsHtml5StateSnapshot : public nsAHtml5TreeBuilderState {
 private:
  autoJArray<nsHtml5StackNode*, int32_t> stack;
  autoJArray<nsHtml5StackNode*, int32_t> listOfActiveFormattingElements;
  autoJArray<int32_t, int32_t> templateModeStack;
  nsIContentHandle* formPointer;
  nsIContentHandle* headPointer;
  int32_t mode;
  int32_t originalMode;
  bool framesetOk;
  bool needToDropLF;
  bool quirks;

 public:
  nsHtml5StateSnapshot(
      jArray<nsHtml5StackNode*, int32_t> stack,
      jArray<nsHtml5StackNode*, int32_t> listOfActiveFormattingElements,
      jArray<int32_t, int32_t> templateModeStack, nsIContentHandle* formPointer,
      nsIContentHandle* headPointer, int32_t mode, int32_t originalMode,
      bool framesetOk, bool needToDropLF, bool quirks);
  jArray<nsHtml5StackNode*, int32_t> getStack() override;
  jArray<int32_t, int32_t> getTemplateModeStack() override;
  jArray<nsHtml5StackNode*, int32_t> getListOfActiveFormattingElements()
      override;
  nsIContentHandle* getFormPointer() override;
  nsIContentHandle* getHeadPointer() override;
  int32_t getMode() override;
  int32_t getOriginalMode() override;
  bool isFramesetOk() override;
  bool isNeedToDropLF() override;
  bool isQuirks() override;
  int32_t getListOfActiveFormattingElementsLength() override;
  int32_t getStackLength() override;
  int32_t getTemplateModeStackLength() override;
  ~nsHtml5StateSnapshot();
  static void initializeStatics();
  static void releaseStatics();
};

#endif
