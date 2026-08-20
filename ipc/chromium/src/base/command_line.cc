// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"

#if defined(XP_WIN)
#  include <windows.h>
#  include <shellapi.h>
#  include "mozilla/DynamicallyLinkedFunctionPtr.h"
#endif

#include "base/logging.h"
#include "base/string_piece.h"
#include "base/string_util.h"
#include "base/sys_string_conversions.h"

CommandLine* CommandLine::current_process_commandline_ = nullptr;

// Since we use a lazy match, make sure that longer versions (like L"--")
// are listed before shorter versions (like L"-") of similar prefixes.
#if defined(XP_WIN)
const wchar_t* const kSwitchPrefixes[] = {L"--", L"-", L"/"};
const wchar_t kSwitchTerminator[] = L"--";
const wchar_t kSwitchValueSeparator[] = L"=";
#else
// Unixes don't use slash as a switch.
const char* const kSwitchPrefixes[] = {"--", "-"};
const char kSwitchTerminator[] = "--";
const char kSwitchValueSeparator[] = "=";
#endif

#if defined(XP_WIN)
// Lowercase a string.  This is used to lowercase switch names.
// Is this what we really want?  It seems crazy to me.  I've left it in
// for backwards compatibility on Windows.
static void Lowercase(std::wstring* parameter) {
  transform(parameter->begin(), parameter->end(), parameter->begin(), tolower);
}
#endif

#if defined(XP_WIN)
void CommandLine::ParseFromString(const std::wstring& command_line) {
  TrimWhitespace(command_line, TRIM_ALL, &command_line_string_);

  if (command_line_string_.empty()) return;

  int num_args = 0;
  wchar_t** args = nullptr;
  // When calling CommandLineToArgvW, use the API set if available.
  // Doing so will bypass loading shell32.dll on Win8+.
  mozilla::DynamicallyLinkedFunctionPtr<decltype(&::CommandLineToArgvW)>
      pCommandLineToArgvW(L"api-ms-win-downlevel-shell32-l1-1-0.dll",
                          "CommandLineToArgvW");
  if (pCommandLineToArgvW) {
    args = pCommandLineToArgvW(command_line_string_.c_str(), &num_args);
  } else {
    args = CommandLineToArgvW(command_line_string_.c_str(), &num_args);
  }

  // Populate program_ with the trimmed version of the first arg.
  TrimWhitespace(args[0], TRIM_ALL, &program_);

  bool parse_switches = true;
  for (int i = 1; i < num_args; ++i) {
    std::wstring arg;
    TrimWhitespace(args[i], TRIM_ALL, &arg);

    if (!parse_switches) {
      loose_values_.push_back(arg);
      continue;
    }

    if (arg == kSwitchTerminator) {
      parse_switches = false;
      continue;
    }

    std::string switch_string;
    std::wstring switch_value;
    if (IsSwitch(arg, &switch_string, &switch_value)) {
      switches_[switch_string] = switch_value;
    } else {
      loose_values_.push_back(arg);
    }
  }

  if (args) LocalFree(args);
}
CommandLine::CommandLine(const std::wstring& program) {
  if (!program.empty()) {
    program_ = program;
    command_line_string_ = L'"' + program + L'"';
  }
}
#else
CommandLine::CommandLine(int argc, const char* const* argv) {
  for (int i = 0; i < argc; ++i) argv_.push_back(argv[i]);
  InitFromArgv();
}
CommandLine::CommandLine(const std::vector<std::string>& argv) {
  argv_ = argv;
  InitFromArgv();
}

void CommandLine::InitFromArgv() {
  bool parse_switches = true;
  for (size_t i = 1; i < argv_.size(); ++i) {
    const std::string& arg = argv_[i];

    if (!parse_switches) {
      loose_values_.push_back(arg);
      continue;
    }

    if (arg == kSwitchTerminator) {
      parse_switches = false;
      continue;
    }

    std::string switch_string;
    std::string switch_value;
    if (IsSwitch(arg, &switch_string, &switch_value)) {
      switches_[switch_string] = switch_value;
    } else {
      loose_values_.push_back(arg);
    }
  }
}

CommandLine::CommandLine(const std::wstring& program) {
  argv_.push_back(WideToASCII(program));
}
#endif

