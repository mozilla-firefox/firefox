// Copyright (c) 2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>

#include "base/file_path.h"
#include "base/logging.h"

// These includes are just for the *Hack functions, and should be removed
// when those functions are removed.
#include "base/string_piece.h"
#include "base/string_util.h"
#include "base/sys_string_conversions.h"

#if defined(FILE_PATH_USES_WIN_SEPARATORS)
const FilePath::CharType FilePath::kSeparators[] = FILE_PATH_LITERAL("\\/");
#else   // FILE_PATH_USES_WIN_SEPARATORS
const FilePath::CharType FilePath::kSeparators[] = FILE_PATH_LITERAL("/");
#endif  // FILE_PATH_USES_WIN_SEPARATORS

const FilePath::CharType FilePath::kCurrentDirectory[] = FILE_PATH_LITERAL(".");
const FilePath::CharType FilePath::kParentDirectory[] = FILE_PATH_LITERAL("..");

const FilePath::CharType FilePath::kExtensionSeparator = FILE_PATH_LITERAL('.');

namespace {

// If this FilePath contains a drive letter specification, returns the
// position of the last character of the drive letter specification,
// otherwise returns npos.  This can only be true on Windows, when a pathname
// begins with a letter followed by a colon.  On other platforms, this always
// returns npos.
FilePath::StringType::size_type FindDriveLetter(
    const FilePath::StringType& path) {
#if defined(FILE_PATH_USES_DRIVE_LETTERS)
  // This is dependent on an ASCII-based character set, but that's a
  // reasonable assumption.  iswalpha can be too inclusive here.
  if (path.length() >= 2 && path[1] == L':' &&
      ((path[0] >= L'A' && path[0] <= L'Z') ||
       (path[0] >= L'a' && path[0] <= L'z'))) {
    return 1;
  }
#endif  // FILE_PATH_USES_DRIVE_LETTERS
  return FilePath::StringType::npos;
}

bool IsPathAbsolute(const FilePath::StringType& path) {
#if defined(FILE_PATH_USES_DRIVE_LETTERS)
  FilePath::StringType::size_type letter = FindDriveLetter(path);
  if (letter != FilePath::StringType::npos) {
    // Look for a separator right after the drive specification.
    return path.length() > letter + 1 &&
           FilePath::IsSeparator(path[letter + 1]);
  }
  // Look for a pair of leading separators.
  return path.length() > 1 && FilePath::IsSeparator(path[0]) &&
         FilePath::IsSeparator(path[1]);
#else   // FILE_PATH_USES_DRIVE_LETTERS
  // Look for a separator in the first position.
  return path.length() > 0 && FilePath::IsSeparator(path[0]);
#endif  // FILE_PATH_USES_DRIVE_LETTERS
}

}  // namespace

bool FilePath::IsSeparator(CharType character) {
  for (size_t i = 0; i < arraysize(kSeparators) - 1; ++i) {
    if (character == kSeparators[i]) {
      return true;
    }
  }

  return false;
}

// libgen's dirname and basename aren't guaranteed to be thread-safe and aren't
// guaranteed to not modify their input strings, and in fact are implemented
// differently in this regard on different platforms.  Don't use them, but
// adhere to their behavior.
FilePath FilePath::DirName() const {
  FilePath new_path(path_);
  new_path.StripTrailingSeparatorsInternal();

  // The drive letter, if any, always needs to remain in the output.  If there
  // is no drive letter, as will always be the case on platforms which do not
  // support drive letters, letter will be npos, or -1, so the comparisons and
  // resizes below using letter will still be valid.
  StringType::size_type letter = FindDriveLetter(new_path.path_);

  StringType::size_type last_separator = new_path.path_.find_last_of(
      kSeparators, StringType::npos, arraysize(kSeparators) - 1);
  if (last_separator == StringType::npos) {
    // path_ is in the current directory.
    new_path.path_.resize(letter + 1);
  } else if (last_separator == letter + 1) {
    // path_ is in the root directory.
    new_path.path_.resize(letter + 2);
  } else if (last_separator == letter + 2 &&
             IsSeparator(new_path.path_[letter + 1])) {
    // path_ is in "//" (possibly with a drive letter); leave the double
    // separator intact indicating alternate root.
    new_path.path_.resize(letter + 3);
  } else if (last_separator != 0) {
    // path_ is somewhere else, trim the basename.
    new_path.path_.resize(last_separator);
  }

  new_path.StripTrailingSeparatorsInternal();
  if (!new_path.path_.length()) new_path.path_ = kCurrentDirectory;

  return new_path;
}

FilePath FilePath::BaseName() const {
  FilePath new_path(path_);
  new_path.StripTrailingSeparatorsInternal();

  // The drive letter, if any, is always stripped.
  StringType::size_type letter = FindDriveLetter(new_path.path_);
  if (letter != StringType::npos) {
    new_path.path_.erase(0, letter + 1);
  }

  // Keep everything after the final separator, but if the pathname is only
  // one character and it's a separator, leave it alone.
  StringType::size_type last_separator = new_path.path_.find_last_of(
      kSeparators, StringType::npos, arraysize(kSeparators) - 1);
  if (last_separator != StringType::npos &&
      last_separator < new_path.path_.length() - 1) {
    new_path.path_.erase(0, last_separator + 1);
  }

  return new_path;
}

FilePath::StringType FilePath::Extension() const {
  // BaseName() calls StripTrailingSeparators, so cases like /foo.baz/// work.
  StringType base = BaseName().value();

  // Special case "." and ".."
  if (base == kCurrentDirectory || base == kParentDirectory)
    return StringType();

  const StringType::size_type last_dot = base.rfind(kExtensionSeparator);
  if (last_dot == StringType::npos) return StringType();
  return StringType(base, last_dot);
}

