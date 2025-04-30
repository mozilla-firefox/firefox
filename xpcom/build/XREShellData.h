/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef XREShellData_h
#define XREShellData_h

#if defined(LIBFUZZER)
#  include "FuzzerRegistry.h"  // LibFuzzerDriver
#endif
#if defined(AFLFUZZ)
#  include "FuzzingInterface.h"  // FuzzingTestFuncRaw
#endif

#if defined(XP_WIN) && defined(MOZ_SANDBOX)
namespace sandbox {
class BrokerServices;
}
#endif

/**
 * Data needed by XRE_XPCShellMain.
 */
struct XREShellData {
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
  /**
   * Chromium sandbox BrokerServices.
   */
  sandbox::BrokerServices* sandboxBrokerServices;
#endif
#if defined(ANDROID)
  FILE* outFile;
  FILE* errFile;
  int crashChildNotificationSocket;
  int crashHelperSocket;
#endif
#if defined(LIBFUZZER)
  LibFuzzerDriver fuzzerDriver;
#endif
#if defined(AFLFUZZ)
  int (*fuzzerDriver)(FuzzingTestFuncRaw);
#endif
};

#endif  // XREShellData_h
