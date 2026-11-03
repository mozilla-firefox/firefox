/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// On macOS, `mach gtest` sets MOZ_XRE_DIR to <objdir>/dist/bin. The
// HWInference process is sandboxed and may only map executable code from the
// application bundle, so it cannot load libmozinference from there and these
// tests fail. Run them locally with MOZ_DISABLE_UTILITY_SANDBOX=1. CI passes
// --xre-path inside the bundle and runs them sandboxed.

#include "gtest/gtest.h"
#include "mozilla/Preferences.h"
#include "mozilla/Result.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/TextGeneratorBinding.h"
#include "mozilla/gtest/WaitFor.h"
#include "mozilla/gtest/ipc/TestUtilityProcess.h"
#include "mozilla/hwinference/HWInferenceProcess.h"
#include "mozilla/hwinference/HWInferenceParent.h"
#include "mozilla/hwinference/TextGenerationParent.h"
#include "mozilla/ipc/FileDescriptor.h"
#include "mozilla/ipc/UtilityProcessManager.h"
#include "nsIProcessToolsService.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "prio.h"
#include "private/pprio.h"

using namespace mozilla;
using namespace mozilla::hwinference;

namespace {

constexpr char kModelFile[] = "TinyStories-656K.Q8_0.gguf";

constexpr char kGracePref[] =
    "browser.ml.hwinference.browser_idle_shutdown_grace_ms";

// Generous for a debug CI machine; a healthy run needs a fraction of it.
constexpr uint32_t kStepTimeoutSeconds = 120;

ipc::FileDescriptor OpenModelFd(const char* aPath) {
  PRFileDesc* fd = PR_Open(aPath, PR_RDONLY, 0);
  if (!fd) {
    return ipc::FileDescriptor();
  }
  ipc::FileDescriptor result(
      ipc::FileDescriptor::PlatformHandleType(PR_FileDesc2NativeHandle(fd)));
  PR_Close(fd);
  return result;
}

template <typename R, typename E, bool Excl>
Maybe<Result<R, E>> WaitForOrTimeout(RefPtr<MozPromise<R, E, Excl>> aPromise,
                                     uint32_t aTimeoutSeconds) {
  auto result = MakeRefPtr<media::Refcountable<Maybe<Result<R, E>>>>();
  aPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [result](R aResolve) {
        *result = Some(Result<R, E>(std::move(aResolve)));
      },
      [result](E aReject) {
        *result = Some(Result<R, E>(Err(std::move(aReject))));
      });
  TimeStamp deadline =
      TimeStamp::Now() + TimeDuration::FromSeconds(aTimeoutSeconds);
  SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
      "TestTextGeneration WaitForOrTimeout"_ns,
      [&] { return result->isSome() || TimeStamp::Now() > deadline; });
  return std::move(*result);
}

template <typename Condition>
bool SpinUntil(const char* aName, uint32_t aTimeoutSeconds,
               Condition&& aCondition) {
  TimeStamp deadline =
      TimeStamp::Now() + TimeDuration::FromSeconds(aTimeoutSeconds);
  bool held = false;
  SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
      nsDependentCString(aName), [&] {
        held = aCondition();
        return held || TimeStamp::Now() > deadline;
      });
  return held;
}

// Nothing while the process is still launching, as its actor is not bound
// yet, and once it is gone.
Maybe<base::ProcessId> ProcessPid() {
  if (!HWInferenceProcess::Browser().IsUp()) {
    return Nothing();
  }
  RefPtr<HWInferenceParent> actor = HWInferenceProcess::Browser().Actor();
  return actor->CanSend() ? Some(actor->OtherPid()) : Nothing();
}

bool ProcessIsGone() { return !HWInferenceProcess::Browser().IsUp(); }

void ExpectProcessGone(const char* aWhy) {
  EXPECT_TRUE(SpinUntil("Process exit", kStepTimeoutSeconds, ProcessIsGone))
      << aWhy;
}

