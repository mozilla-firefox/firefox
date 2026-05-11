/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLValidateStrings.h"

#include <regex>

#include "WebGLTypes.h"
#include "nsPrintfCString.h"

namespace mozilla {

/* GLSL ES 3.00 p17:
  - Comments are delimited by / * and * /, or by // and a newline.

  - '//' style comments include the initial '//' marker and continue up to, but
not including, the terminating newline.

  - '/ * ... * /' comments include both the start and end marker.

  - The begin comment delimiters (/ * or //) are not recognized as comment
delimiters inside of a comment, hence comments cannot be nested.

  - Comments are treated syntactically as a single space.
*/

std::string CommentsToSpaces(const std::string& src) {
  constexpr auto flags =
      std::regex::ECMAScript | std::regex::nosubs | std::regex::optimize;

  static const auto RE_COMMENT_BEGIN = std::regex("/[*/]", flags);
  static const auto RE_LINE_COMMENT_END = std::regex(R"([^\\]\n)", flags);
  static const auto RE_BLOCK_COMMENT_END = std::regex(R"(\*/)", flags);

  std::string ret;
  ret.reserve(src.size());

  // Replace all comments with block comments with the right number of newlines.
  // Line positions may be off, but line numbers will be accurate, which is more
  // important.

  auto itr = src.begin();
  const auto end = src.end();
  std::smatch match;
  while (std::regex_search(itr, end, match, RE_COMMENT_BEGIN)) {
    MOZ_ASSERT(match.length() == 2);
    const auto commentBegin = itr + match.position();
    ret.append(itr, commentBegin);

    itr = commentBegin + match.length();

    const bool isBlockComment = (*(commentBegin + 1) == '*');
    const auto* endRegex = &RE_LINE_COMMENT_END;
    if (isBlockComment) {
      endRegex = &RE_BLOCK_COMMENT_END;
    }

    if (isBlockComment) {
      ret += "/*";
    }

    auto commentEnd = end;
    if (!isBlockComment && itr != end && *itr == '\n') {
      commentEnd = itr + 1;  // '//\n'
    } else if (std::regex_search(itr, end, match, *endRegex)) {
      commentEnd = itr + match.position() + match.length();
    } else {
      return ret;
    }

    for (; itr != commentEnd; ++itr) {
      const auto cur = *itr;
      if (cur == '\n') {
        ret += cur;
      }
    }
    if (isBlockComment) {
      ret += "*/";
    }
  }

  ret.append(itr, end);
  return ret;
}

////////////////////////////////////////////////////////////////////////////////

static constexpr bool IsValidGLSLChar(const char c) {
  if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
      ('0' <= c && c <= '9')) {
    return true;
  }

  switch (c) {
    case ' ':
    case '\t':
    case '\v':
    case '\f':
    case '\r':
    case '\n':
    case '_':
    case '.':
    case '+':
    case '-':
    case '/':
    case '*':
    case '%':
    case '<':
    case '>':
    case '[':
    case ']':
    case '(':
    case ')':
    case '{':
    case '}':
    case '^':
    case '|':
    case '&':
    case '~':
    case '=':
    case '!':
    case ':':
    case ';':
    case ',':
    case '?':
      return true;

    default:
      return false;
  }
}

static constexpr bool IsValidForPreprocOrGlsl(const char c) {
  switch (c) {
    case '#':
    case '\\':
      return true;

    default:
      return IsValidGLSLChar(c);
  }
}

////

static constexpr char INVALID_GLSL_CHAR = '$';

std::string CrushGlslToAscii(const std::string& u8) {
  static_assert(!IsValidForPreprocOrGlsl(INVALID_GLSL_CHAR));
  auto ascii = u8;
  for (auto& c : ascii) {
    if (!IsValidForPreprocOrGlsl(c)) [[unlikely]] {
      c = INVALID_GLSL_CHAR;
    }
  }
  return ascii;
}

Maybe<webgl::ErrorInfo> CheckGLSLVariableName(const bool webgl2,
                                              const std::string& name) {
  if (name.empty()) return {};

  const uint32_t maxSize = webgl2 ? 1024 : 256;
  if (name.size() > maxSize) {
    const auto info = nsPrintfCString(
        "Identifier is %zu characters long, exceeds the"
        " maximum allowed length of %u characters.",
        name.size(), maxSize);
    return Some(webgl::ErrorInfo{LOCAL_GL_INVALID_VALUE, info.get()});
  }

  for (const auto cur : name) {
    if (!IsValidGLSLChar(cur)) {
      const auto info =
          nsPrintfCString("String contains the illegal character 0x%x'.", cur);
      return Some(webgl::ErrorInfo{LOCAL_GL_INVALID_VALUE, info.get()});
    }
  }

  if (name.find("webgl_") == 0 || name.find("_webgl_") == 0) {
    return Some(webgl::ErrorInfo{
        LOCAL_GL_INVALID_OPERATION,
        "String matches reserved GLSL prefix pattern /_?webgl_/."});
  }

  return {};
}

}  // namespace mozilla
