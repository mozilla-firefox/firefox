/*
 * Copyright (c) 2008-2010 Mozilla Foundation
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
 * Please edit UTF16Buffer.java instead and regenerate.
 */

#ifndef nsHtml5UTF16Buffer_h
#define nsHtml5UTF16Buffer_h

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
class nsHtml5StateSnapshot;
class nsHtml5Portability;

class nsHtml5UTF16Buffer {
 private:
  char16_t* buffer;
  int32_t start;
  int32_t end;

 public:
  int32_t getStart();
  void setStart(int32_t start);
  char16_t* getBuffer();
  int32_t getEnd();
  bool hasMore();
  int32_t getLength();
  void adjust(bool lastWasCR);
  void setEnd(int32_t end);
  static void initializeStatics();
  static void releaseStatics();

#include "nsHtml5UTF16BufferHSupplement.h"
};

#endif