// Kills the process the way a crash would, and waits for the manager to
// notice.
void Crash(base::ProcessId aPid) {
  nsCOMPtr<nsIProcessToolsService> tools =
      do_GetService("@mozilla.org/processtools-service;1");
  ASSERT_TRUE(tools);
  ASSERT_TRUE(NS_SUCCEEDED(tools->Kill(aPid)));
  ASSERT_TRUE(SpinUntil("Crash noticed", kStepTimeoutSeconds, ProcessIsGone));
}

// top-k:1 is greedy, but llama.cpp needs the chain to end with a dist sampler.
CopyableTArray<Sampler> GreedySamplers() {
  CopyableTArray<Sampler> samplers;
  // WebIDL defaults for the fields each sampler ignores.
  Sampler topK(dom::TextGenerationSamplerType::Top_k, /* topK */ 1,
               /* topP */ 0.95f, /* temp */ 0.80f,
               /* seed */ 0xFFFFFFFF, CopyableTArray<LogitBias>());
  Sampler dist = topK;
  dist.type() = dom::TextGenerationSamplerType::Dist;
  samplers.AppendElement(topK);
  samplers.AppendElement(dist);
  return samplers;
}

GenerateRequest StoryRequest(uint32_t aMaxTokens = 16,
                             uint32_t aBufferLength = 4) {
  GenerateRequest request;
  request.messages().AppendElement(ChatMessage(
      dom::TextGenerationRole::System, "You are a friendly storyteller."_ns));
  request.messages().AppendElement(
      ChatMessage(dom::TextGenerationRole::User,
                  "Once upon a time there was a small mouse who"_ns));
  request.maxTokens() = aMaxTokens;
  request.bufferLength() = aBufferLength;
  request.samplers() = GreedySamplers();
  request.stopOnEndOfGenerationTokens() = true;
  return request;
}

struct Session {
  RefPtr<TextGenerationParent> mGenerator;
  nsCString mStreamed;
};

// One fd per generator: sending a FileDescriptor consumes its handle.
Session CreateGenerator(const ipc::FileDescriptor& aModel) {
  Session session;
  session.mGenerator = TextGenerationParent::Create(
      aModel,
      TextGenerationOptions(512, 2, 2, 2048, 512,
                            dom::TextGenerationKVCacheDtype::F16, false));
  if (!session.mGenerator) {
    printf_stderr("CreateGenerator: TextGenerationParent::Create() failed\n");
  }
  return session;
}

Session CreateGenerator() { return CreateGenerator(OpenModelFd(kModelFile)); }

// Some() carries the load failure; Nothing() means the model loaded.
Maybe<TextGenerationParent::LoadFailure> WaitForReady(Session& aSession) {
  auto ready =
      WaitForOrTimeout(aSession.mGenerator->WhenReady(), kStepTimeoutSeconds);
  EXPECT_TRUE(ready.isSome()) << "Ready timed out";
  if (ready.isNothing()) {
    return Some(TextGenerationParent::LoadFailure{
        TextGenerationParent::LoadFailure::Cause::ActorGone,
        "Ready timed out"_ns});
  }
  if (ready->isOk()) {
    return Nothing();
  }
  return Some(ready->unwrapErr());
}

auto StartGenerate(Session& aSession, const GenerateRequest& aRequest) {
  aSession.mGenerator->SetDeltaHandler(
      [session = &aSession](const nsCString& aText) {
        session->mStreamed.Append(aText);
      });
  return aSession.mGenerator->SendGenerate(aRequest);
}

// aErrorExpected only suppresses the diagnostic printf.
GenerateResponse Generate(Session& aSession, const GenerateRequest& aRequest,
                          bool aErrorExpected = false) {
  auto result =
      WaitForOrTimeout(StartGenerate(aSession, aRequest), kStepTimeoutSeconds);
  EXPECT_TRUE(result.isSome()) << "Generate timed out";
  if (result.isNothing()) {
    return GenerateResponse();
  }
  EXPECT_TRUE(result->isOk());
  GenerateResponse response =
      result->isOk() ? result->unwrap() : GenerateResponse();
  if (!aErrorExpected && response.type() == GenerateResponse::TGenerateError) {
    printf_stderr("GenerateError: %s\n",
                  response.get_GenerateError().message().get());
  }
  return response;
}

