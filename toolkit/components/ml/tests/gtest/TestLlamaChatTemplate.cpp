/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/llama/LlamaRuntimeLinker.h"

#include <iterator>
#include <string>

TEST(LlamaChatTemplate, Gemma4)
{
  auto* lib = mozilla::llama::LlamaRuntimeLinker::Get();
  ASSERT_NE(lib, nullptr);
  const llama_chat_message messages[] = {{"system", "Instructions"},
                                         {"user", "Question"},
                                         {"assistant", "Answer"}};
  const char* expected =
      "<|turn>system\nInstructions<turn|>\n"
      "<|turn>user\nQuestion<turn|>\n"
      "<|turn>model\nAnswer<turn|>\n";
  for (const char* chatTemplate :
       {"gemma4", "{{ '<|turn>' + message['role'] }}"}) {
    for (bool addAssistant : {false, true}) {
      char buffer[512];
      const int32_t length = lib->llama_chat_apply_template(
          chatTemplate, messages, std::size(messages), addAssistant, buffer,
          sizeof(buffer));
      ASSERT_GT(length, 0);
      ASSERT_LT(size_t(length), sizeof(buffer));
      std::string result(buffer, length);
      EXPECT_EQ(result,
                std::string(expected) + (addAssistant ? "<|turn>model\n" : ""));
    }
  }
}