// static
bool CommandLine::IsSwitch(const StringType& parameter_string,
                           std::string* switch_string,
                           StringType* switch_value) {
  switch_string->clear();
  switch_value->clear();

  for (auto switchPrefix : kSwitchPrefixes) {
    StringType prefix(switchPrefix);
    if (parameter_string.find(prefix) != 0) continue;

    const size_t switch_start = prefix.length();
    const size_t equals_position =
        parameter_string.find(kSwitchValueSeparator, switch_start);
    StringType switch_native;
    if (equals_position == StringType::npos) {
      switch_native = parameter_string.substr(switch_start);
    } else {
      switch_native =
          parameter_string.substr(switch_start, equals_position - switch_start);
      *switch_value = parameter_string.substr(equals_position + 1);
    }
#if defined(XP_WIN)
    Lowercase(&switch_native);
    *switch_string = WideToASCII(switch_native);
#else
    *switch_string = switch_native;
#endif

    return true;
  }

  return false;
}

// static
void CommandLine::Init(int argc, const char* const* argv) {
  DCHECK(current_process_commandline_ == nullptr);
#if defined(XP_WIN)
  current_process_commandline_ = new CommandLine;
  current_process_commandline_->ParseFromString(::GetCommandLineW());
#else
  current_process_commandline_ = new CommandLine(argc, argv);
#endif
}

void CommandLine::Terminate() {
  DCHECK(current_process_commandline_ != nullptr);
  delete current_process_commandline_;
  current_process_commandline_ = nullptr;
}

bool CommandLine::HasSwitch(const std::wstring& switch_string) const {
#if defined(XP_WIN)
  std::wstring lowercased_switch(switch_string);
  Lowercase(&lowercased_switch);
#else
  const std::wstring& lowercased_switch = switch_string;
#endif
  return switches_.find(WideToASCII(lowercased_switch)) != switches_.end();
}

std::wstring CommandLine::GetSwitchValue(
    const std::wstring& switch_string) const {
#if defined(XP_WIN)
  std::wstring lowercased_switch(switch_string);
  Lowercase(&lowercased_switch);
#else
  const std::wstring& lowercased_switch = switch_string;
#endif

  std::map<std::string, StringType>::const_iterator result =
      switches_.find(WideToASCII(lowercased_switch));

  if (result == switches_.end()) {
    return L"";
  } else {
#if defined(XP_WIN)
    return result->second;
#else
    return ASCIIToWide(result->second);
#endif
  }
}

#if defined(XP_WIN)
std::vector<std::wstring> CommandLine::GetLooseValues() const {
  return loose_values_;
}
std::wstring CommandLine::program() const { return program_; }
#else
std::vector<std::wstring> CommandLine::GetLooseValues() const {
  std::vector<std::wstring> values;
  values.reserve(loose_values_.size());
  for (size_t i = 0; i < loose_values_.size(); ++i)
    values.push_back(ASCIIToWide(loose_values_[i]));
  return values;
}
std::wstring CommandLine::program() const {
  DCHECK(argv_.size() > 0);
  return ASCIIToWide(argv_[0]);
}
#endif

// static
std::wstring CommandLine::PrefixedSwitchString(
    const std::wstring& switch_string) {
  return StringPrintf(L"%ls%ls", kSwitchPrefixes[0], switch_string.c_str());
}

// static
std::wstring CommandLine::PrefixedSwitchStringWithValue(
    const std::wstring& switch_string, const std::wstring& value_string) {
  if (value_string.empty()) {
    return PrefixedSwitchString(switch_string);
  }

  return StringPrintf(L"%ls%ls%ls%ls", kSwitchPrefixes[0],
                      switch_string.c_str(), kSwitchValueSeparator,
                      value_string.c_str());
}

#if defined(XP_WIN)
void CommandLine::AppendSwitch(const std::wstring& switch_string) {
  std::wstring prefixed_switch_string = PrefixedSwitchString(switch_string);
  command_line_string_.append(L" ");
  command_line_string_.append(prefixed_switch_string);
  switches_[WideToASCII(switch_string)] = L"";
}