void Teardown(Session& aSession) {
  if (aSession.mGenerator) {
    aSession.mGenerator->Close();
  }
  ExpectProcessGone("last generator gone but the HWInference process survived");
}

}  // namespace

class TextGenerationTest : public mozilla::gtest::ipc::TestUtilityProcess {
 protected:
  // Zero grace: Teardown() asserts the process exits immediately.
  void SetUp() override { Preferences::SetUint(kGracePref, 0); }
  void TearDown() override { Preferences::ClearUser(kGracePref); }
};

TEST_F(TextGenerationTest, GenerateFromModel) {
  Session session = CreateGenerator();
  ASSERT_TRUE(session.mGenerator);
  EXPECT_FALSE(session.mGenerator->ProcessReused());
  EXPECT_TRUE(WaitForReady(session).isNothing())
      << "construction should load the model";

  GenerateResponse response = Generate(session, StoryRequest());
  ASSERT_EQ(response.type(), GenerateResponse::TGenerateResult);
  const GenerateResult& result = response.get_GenerateResult();

  EXPECT_FALSE(result.content().IsEmpty());
  EXPECT_EQ(session.mStreamed, result.content());
  EXPECT_GT(result.usage().promptTokens(), 0u);
  EXPECT_GT(result.usage().generatedTokens(), 0u);
  EXPECT_GT(result.usage().promptCharacters(), result.usage().promptTokens());
  EXPECT_TRUE(result.reason() == dom::TextGenerationFinishReason::Length ||
              result.reason() == dom::TextGenerationFinishReason::Eos);

  EXPECT_GE(result.resources().after().cpuTimeMs(),
            result.resources().before().cpuTimeMs());
  EXPECT_GT(result.resources().after().memoryBytes(), 0u);

  Teardown(session);
}

TEST_F(TextGenerationTest, ClearResetsHistory) {
  Session session = CreateGenerator();
  ASSERT_TRUE(session.mGenerator);
  EXPECT_TRUE(WaitForReady(session).isNothing())
      << "construction should load the model";

  GenerateResponse first = Generate(session, StoryRequest());
  ASSERT_EQ(first.type(), GenerateResponse::TGenerateResult);

  session.mStreamed.Truncate();
  GenerateResponse appended = Generate(session, StoryRequest());
  ASSERT_EQ(appended.type(), GenerateResponse::TGenerateResult);
  EXPECT_GT(appended.get_GenerateResult().usage().promptTokens(),
            first.get_GenerateResult().usage().promptTokens())
      << "second Generate should prefill the accumulated history";

  // Fire-and-forget; in-order delivery lands it before the next Generate.
  EXPECT_TRUE(session.mGenerator->SendClear());
  session.mStreamed.Truncate();
  GenerateResponse cleared = Generate(session, StoryRequest());
  ASSERT_EQ(cleared.type(), GenerateResponse::TGenerateResult);
  EXPECT_EQ(cleared.get_GenerateResult().usage().promptTokens(),
            first.get_GenerateResult().usage().promptTokens());
  EXPECT_EQ(cleared.get_GenerateResult().usage().promptCharacters(),
            first.get_GenerateResult().usage().promptCharacters());
#if defined(XP_MACOSX) && defined(__x86_64__)
  // Bug 2047025: greedy decode is not byte-stable on mac x64.
  EXPECT_FALSE(cleared.get_GenerateResult().content().IsEmpty());
#else
  EXPECT_EQ(cleared.get_GenerateResult().content(),
            first.get_GenerateResult().content())
      << "greedy generation after Clear should reproduce the first run";
#endif

  Teardown(session);
}

