/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FontFileValidator.h"

#include <windows.h>

#include "mozilla/TextUtils.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/gfx/FontPathSet.h"
#include "nsString.h"
#include "nsUnicharUtils.h"

namespace mozilla::sandboxing {

using gfx::FontPathSet;

static constexpr std::wstring_view kNTPrefix = LR"(\??\)";

// Maximum time to wait for the font path set to be rebuilt, when a path with a
// font file extension is not found.
static constexpr uint32_t kMaxRebuildWaitMs = 3000;

bool GetFontFileWin32Path(std::wstring_view aNtPath,
                          nsDependentSubstring& aWin32Path) {
  if (!aNtPath.starts_with(kNTPrefix)) {
    return false;
  }

  std::wstring_view path = aNtPath.substr(kNTPrefix.length());
  if (path.length() < 4 || !IsAsciiAlpha(static_cast<char16_t>(path[0])) ||
      path[1] != L':' || path[2] != L'\\') {
    return false;
  }

  if (path.find(LR"(\..\)") != std::wstring_view::npos ||
      path.ends_with(LR"(\..)")) {
    return false;
  }

  aWin32Path.Rebind(reinterpret_cast<const char16_t*>(path.data()),
                    path.length());
  return true;
}

bool HasFontFileExtension(const nsAString& aPath) {
  static constexpr nsLiteralString kFontFileExtensions[] = {
      u".ttf"_ns,
      u".otf"_ns,
      u".ttc"_ns,
      u".otc"_ns,
  };

  for (const auto& extension : kFontFileExtensions) {
    if (StringEndsWith(aPath, extension,
                       nsASCIICaseInsensitiveStringComparator)) {
      return true;
    }
  }

  return false;
}

bool ValidateFontFile(std::wstring_view aNtPath) {
  nsDependentSubstring path;
  if (!GetFontFileWin32Path(aNtPath, path)) {
    return false;
  }

  // Only wait for the font path set to be rebuilt if the path is likely to be a
  // font.
  TimeDuration maxWait;
  if (HasFontFileExtension(path)) {
    maxWait = TimeDuration::FromMilliseconds(kMaxRebuildWaitMs);
  }

  return FontPathSet::Contains(path, maxWait);
}

}  // namespace mozilla::sandboxing
