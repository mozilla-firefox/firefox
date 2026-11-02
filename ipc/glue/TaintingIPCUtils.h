/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_Tainting_h
#define mozilla_ipc_Tainting_h

#include "mozilla/Tainting.h"

#include "chrome/common/ipc_message_utils.h"

namespace IPC {

template <typename T>
struct ParamTraits<mozilla::Tainted<T>> {
  static void Write(MessageWriter* aWriter, const mozilla::Tainted<T>& aParam) {
    WriteParam(aWriter, aParam.mValue);
  }

  static void Write(MessageWriter* aWriter, mozilla::Tainted<T>&& aParam) {
    WriteParam(aWriter, std::move(aParam.mValue));
  }

  static ReadResult<mozilla::Tainted<T>> Read(MessageReader* aReader) {
    auto value = ReadParam<T>(aReader);
    if (!value) {
      return {};
    }
    return mozilla::Tainted<T>(std::move(*value));
  }
};

}  // namespace IPC

#endif  // ifndef mozilla_ipc_Tainting_h
