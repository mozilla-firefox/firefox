//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// string_utils:
//   String helper functions.
//

#include "common/string_utils.h"
#include "common/unsafe_buffers.h"

#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>

#include "common/platform.h"
#include "common/system_utils.h"

namespace
{

bool EndsWithSuffix(const char *str,
                    const size_t strLen,
                    const char *suffix,
                    const size_t suffixLen)
{
    return suffixLen <= strLen &&
           ANGLE_UNSAFE_TODO(strncmp(str + strLen - suffixLen, suffix, suffixLen)) == 0;
}

}  // anonymous namespace

namespace angle
{

const char kWhitespaceASCII[] = " \f\n\r\t\v";

std::vector<std::string> SplitString(const std::string &input,
                                     const std::string_view &delimiters,
                                     WhitespaceHandling whitespace,
                                     SplitResult resultType)
{
    std::vector<std::string_view> views =
        SplitStringView(input, delimiters, whitespace, resultType);
    std::vector<std::string> result(views.size());
    for (size_t i = 0; i < views.size(); i++)
    {
        result[i] = std::string(views[i]);
    }
    return result;
}

std::vector<std::string_view> SplitStringView(const std::string_view &input,
                                              const std::string_view &delimiters,
                                              WhitespaceHandling whitespace,
                                              SplitResult resultType)
{
    std::vector<std::string_view> result;
    if (input.empty())
    {
        return result;
    }

    std::string::size_type start = 0;
    while (start != std::string::npos)
    {
        auto end = input.find_first_of(delimiters, start);

        std::string_view piece;
        if (end == std::string::npos)
        {
            piece = input.substr(start);
            start = std::string::npos;
        }
        else
        {
            piece = input.substr(start, end - start);
            start = end + 1;
        }

        if (whitespace == TRIM_WHITESPACE)
        {
            piece = TrimStringView(piece, kWhitespaceASCII);
        }

        if (resultType == SPLIT_WANT_ALL || !piece.empty())
        {
            result.push_back(std::move(piece));
        }
    }

    return result;
}

void SplitStringAlongWhitespace(const std::string &input, std::vector<std::string> *tokensOut)
{

    std::istringstream stream(input);
    std::string line;

    while (std::getline(stream, line))
    {
        size_t prev = 0, pos;
        while ((pos = line.find_first_of(kWhitespaceASCII, prev)) != std::string::npos)
        {
            if (pos > prev)
                tokensOut->push_back(line.substr(prev, pos - prev));
            prev = pos + 1;
        }
        if (prev < line.length())
            tokensOut->push_back(line.substr(prev, std::string::npos));
    }
}

std::string TrimString(const std::string &input, const std::string &trimChars)
{
    return std::string(TrimStringView(input, trimChars));
}

std::string_view TrimStringView(const std::string_view &input, const std::string &trimChars)
{
    auto begin = input.find_first_not_of(trimChars);
    if (begin == std::string::npos)
    {
        return "";
    }

    std::string::size_type end = input.find_last_not_of(trimChars);
    if (end == std::string::npos)
    {
        return input.substr(begin);
    }

    return input.substr(begin, end - begin + 1);
}

std::string GetPrefix(const std::string &input, size_t offset, const char *delimiter)
{
    size_t match = input.find(delimiter, offset);
    if (match == std::string::npos)
    {
        return input.substr(offset);
    }
    return input.substr(offset, match - offset);
}

std::string GetPrefix(const std::string &input, size_t offset, char delimiter)
{
    size_t match = input.find(delimiter, offset);
    if (match == std::string::npos)
    {
        return input.substr(offset);
    }
    return input.substr(offset, match - offset);
}

bool HexStringToUInt(const std::string_view &input, unsigned int *uintOut)
{
    if (input.empty() || uintOut == nullptr)
    {
        return false;
    }

    unsigned int offset = 0;

    if (input.size() >= 2 && input[0] == '0' && input[1] == 'x')
    {
        offset = 2u;
    }

    // Simple validity check
    if (input.find_first_not_of("0123456789ABCDEFabcdef", offset) != std::string::npos)
    {
        return false;
    }

    std::string_view inputWithOffset = input.substr(offset);
    const auto result                = std::from_chars(
        inputWithOffset.data(), ANGLE_UNSAFE_TODO(inputWithOffset.data() + inputWithOffset.size()),
        *uintOut, 16);
    if (result.ec != std::errc{})
    {
        return false;
    }

    // A successful conversion should consume the entire input string view.
    // If result.ptr is not at the end, it means there were extra characters.
    if (result.ptr != ANGLE_UNSAFE_TODO(inputWithOffset.data() + inputWithOffset.size()))
    {
        return false;
    }

    return true;
}

bool StringToInt64(const std::string_view &input, int64_t *valueOut)
{
    // SAFETY: data() + size() is the end of the string view.
    const char *end   = ANGLE_UNSAFE_BUFFERS(input.data() + input.size());
    int64_t value     = 0;
    const auto result = std::from_chars(input.data(), end, value);
    if (result.ec != std::errc{} || result.ptr != end)
    {
        return false;
    }

    *valueOut = value;
    return true;
}

bool ReadFileToString(const std::string &path, std::string *stringOut)
{
    std::ifstream inFile(path.c_str(), std::ios::binary);
    if (inFile.fail())
    {
        return false;
    }

    inFile.seekg(0, std::ios::end);
    auto size = static_cast<std::string::size_type>(inFile.tellg());
    stringOut->resize(size);
    inFile.seekg(0, std::ios::beg);

    inFile.read(stringOut->data(), size);
    return !inFile.fail();
}

bool BeginsWith(const std::string &str, const std::string &prefix)
{
    return ANGLE_UNSAFE_TODO(strncmp(str.c_str(), prefix.c_str(), prefix.length())) == 0;
}

bool BeginsWith(const std::string &str, const char *prefix)
{
    return ANGLE_UNSAFE_TODO(strncmp(str.c_str(), prefix, strlen(prefix))) == 0;
}

bool BeginsWith(const char *str, const char *prefix)
{
    return ANGLE_UNSAFE_TODO(strncmp(str, prefix, strlen(prefix))) == 0;
}

bool BeginsWith(const std::string &str, const std::string &prefix, const size_t prefixLength)
{
    return ANGLE_UNSAFE_TODO(strncmp(str.c_str(), prefix.c_str(), prefixLength)) == 0;
}

bool EndsWith(const std::string &str, const std::string &suffix)
{
    return EndsWithSuffix(str.c_str(), str.length(), suffix.c_str(), suffix.length());
}

bool EndsWith(const std::string &str, const char *suffix)
{
    return EndsWithSuffix(str.c_str(), str.length(), suffix, strlen(suffix));
}

bool EndsWith(const char *str, const char *suffix)
{
    return EndsWithSuffix(str, strlen(str), suffix, strlen(suffix));
}

bool ContainsToken(const std::string &tokenStr, char delimiter, const std::string &token)
{
    if (token.empty())
    {
        return false;
    }
    // Compare token with all sub-strings terminated by delimiter or end of string
    std::string::size_type start = 0u;
    do
    {
        std::string::size_type end = tokenStr.find(delimiter, start);
        if (end == std::string::npos)
        {
            end = tokenStr.length();
        }
        const std::string::size_type length = end - start;
        if (length == token.length() && tokenStr.compare(start, length, token) == 0)
        {
            return true;
        }
        start = end + 1u;
    } while (start < tokenStr.size());
    return false;
}

void ToLower(std::string *str)
{
    for (char &ch : *str)
    {
        ch = static_cast<char>(::tolower(ch));
    }
}

void ToUpper(std::string *str)
{
    for (char &ch : *str)
    {
        ch = static_cast<char>(::toupper(ch));
    }
}

bool ReplaceSubstring(std::string *str,
                      const std::string &substring,
                      const std::string &replacement)
{
    size_t replacePos = str->find(substring);
    if (replacePos == std::string::npos)
    {
        return false;
    }
    str->replace(replacePos, substring.size(), replacement);
    return true;
}

int ReplaceAllSubstrings(std::string *str,
                         const std::string &substring,
                         const std::string &replacement)
{
    int count = 0;
    while (ReplaceSubstring(str, substring, replacement))
    {
        count++;
    }
    return count;
}

std::string ToCamelCase(const std::string &str)
{
    std::string result;

    bool lastWasUnderscore = false;
    for (char c : str)
    {
        if (c == '_')
        {
            lastWasUnderscore = true;
            continue;
        }

        if (lastWasUnderscore)
        {
            c                 = static_cast<char>(std::toupper(c));
            lastWasUnderscore = false;
        }
        result += c;
    }

    return result;
}

std::vector<std::string> GetStringsFromEnvironmentVarOrAndroidProperty(const char *varName,
                                                                       const char *propertyName,
                                                                       const char *separator)
{
    std::string environment = GetEnvironmentVarOrAndroidProperty(varName, propertyName);
    return SplitString(environment, separator, TRIM_WHITESPACE, SPLIT_WANT_NONEMPTY);
}

std::vector<std::string> GetCachedStringsFromEnvironmentVarOrAndroidProperty(
    const char *varName,
    const char *propertyName,
    const char *separator)
{
    std::string environment = GetEnvironmentVarOrAndroidProperty(varName, propertyName);
    return SplitString(environment, separator, TRIM_WHITESPACE, SPLIT_WANT_NONEMPTY);
}

bool IsGlobPattern(const std::string_view &pattern)
{
    return std::any_of(pattern.begin(), pattern.end(),
                       [](const char c) { return c == '?' || c == '*'; });
}

bool NamesMatchWithWildcard(const std::string_view &glob, const std::string_view &name)
{
    // This function implements a linear-time string globbing algorithm based on
    // https://research.swtch.com/glob.
    // It is mostly taken from the implementation in gtest.cc.

    using StringIter = std::string_view::iterator;

    StringIter nameIter        = name.begin();
    const StringIter nameBegin = name.begin();
    const StringIter nameEnd   = name.end();

    StringIter globIter      = glob.begin();
    const StringIter globEnd = glob.end();

    StringIter globNext = globIter;
    StringIter nameNext = nameIter;

    while (globIter < globEnd || nameIter < nameEnd)
    {
        if (globIter < globEnd)
        {
            switch (*globIter)
            {
                default:  // Match an ordinary character.
                    if (nameIter < nameEnd && *nameIter == *globIter)
                    {
                        ++globIter;
                        ++nameIter;
                        continue;
                    }
                    break;

                case '?':  // Match any single character.
                    if (nameIter < nameEnd)
                    {
                        ++globIter;
                        ++nameIter;
                        continue;
                    }
                    break;

                case '*':
                    // Match zero or more characters. Start by skipping over the wildcard
                    // and matching zero characters from name. If that fails, restart and
                    // match one more character than the last attempt.
                    globNext = globIter;
                    nameNext = nameIter + 1;
                    ++globIter;
                    continue;
            }
        }

        // Failed to match a character. Restart if possible.
        if (nameBegin < nameNext && nameNext <= nameEnd)
        {
            globIter = globNext;
            nameIter = nameNext;
            continue;
        }

        return false;
    }

    return true;
}

std::vector<uint8_t> HexStringToUintVector(const std::string_view &hexStr)
{
    std::vector<uint8_t> bin;
    bin.reserve(hexStr.length() / 2);

    for (size_t index = 0; index < hexStr.length(); index += 2)
    {
        unsigned int hexValue;
        HexStringToUInt(hexStr.substr(index, 2), &hexValue);
        bin.push_back(static_cast<uint8_t>(hexValue));
    }

    return bin;
}

std::string UintStreamToHexString(angle::Span<const uint8_t> uintStream)
{
    std::string result;
    result.reserve(uintStream.size() * 2 + 1);

    static constexpr std::array<char, 16> kNibbleToHex = {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
    };

    for (uint8_t u : uintStream)
    {
        result += kNibbleToHex[u >> 4];
        result += kNibbleToHex[u & 0xF];
    }

    return result;
}

}  // namespace angle