TEST_F(TextGenerationTest, ZeroTokenResultCommitsHistory) {
  Session session = CreateGenerator();
  ASSERT_TRUE(session.mGenerator);
  ASSERT_TRUE(WaitForReady(session).isNothing());

  GenerateResponse first = Generate(session, StoryRequest(1));
  ASSERT_EQ(first.type(), GenerateResponse::TGenerateResult);
  ASSERT_TRUE(session.mGenerator->SendClear());

  GenerateResponse zero = Generate(session, StoryRequest(0));
  ASSERT_EQ(zero.type(), GenerateResponse::TGenerateResult);
  EXPECT_EQ(zero.get_GenerateResult().usage().generatedTokens(), 0u);

  GenerateRequest next = StoryRequest(1);
  next.messages().Clear();
  GenerateResponse continued = Generate(session, next);
  ASSERT_EQ(continued.type(), GenerateResponse::TGenerateResult);
  EXPECT_EQ(continued.get_GenerateResult().usage().promptTokens(),
            first.get_GenerateResult().usage().promptTokens());
  EXPECT_EQ(continued.get_GenerateResult().usage().promptCharacters(),
            first.get_GenerateResult().usage().promptCharacters());
  EXPECT_GT(continued.get_GenerateResult().usage().generatedTokens(), 0u);

  Teardown(session);
}

TEST_F(TextGenerationTest, CancelResolvesWithPartialResult) {
  Session session = CreateGenerator();
  ASSERT_TRUE(session.mGenerator);
  EXPECT_TRUE(WaitForReady(session).isNothing());

  auto pending = StartGenerate(
      session, StoryRequest(/* aMaxTokens */ 512, /* aBufferLength */ 1));
  ASSERT_TRUE(SpinUntil("first delta", kStepTimeoutSeconds,
                        [&] { return !session.mStreamed.IsEmpty(); }));
  EXPECT_TRUE(session.mGenerator->SendCancel());

  auto result = WaitForOrTimeout(std::move(pending), kStepTimeoutSeconds);
  ASSERT_TRUE(result.isSome())
  << "cancelled Generate never resolved";
  ASSERT_TRUE(result->isOk());
  GenerateResponse response = result->unwrap();
  ASSERT_EQ(response.type(), GenerateResponse::TGenerateResult);
  const GenerateResult& generated = response.get_GenerateResult();
  EXPECT_EQ(generated.reason(), dom::TextGenerationFinishReason::Cancelled);
  EXPECT_EQ(session.mStreamed, generated.content());
  EXPECT_LT(generated.usage().generatedTokens(), 512u)
      << "cancel should have stopped generation early";

  GenerateRequest next = StoryRequest(1);
  next.messages().Clear();
  GenerateResponse continued = Generate(session, next);
  ASSERT_EQ(continued.type(), GenerateResponse::TGenerateResult);
  EXPECT_EQ(continued.get_GenerateResult().usage().promptTokens(),
            generated.usage().promptTokens());
  EXPECT_EQ(continued.get_GenerateResult().usage().promptCharacters(),
            generated.usage().promptCharacters());

  Teardown(session);
}

TEST_F(TextGenerationTest, WhenReadyAnswersAfterTheLoadReported) {
  Session session = CreateGenerator();
  ASSERT_TRUE(session.mGenerator);

  ASSERT_TRUE(WaitForReady(session).isNothing())
  << "construction should load the model";

  ASSERT_TRUE(WaitForReady(session).isNothing())
  << "a late WhenReady() must still see the successful load";

  Teardown(session);
}

TEST_F(TextGenerationTest, InvalidFdFailsAtConstruction) {
  Session session = CreateGenerator(ipc::FileDescriptor());
  ASSERT_TRUE(session.mGenerator);

  Maybe<TextGenerationParent::LoadFailure> loadError = WaitForReady(session);
  ASSERT_TRUE(loadError.isSome());
  EXPECT_EQ(loadError->cause,
            TextGenerationParent::LoadFailure::Cause::Backend);
  EXPECT_FALSE(loadError->message.IsEmpty());

  GenerateResponse response =
      Generate(session, StoryRequest(), /* aErrorExpected */ true);
  ASSERT_EQ(response.type(), GenerateResponse::TGenerateError);
  EXPECT_FALSE(response.get_GenerateError().message().IsEmpty());

  Teardown(session);
}

TEST_F(TextGenerationTest, CloseBeforeReadyRetiresTheProcess) {
  Session session = CreateGenerator();
  ASSERT_TRUE(session.mGenerator);

  session.mGenerator->Close();
  Maybe<TextGenerationParent::LoadFailure> loadError = WaitForReady(session);
  ASSERT_TRUE(loadError.isSome());
  EXPECT_EQ(loadError->cause,
            TextGenerationParent::LoadFailure::Cause::ActorGone);
  ExpectProcessGone("a generator closed before ready pinned the process");
}