FilePath FilePath::RemoveExtension() const {
  StringType ext = Extension();
  // It's important to check Extension() since that verifies that the
  // kExtensionSeparator actually appeared in the last path component.
  if (ext.empty()) return FilePath(path_);
  // Since Extension() verified that the extension is in fact in the last path
  // component, this substr will effectively strip trailing separators.
  const StringType::size_type last_dot = path_.rfind(kExtensionSeparator);
  return FilePath(path_.substr(0, last_dot));
}

FilePath FilePath::InsertBeforeExtension(const StringType& suffix) const {
  if (suffix.empty()) return FilePath(path_);

  if (path_.empty()) return FilePath();

  StringType base = BaseName().value();
  if (base.empty()) return FilePath();
  if (*(base.end() - 1) == kExtensionSeparator) {
    // Special case "." and ".."
    if (base == kCurrentDirectory || base == kParentDirectory) {
      return FilePath();
    }
  }

  StringType ext = Extension();
  StringType ret = RemoveExtension().value();
  ret.append(suffix);
  ret.append(ext);
  return FilePath(ret);
}

FilePath FilePath::ReplaceExtension(const StringType& extension) const {
  if (path_.empty()) return FilePath();

  StringType base = BaseName().value();
  if (base.empty()) return FilePath();
  if (*(base.end() - 1) == kExtensionSeparator) {
    // Special case "." and ".."
    if (base == kCurrentDirectory || base == kParentDirectory) {
      return FilePath();
    }
  }

  FilePath no_ext = RemoveExtension();
  // If the new extension is "" or ".", then just remove the current extension.
  if (extension.empty() || extension == StringType(1, kExtensionSeparator))
    return no_ext;

  StringType str = no_ext.value();
  if (extension[0] != kExtensionSeparator) str.append(1, kExtensionSeparator);
  str.append(extension);
  return FilePath(str);
}

FilePath FilePath::Append(const StringType& component) const {
  DCHECK(!IsPathAbsolute(component));
  if (path_.compare(kCurrentDirectory) == 0) {
    // Append normally doesn't do any normalization, but as a special case,
    // when appending to kCurrentDirectory, just return a new path for the
    // component argument.  Appending component to kCurrentDirectory would
    // serve no purpose other than needlessly lengthening the path, and
    // it's likely in practice to wind up with FilePath objects containing
    // only kCurrentDirectory when calling DirName on a single relative path
    // component.
    return FilePath(component);
  }

  FilePath new_path(path_);
  new_path.StripTrailingSeparatorsInternal();

  // Don't append a separator if the path is empty (indicating the current
  // directory) or if the path component is empty (indicating nothing to
  // append).
  if (component.length() > 0 && new_path.path_.length() > 0) {
    // Don't append a separator if the path still ends with a trailing
    // separator after stripping (indicating the root directory).
    if (!IsSeparator(new_path.path_[new_path.path_.length() - 1])) {
      // Don't append a separator if the path is just a drive letter.
      if (FindDriveLetter(new_path.path_) + 1 != new_path.path_.length()) {
        new_path.path_.append(1, kSeparators[0]);
      }
    }
  }

  new_path.path_.append(component);
  return new_path;
}

FilePath FilePath::Append(const FilePath& component) const {
  return Append(component.value());
}

FilePath FilePath::AppendASCII(const std::string& component) const {
  DCHECK(IsStringASCII(component));
#if defined(XP_WIN)
  return Append(ASCIIToWide(component));
#else
  return Append(component);
#endif
}

bool FilePath::IsAbsolute() const { return IsPathAbsolute(path_); }

#if defined(XP_UNIX)
// See file_path.h for a discussion of the encoding of paths on POSIX
// platforms.  These *Hack() functions are not quite correct, but they're
// only temporary while we fix the remainder of the code.
// Remember to remove the #includes at the top when you remove these.

// static
FilePath FilePath::FromWStringHack(const std::wstring& wstring) {
  return FilePath(base::SysWideToNativeMB(wstring));
}
std::wstring FilePath::ToWStringHack() const {
  return base::SysNativeMBToWide(path_);
}
#else
// static
FilePath FilePath::FromWStringHack(const std::wstring& wstring) {
  return FilePath(wstring);
}
std::wstring FilePath::ToWStringHack() const { return path_; }
#endif

void FilePath::OpenInputStream(std::ifstream& stream) const {
  stream.open(path_.c_str(), std::ios::in | std::ios::binary);
}

FilePath FilePath::StripTrailingSeparators() const {
  FilePath new_path(path_);
  new_path.StripTrailingSeparatorsInternal();

  return new_path;
}

void FilePath::StripTrailingSeparatorsInternal() {
  // If there is no drive letter, start will be 1, which will prevent stripping
  // the leading separator if there is only one separator.  If there is a drive
  // letter, start will be set appropriately to prevent stripping the first
  // separator following the drive letter, if a separator immediately follows
  // the drive letter.
  StringType::size_type start = FindDriveLetter(path_) + 2;

  StringType::size_type last_stripped = StringType::npos;
  for (StringType::size_type pos = path_.length();
       pos > start && IsSeparator(path_[pos - 1]); --pos) {
    // If the string only has two separators and they're at the beginning,
    // don't strip them, unless the string began with more than two separators.
    if (pos != start + 1 || last_stripped == start + 2 ||
        !IsSeparator(path_[start - 1])) {
      path_.resize(pos - 1);
      last_stripped = pos;
    }
  }
}
