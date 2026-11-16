/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef IPC_GLUE_IPCMODULE_H_
#define IPC_GLUE_IPCMODULE_H_

#include "ErrorList.h"

namespace mozilla::ipc {

/**
 * Initialize the IPC module.
 *
 * This includes things in IPC which rely on XPCOM.
 */
nsresult InitIPCModule();

}  // namespace mozilla::ipc

#endif