// Bug 2069379: a failed launch does not reach the generator yet.
TEST_F(TextGenerationTest, DISABLED_LaunchFailureEndsTheGenerator) {
  auto manager = ipc::UtilityProcessManager::GetSingleton();
  ASSERT_TRUE(manager);

  Session session = CreateGenerator();
  ASSERT_TRUE(session.mGenerator);
  // Nothing spins the event loop in between, so the launch is still pending
  // and this fails it.
  manager->CleanShutdownAllProcesses();

  Maybe<TextGenerationParent::LoadFailure> loadError = WaitForReady(session);
  ASSERT_TRUE(loadError.isSome())
  << "a generator on a failed launch must report it";
  EXPECT_EQ(loadError->cause,
            TextGenerationParent::LoadFailure::Cause::ActorGone);
  EXPECT_FALSE(session.mGenerator->CanSend());
  session.mGenerator = nullptr;

  Session next = CreateGenerator();
  ASSERT_TRUE(next.mGenerator);
  EXPECT_FALSE(next.mGenerator->ProcessReused());
  EXPECT_TRUE(WaitForReady(next).isNothing())
      << "a fresh launch must follow a failed one";
  Teardown(next);

  NS_ProcessPendingEvents(nullptr);
}

TEST_F(TextGenerationTest, ProcessDeathDuringGenerateRejects) {
  Session session = CreateGenerator();
  ASSERT_TRUE(session.mGenerator);
  EXPECT_TRUE(WaitForReady(session).isNothing());
  Maybe<base::ProcessId> pid = ProcessPid();
  ASSERT_TRUE(pid.isSome());

  auto pending = StartGenerate(
      session, StoryRequest(/* aMaxTokens */ 4096, /* aBufferLength */ 1));
  ASSERT_TRUE(SpinUntil("first delta", kStepTimeoutSeconds,
                        [&] { return !session.mStreamed.IsEmpty(); }));
  Crash(*pid);

  auto result = WaitForOrTimeout(std::move(pending), kStepTimeoutSeconds);
  ASSERT_TRUE(result.isSome())
  << "Generate never settled after the crash";
  EXPECT_TRUE(result->isErr()) << "Generate must reject with its process";
  EXPECT_FALSE(session.mGenerator->CanSend());

  Session next = CreateGenerator();
  ASSERT_TRUE(next.mGenerator);
  EXPECT_FALSE(next.mGenerator->ProcessReused());
  EXPECT_TRUE(WaitForReady(next).isNothing());
  Maybe<base::ProcessId> newPid = ProcessPid();
  ASSERT_TRUE(newPid.isSome());
  EXPECT_NE(*newPid, *pid);

  session.mGenerator = nullptr;
  EXPECT_EQ(ProcessPid(), newPid)
      << "a generator of the dead process must not touch its replacement";
  Teardown(next);
}

TEST_F(TextGenerationTest, IdleGraceKeepsProcessForReuse) {
  constexpr uint32_t kGraceMs = 2000;
  Preferences::SetUint(kGracePref, kGraceMs);
  Session first = CreateGenerator();
  ASSERT_TRUE(first.mGenerator);
  ASSERT_TRUE(WaitForReady(first).isNothing());
  const auto pid = ProcessPid();
  ASSERT_TRUE(pid.isSome());
  first.mGenerator->Close();

  Session second = CreateGenerator();
  ASSERT_TRUE(second.mGenerator);
  ASSERT_TRUE(WaitForReady(second).isNothing());
  EXPECT_TRUE(second.mGenerator->ProcessReused());
  EXPECT_EQ(ProcessPid(), pid);

  const TimeStamp released = TimeStamp::Now();
  second.mGenerator->Close();
  ExpectProcessGone("process survived the last generator's idle grace");
  EXPECT_GE((TimeStamp::Now() - released).ToMilliseconds(), kGraceMs * 0.8);
}