// Quote a string if necessary, such that CommandLineToArgvW() will
// always process it as a single argument.
static std::wstring WindowsStyleQuote(const std::wstring& arg) {
  // We follow the quoting rules of CommandLineToArgvW.
  // http://msdn.microsoft.com/en-us/library/17w5ykft.aspx
  if (arg.find_first_of(L" \\\"\t") == std::wstring::npos) {
    // No quoting necessary.
    return arg;
  }

  std::wstring out;
  out.push_back(L'"');
  for (size_t i = 0; i < arg.size(); ++i) {
    if (arg[i] == '\\') {
      // Find the extent of this run of backslashes.
      size_t start = i, end = start + 1;
      for (; end < arg.size() && arg[end] == '\\'; ++end) /* empty */
        ;
      size_t backslash_count = end - start;

      // Backslashes are escapes only if the run is followed by a double quote.
      // Since we also will end the string with a double quote, we escape for
      // either a double quote or the end of the string.
      if (end == arg.size() || arg[end] == '"') {
        // To quote, we need to output 2x as many backslashes.
        backslash_count *= 2;
      }
      for (size_t j = 0; j < backslash_count; ++j) out.push_back('\\');

      // Advance i to one before the end to balance i++ in loop.
      i = end - 1;
    } else if (arg[i] == '"') {
      out.push_back('\\');
      out.push_back('"');
    } else {
      out.push_back(arg[i]);
    }
  }
  out.push_back('"');

  return out;
}

void CommandLine::AppendSwitchWithValue(const std::wstring& switch_string,
                                        const std::wstring& value_string) {
  std::wstring quoted_value_string = WindowsStyleQuote(value_string);
  std::wstring combined_switch_string =
      PrefixedSwitchStringWithValue(switch_string, quoted_value_string);

  command_line_string_.append(L" ");
  command_line_string_.append(combined_switch_string);

  switches_[WideToASCII(switch_string)] = value_string;
}

void CommandLine::AppendLooseValue(const std::wstring& value) {
  command_line_string_.append(L" ");
  command_line_string_.append(WindowsStyleQuote(value));
}

void CommandLine::AppendArguments(const CommandLine& other,
                                  bool include_program) {
  // Verify include_program is used correctly.
  // Logic could be shorter but this is clearer.
  DCHECK(include_program ? !other.program().empty() : other.program().empty());
  command_line_string_ += L" " + other.command_line_string_;
  std::map<std::string, StringType>::const_iterator i;
  for (i = other.switches_.begin(); i != other.switches_.end(); ++i)
    switches_[i->first] = i->second;
}

void CommandLine::PrependWrapper(const std::wstring& wrapper) {
  // The wrapper may have embedded arguments (like "gdb --args"). In this case,
  // we don't pretend to do anything fancy, we just split on spaces.
  std::vector<std::wstring> wrapper_and_args;
  SplitString(wrapper, ' ', &wrapper_and_args);
  program_ = wrapper_and_args[0];
  command_line_string_ = wrapper + L" " + command_line_string_;
}

#else
void CommandLine::AppendSwitch(const std::wstring& switch_string) {
  std::string ascii_switch = WideToASCII(switch_string);
  argv_.push_back(kSwitchPrefixes[0] + ascii_switch);
  switches_[ascii_switch] = "";
}

void CommandLine::AppendSwitchWithValue(const std::wstring& switch_string,
                                        const std::wstring& value_string) {
  std::string ascii_switch = WideToASCII(switch_string);
  std::string ascii_value = WideToASCII(value_string);

  argv_.push_back(kSwitchPrefixes[0] + ascii_switch + kSwitchValueSeparator +
                  ascii_value);
  switches_[ascii_switch] = ascii_value;
}

void CommandLine::AppendLooseValue(const std::wstring& value) {
  argv_.push_back(WideToASCII(value));
}

void CommandLine::AppendArguments(const CommandLine& other,
                                  bool include_program) {
  // Verify include_program is used correctly.
  // Logic could be shorter but this is clearer.
  DCHECK(include_program ? !other.program().empty() : other.program().empty());

  size_t first_arg = include_program ? 0 : 1;
  for (size_t i = first_arg; i < other.argv_.size(); ++i)
    argv_.push_back(other.argv_[i]);
  std::map<std::string, StringType>::const_iterator i;
  for (i = other.switches_.begin(); i != other.switches_.end(); ++i)
    switches_[i->first] = i->second;
}

void CommandLine::PrependWrapper(const std::wstring& wrapper_wide) {
  // The wrapper may have embedded arguments (like "gdb --args"). In this case,
  // we don't pretend to do anything fancy, we just split on spaces.
  const std::string wrapper = WideToASCII(wrapper_wide);
  std::vector<std::string> wrapper_and_args;
  SplitString(wrapper, ' ', &wrapper_and_args);
  argv_.insert(argv_.begin(), wrapper_and_args.begin(), wrapper_and_args.end());
}

#endif
