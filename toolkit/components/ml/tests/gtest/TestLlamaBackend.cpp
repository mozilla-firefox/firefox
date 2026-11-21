/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/llama/LlamaBackend.h"

#include <stdio.h>

using namespace mozilla;
using namespace mozilla::llama;

// TinyStories adds a BOS and prefixes a space, and its vocabulary has no "▁."
// token, so "." becomes [BOS, "▁", "."]: more tokens than the prompt has
// bytes plus one, which is the initial size of the tokenization buffer.
TEST(LlamaBackend, TokenizeRetriesWhenTheBufferIsTooSmall)
{
  FILE* fp = fopen("TinyStories-656K.Q8_0.gguf", "rb");
  ASSERT_TRUE(fp);
  auto closeFp = MakeScopeExit([fp] { fclose(fp); });

  dom::LlamaModelOptions modelOptions;
  modelOptions.mUseMmap = false;
  modelOptions.mContext.mNBatch = 64;
  modelOptions.mContext.mNThreads = 2;
  modelOptions.mContext.mNThreadsBatch = 2;

  RefPtr<LlamaBackend> backend = new LlamaBackend();
  auto init = backend->Reinitialize(modelOptions, fp);
  ASSERT_TRUE(init.isOk())
  << init.inspectErr().mMessage.get();

  dom::LlamaChatOptions chatOptions;
  chatOptions.mPrompt = "."_ns;
  chatOptions.mMaxGeneratedTokens = 1;

  Maybe<size_t> promptTokens;
  auto result = backend->Generate(
      chatOptions,
      [&](const LlamaChatResponse& aResponse) -> ResultStatus {
        if (aResponse.mPhase == LlamaChatPhase::Prompt) {
          promptTokens = Some(aResponse.mTokens.Length());
        }
        return Ok();
      },
      [] { return false; });

  ASSERT_TRUE(result.isOk())
  << result.inspectErr().mMessage.get();
  ASSERT_TRUE(promptTokens.isSome());
  EXPECT_EQ(*promptTokens, 3u);
}
