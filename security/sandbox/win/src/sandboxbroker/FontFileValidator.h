/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_FONTFILEVALIDATOR_H_
#define SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_FONTFILEVALIDATOR_H_

#include <string_view>

#include "nsStringFwd.h"

namespace mozilla::sandboxing {

/**
 * Gets the Win32 path, as used by gfx::FontPathSet, from an NT path sent by a
 * sandboxed process. aWin32Path depends on the memory of aNtPath.
 * @return false if the path is not a plain drive letter path.
 */
bool GetFontFileWin32Path(std::wstring_view aNtPath,
                          nsDependentSubstring& aWin32Path);

/**
 * @return true if a path has the extension, ignoring case, of a font file type
 *         supported by DirectWrite.
 */
bool HasFontFileExtension(const nsAString& aPath);

/**
 * BrokerGeckoServices font file validator that allows access to installed font
 * files.
 * If the path is not found and the font path set is being rebuilt, this waits
 * for a limited time if the path has a font file extension.
 */
bool ValidateFontFile(std::wstring_view aNtPath);

}  // namespace mozilla::sandboxing

#endif  // SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_FONTFILEVALIDATOR_H_
