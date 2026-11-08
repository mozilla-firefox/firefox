/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/Preferences.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "nsITimer.h"
#include "nsTArray.h"
#include "nsIObserver.h"
#include "nsThreadUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsWeakReference.h"

using namespace mozilla;

// ---------------------------------------------------------------------------
// Helpers shared by the CallbackTrie tests below.
// ---------------------------------------------------------------------------
namespace {

struct CallbackOrder {
  nsTArray<int>* order;
  int id;
};

void TrackOrder(const char*, void* aData) {
  auto* d = static_cast<CallbackOrder*>(aData);
  d->order->AppendElement(d->id);
}

void IncrementCount(const char*, void* aData) { (*static_cast<int*>(aData))++; }

}  // namespace

class TestWeakPrefObserver final : public nsIObserver,
                                   public nsSupportsWeakReference {
 public:
  NS_DECL_ISUPPORTS
  NS_IMETHOD Observe(nsISupports* aSubject, const char* aTopic,
                     const char16_t* aData) override {
    mNotifyCount++;
    return NS_OK;
  }
  int mNotifyCount = 0;

 private:
  ~TestWeakPrefObserver() = default;
};

NS_IMPL_ISUPPORTS(TestWeakPrefObserver, nsIObserver, nsISupportsWeakReference)

// Like TestWeakPrefObserver, but also records the root-relative pref name that
// nsPrefBranch::NotifyObserver hands to the observer.
class TestPrefBranchObserver final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_IMETHOD Observe(nsISupports* aSubject, const char* aTopic,
                     const char16_t* aData) override {
    mNotifyCount++;
    mLastData.Assign(aData);
    return NS_OK;
  }
  int mNotifyCount = 0;
  nsString mLastData;

 private:
  ~TestPrefBranchObserver() = default;
};

NS_IMPL_ISUPPORTS(TestPrefBranchObserver, nsIObserver)

TEST(PrefsBasics, Errors)
{
  Preferences::SetBool("foo.bool", true, PrefValueKind::Default);
  Preferences::SetBool("foo.bool", false, PrefValueKind::User);
  ASSERT_EQ(Preferences::GetBool("foo.bool", false, PrefValueKind::Default),
            true);
  ASSERT_EQ(Preferences::GetBool("foo.bool", true, PrefValueKind::User), false);

  Preferences::SetInt("foo.int", -66, PrefValueKind::Default);
  Preferences::SetInt("foo.int", -77, PrefValueKind::User);
  ASSERT_EQ(Preferences::GetInt("foo.int", 1, PrefValueKind::Default), -66);
  ASSERT_EQ(Preferences::GetInt("foo.int", 1, PrefValueKind::User), -77);

  Preferences::SetUint("foo.uint", 88, PrefValueKind::Default);
  Preferences::SetUint("foo.uint", 99, PrefValueKind::User);
  ASSERT_EQ(Preferences::GetUint("foo.uint", 1, PrefValueKind::Default), 88U);
  ASSERT_EQ(Preferences::GetUint("foo.uint", 1, PrefValueKind::User), 99U);

  Preferences::SetFloat("foo.float", 3.33f, PrefValueKind::Default);
  Preferences::SetFloat("foo.float", 4.44f, PrefValueKind::User);
  ASSERT_FLOAT_EQ(
      Preferences::GetFloat("foo.float", 1.0f, PrefValueKind::Default), 3.33f);
  ASSERT_FLOAT_EQ(Preferences::GetFloat("foo.float", 1.0f, PrefValueKind::User),
                  4.44f);
}

TEST(PrefsBasics, Serialize)
{
  // Ensure that at least this one preference exists
  Preferences::SetBool("foo.bool", true, PrefValueKind::Default);
  ASSERT_EQ(Preferences::GetBool("foo.bool", false, PrefValueKind::Default),
            true);

  nsCString str;
  Preferences::SerializePreferences(str, true);
  fprintf(stderr, "%s\n", str.get());
  // Assert that some prefs were not sanitized
  ASSERT_NE(nullptr, strstr(str.get(), "B--:"));
  ASSERT_NE(nullptr, strstr(str.get(), "I--:"));
  ASSERT_NE(nullptr, strstr(str.get(), "S--:"));
  // Assert that something was sanitized
  ASSERT_NE(
      nullptr,
      strstr(
          str.get(),
          "I-S:56/datareporting.policy.dataSubmissionPolicyAcceptedVersion"));
}

TEST(PrefsBasics, WeakObserverIdleSweep)
{
  // In gtest there is no RefreshDriver to drive idle scheduling and we don't
  // want to wait for the maximum idle delay. A repeating timer provides enough
  // main-thread activity for the idle task machinery to find idle time.
  nsCOMPtr<nsITimer> keepAlive = NS_NewTimer();
  keepAlive->InitWithNamedFuncCallback(
      [](nsITimer*, void*) {}, nullptr, 16, nsITimer::TYPE_REPEATING_SLACK,
      "PrefsBasics.WeakObserverIdleSweep.keepAlive"_ns);

  // Drain any startup-triggered sweep runner before we begin.
  TimeStamp drainDeadline =
      TimeStamp::Now() + TimeDuration::FromMilliseconds(100);
  MOZ_ALWAYS_TRUE(
      SpinEventLoopUntil("PrefsBasics.WeakObserverIdleSweep.drain"_ns,
                         [&] { return TimeStamp::Now() >= drainDeadline; }));
  NS_ProcessPendingEvents(nullptr);

  static const char kPref[] = "test.weak.observer.sweep";
  Preferences::SetBool(kPref, false);

  uint32_t countWithObserver;
  {
    RefPtr<TestWeakPrefObserver> observer = new TestWeakPrefObserver();
    nsresult rv = Preferences::AddWeakObserver(observer, kPref);
    ASSERT_TRUE(NS_SUCCEEDED(rv));
    countWithObserver = Preferences::GetCallbackCount();
  }

  // Observer expired, but no pref change — callback is still in the list.
  EXPECT_EQ(Preferences::GetCallbackCount(), countWithObserver);

  // Changing the pref notifies the expired observer, scheduling an idle sweep.
  Preferences::SetBool(kPref, true);

  // Spin the event loop until the idle sweep runs and removes the callback.
  MOZ_ALWAYS_TRUE(SpinEventLoopUntil(
      "PrefsBasics.WeakObserverIdleSweep"_ns,
      [&] { return Preferences::GetCallbackCount() < countWithObserver; }));

  keepAlive->Cancel();
}

TEST(PrefsBasics, WeakObserverRegistrationSweep)
{
  // In gtest there is no RefreshDriver to drive idle scheduling and we don't
  // want to wait for the maximum idle delay. A repeating timer provides enough
  // main-thread activity for the idle task machinery to find idle time.
  nsCOMPtr<nsITimer> keepAlive = NS_NewTimer();
  keepAlive->InitWithNamedFuncCallback(
      [](nsITimer*, void*) {}, nullptr, 16, nsITimer::TYPE_REPEATING_SLACK,
      "PrefsBasics.WeakObserverRegistrationSweep.keepAlive"_ns);

  // Drain any pending sweep runner before we begin.
  TimeStamp drainDeadline =
      TimeStamp::Now() + TimeDuration::FromMilliseconds(100);
  MOZ_ALWAYS_TRUE(
      SpinEventLoopUntil("PrefsBasics.WeakObserverRegistrationSweep.drain"_ns,
                         [&] { return TimeStamp::Now() >= drainDeadline; }));
  NS_ProcessPendingEvents(nullptr);

  static const char kPref[] = "test.weak.observer.regsweep";
  Preferences::SetBool(kPref, false);

  uint32_t countWithObserver;
  {
    RefPtr<TestWeakPrefObserver> observer = new TestWeakPrefObserver();
    nsresult rv = Preferences::AddWeakObserver(observer, kPref);
    ASSERT_TRUE(NS_SUCCEEDED(rv));
    countWithObserver = Preferences::GetCallbackCount();
  }

  // Observer expired, but callback is still in the list.
  EXPECT_EQ(Preferences::GetCallbackCount(), countWithObserver);

  // Register 512 weak observers to hit the periodic sweep threshold.
  // Keep them alive during registration to avoid address reuse causing
  // duplicate keys in the observer hashtable.
  static constexpr uint32_t kSweepInterval = 512;
  nsTArray<RefPtr<TestWeakPrefObserver>> observers(kSweepInterval);
  for (uint32_t i = 0; i < kSweepInterval; i++) {
    observers.AppendElement(new TestWeakPrefObserver());
    Preferences::AddWeakObserver(observers.LastElement(), kPref);
  }
  EXPECT_EQ(Preferences::GetCallbackCount(),
            countWithObserver + kSweepInterval);

  // Let all observers expire at once.
  observers.Clear();

  // Spin the event loop until the sweep removes all expired callbacks.
  MOZ_ALWAYS_TRUE(SpinEventLoopUntil(
      "PrefsBasics.WeakObserverRegistrationSweep"_ns,
      [&] { return Preferences::GetCallbackCount() < countWithObserver; }));

  keepAlive->Cancel();
}

TEST(PrefsBasics, FreeObserverListRemovesAllCallbacks)
{
  Preferences::SetBool("test.free.a.pref", false);
  Preferences::SetBool("test.free.b.pref", false);

  uint32_t baselineCount = Preferences::GetCallbackCount();

  nsCOMPtr<nsIPrefService> prefService =
      do_GetService(NS_PREFSERVICE_CONTRACTID);
  ASSERT_TRUE(prefService);

  nsCOMPtr<nsIPrefBranch> branchA;
  nsresult rv = prefService->GetBranch("test.free.a.", getter_AddRefs(branchA));
  ASSERT_TRUE(NS_SUCCEEDED(rv));

  nsCOMPtr<nsIPrefBranch> branchB;
  rv = prefService->GetBranch("test.free.b.", getter_AddRefs(branchB));
  ASSERT_TRUE(NS_SUCCEEDED(rv));

  RefPtr<TestWeakPrefObserver> obs1 = new TestWeakPrefObserver();
  RefPtr<TestWeakPrefObserver> obs2 = new TestWeakPrefObserver();
  RefPtr<TestWeakPrefObserver> obs3 = new TestWeakPrefObserver();

  // Interleave observer registration across the two branches.
  rv = branchA->AddObserver("pref", obs1, false);
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  rv = branchB->AddObserver("pref", obs2, false);
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  rv = branchA->AddObserver("pref", obs3, false);
  ASSERT_TRUE(NS_SUCCEEDED(rv));
  EXPECT_EQ(Preferences::GetCallbackCount(), baselineCount + 3);

  // Releasing branchA should only remove its two callbacks.
  branchA = nullptr;
  EXPECT_EQ(Preferences::GetCallbackCount(), baselineCount + 1);

  // Releasing branchB removes the remaining one.
  branchB = nullptr;
  EXPECT_EQ(Preferences::GetCallbackCount(), baselineCount);
}

// ---------------------------------------------------------------------------
// CallbackTrie tests — verify HashedSegmentsTrie notification semantics.
// ---------------------------------------------------------------------------

// Callback at the exact pref name fires when that pref changes.
TEST(PrefsCallbackTrie, ExactMatch)
{
  int count = 0;
  Preferences::SetBool("test.trie.exact", false);
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterCallback(
      IncrementCount, "test.trie.exact", &count)));
  Preferences::SetBool("test.trie.exact", true);
  EXPECT_EQ(count, 1);
  Preferences::UnregisterCallback(IncrementCount, "test.trie.exact", &count);
}

// Prefix callback at an ancestor fires when a deeper descendant pref changes.
TEST(PrefsCallbackTrie, PrefixAncestorFiresForDescendant)
{
  int count = 0;
  Preferences::SetBool("test.trie.anc.deep.pref", false);
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterPrefixCallback(
      IncrementCount, "test.trie.anc"_ns, &count)));
  Preferences::SetBool("test.trie.anc.deep.pref", true);
  EXPECT_EQ(count, 1);
  Preferences::UnregisterPrefixCallback(IncrementCount, "test.trie.anc"_ns,
                                        &count);
}

// Exact callback at an ancestor does NOT fire when a descendant pref changes.
TEST(PrefsCallbackTrie, ExactAncestorDoesNotFireForDescendant)
{
  int count = 0;
  Preferences::SetBool("test.trie.anc.exact.pref", false);
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterCallback(
      IncrementCount, "test.trie.anc.exact"_ns, &count)));
  Preferences::SetBool("test.trie.anc.exact.pref", true);
  EXPECT_EQ(count, 0);
  Preferences::UnregisterCallback(IncrementCount, "test.trie.anc.exact"_ns,
                                  &count);
}

// Callback does NOT fire for a sibling pref (different child at the same
// depth).
TEST(PrefsCallbackTrie, NoFireForSibling)
{
  int count = 0;
  Preferences::SetBool("test.trie.sib.b", false);
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterCallback(
      IncrementCount, "test.trie.sib.a", &count)));
  Preferences::SetBool("test.trie.sib.b", true);
  EXPECT_EQ(count, 0);
  Preferences::UnregisterCallback(IncrementCount, "test.trie.sib.a", &count);
}

// Callback does NOT fire for a pref whose name shares a prefix at the byte
// level but is a different dot-separated segment ("ab" vs "abc").
TEST(PrefsCallbackTrie, NoFireForPrefixSubstring)
{
  int count = 0;
  Preferences::SetBool("test.trie.seg.abc", false);
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterCallback(
      IncrementCount, "test.trie.seg.ab", &count)));
  Preferences::SetBool("test.trie.seg.abc", true);
  EXPECT_EQ(count, 0);
  Preferences::UnregisterCallback(IncrementCount, "test.trie.seg.ab", &count);
}

// Prefix callbacks at ancestor nodes fire before callbacks at deeper nodes.
TEST(PrefsCallbackTrie, AncestorBeforeDescendantOrder)
{
  nsTArray<int> order;
  CallbackOrder dataA{&order, 1};
  CallbackOrder dataAB{&order, 2};
  CallbackOrder dataABC{&order, 3};

  Preferences::SetBool("test.trie.order.a.b.c", false);
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterPrefixCallback(
      TrackOrder, "test.trie.order.a"_ns, &dataA)));
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterPrefixCallback(
      TrackOrder, "test.trie.order.a.b"_ns, &dataAB)));
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterCallback(
      TrackOrder, "test.trie.order.a.b.c"_ns, &dataABC)));

  Preferences::SetBool("test.trie.order.a.b.c", true);
  ASSERT_EQ(order.Length(), 3u);
  EXPECT_EQ(order[0], 1);
  EXPECT_EQ(order[1], 2);
  EXPECT_EQ(order[2], 3);

  Preferences::UnregisterPrefixCallback(TrackOrder, "test.trie.order.a"_ns,
                                        &dataA);
  Preferences::UnregisterPrefixCallback(TrackOrder, "test.trie.order.a.b"_ns,
                                        &dataAB);
  Preferences::UnregisterCallback(TrackOrder, "test.trie.order.a.b.c"_ns,
                                  &dataABC);
}

// Within a single trie node, callbacks fire in LIFO order (most recently
// registered fires first).
TEST(PrefsCallbackTrie, LIFOWithinNode)
{
  nsTArray<int> order;
  CallbackOrder data1{&order, 1};
  CallbackOrder data2{&order, 2};

  Preferences::SetBool("test.trie.lifo", false);
  ASSERT_TRUE(NS_SUCCEEDED(
      Preferences::RegisterCallback(TrackOrder, "test.trie.lifo", &data1)));
  ASSERT_TRUE(NS_SUCCEEDED(
      Preferences::RegisterCallback(TrackOrder, "test.trie.lifo", &data2)));

  Preferences::SetBool("test.trie.lifo", true);
  ASSERT_EQ(order.Length(), 2u);
  EXPECT_EQ(order[0], 2);
  EXPECT_EQ(order[1], 1);

  Preferences::UnregisterCallback(TrackOrder, "test.trie.lifo", &data1);
  Preferences::UnregisterCallback(TrackOrder, "test.trie.lifo", &data2);
}

// Even when a leaf callback is registered before its ancestor prefix callback,
// the ancestor still fires first (trie depth determines order, not creation
// order).
TEST(PrefsCallbackTrie, LeafRegisteredBeforeAncestor)
{
  nsTArray<int> order;
  CallbackOrder dataLeaf{&order, 2};
  CallbackOrder dataAncestor{&order, 1};

  Preferences::SetBool("test.trie.corder.a.b.c", false);
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterCallback(
      TrackOrder, "test.trie.corder.a.b.c"_ns, &dataLeaf)));
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterPrefixCallback(
      TrackOrder, "test.trie.corder.a"_ns, &dataAncestor)));

  Preferences::SetBool("test.trie.corder.a.b.c", true);
  ASSERT_EQ(order.Length(), 2u);
  EXPECT_EQ(order[0], 1);
  EXPECT_EQ(order[1], 2);

  Preferences::UnregisterCallback(TrackOrder, "test.trie.corder.a.b.c"_ns,
                                  &dataLeaf);
  Preferences::UnregisterPrefixCallback(TrackOrder, "test.trie.corder.a"_ns,
                                        &dataAncestor);
}

// A domain registered with a trailing dot is equivalent to one without.
TEST(PrefsCallbackTrie, TrailingDotEquivalence)
{
  int count = 0;
  Preferences::SetBool("test.trie.dot.a.b", false);
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterPrefixCallback(
      IncrementCount, "test.trie.dot.a."_ns, &count)));
  Preferences::SetBool("test.trie.dot.a.b", true);
  EXPECT_EQ(count, 1);
  // Unregistering without the trailing dot should remove the same node.
  Preferences::UnregisterPrefixCallback(IncrementCount, "test.trie.dot.a"_ns,
                                        &count);
  Preferences::SetBool("test.trie.dot.a.b", false);
  EXPECT_EQ(count, 1);
}

// A domain ending in more than one dot is rejected rather than partially
// normalized.
TEST(PrefsCallbackTrie, MultipleTrailingDotsRejected)
{
  int count = 0;
  EXPECT_EQ(Preferences::RegisterCallback(IncrementCount,
                                          "test.trie.dots.a.."_ns, &count),
            NS_ERROR_INVALID_ARG);
  EXPECT_EQ(Preferences::RegisterPrefixCallback(
                IncrementCount, "test.trie.dots.a.."_ns, &count),
            NS_ERROR_INVALID_ARG);
  EXPECT_EQ(Preferences::UnregisterCallback(IncrementCount,
                                            "test.trie.dots.a.."_ns, &count),
            NS_ERROR_INVALID_ARG);

  nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
  ASSERT_TRUE(prefs);
  RefPtr<TestWeakPrefObserver> observer = new TestWeakPrefObserver();
  EXPECT_EQ(prefs->AddObserver("test.trie.dots.d.."_ns, observer, false),
            NS_ERROR_INVALID_ARG);
  Preferences::SetBool("test.trie.dots.d.e", true);
  EXPECT_EQ(observer->mNotifyCount, 0);
}

// A domain registered with a trailing dot covers the subtree below it, not the
// bare stem pref of the same name: "test.trie.stem.a." must not fire for the
// pref "test.trie.stem.a", whose name is shorter than the registered domain.
TEST(PrefsCallbackTrie, TrailingDotDomainSkipsStemPref)
{
  int count = 0;
  Preferences::SetBool("test.trie.stem.a", false);
  Preferences::SetBool("test.trie.stem.a.b", false);
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterPrefixCallback(
      IncrementCount, "test.trie.stem.a."_ns, &count)));
  Preferences::SetBool("test.trie.stem.a", true);
  EXPECT_EQ(count, 0);
  Preferences::SetBool("test.trie.stem.a.b", true);
  EXPECT_EQ(count, 1);
  Preferences::UnregisterPrefixCallback(IncrementCount, "test.trie.stem.a."_ns,
                                        &count);
}

// Same for an array registration mixing a trailing-dot domain with a bare one:
// the trailing-dot entry skips its stem while the bare entry still matches
// exactly.
TEST(PrefsCallbackTrie, TrailingDotDomainSkipsStemPrefInArray)
{
  static const char* const kPrefs[] = {"test.trie.stemarr.a.",
                                       "test.trie.stemarr.b", nullptr};
  int count = 0;
  Preferences::SetBool("test.trie.stemarr.a", false);
  Preferences::SetBool("test.trie.stemarr.a.b", false);
  Preferences::SetBool("test.trie.stemarr.b", false);
  ASSERT_TRUE(NS_SUCCEEDED(
      Preferences::RegisterPrefixCallbacks(IncrementCount, kPrefs, &count)));
  Preferences::SetBool("test.trie.stemarr.a", true);
  EXPECT_EQ(count, 0);
  Preferences::SetBool("test.trie.stemarr.a.b", true);
  EXPECT_EQ(count, 1);
  Preferences::SetBool("test.trie.stemarr.b", true);
  EXPECT_EQ(count, 2);
  Preferences::UnregisterPrefixCallbacks(IncrementCount, kPrefs, &count);
}

// A pref-branch observer is handed pref names relative to its branch root, so
// the stem pref of the root has no representation in that namespace and must
// not be reported: nsPrefBranch::NotifyObserver would otherwise offset past the
// end of the shorter name.
TEST(PrefsCallbackTrie, BranchObserverSkipsStemPref)
{
  nsCOMPtr<nsIPrefService> prefService =
      do_GetService(NS_PREFSERVICE_CONTRACTID);
  ASSERT_TRUE(prefService);

  nsCOMPtr<nsIPrefBranch> branch;
  ASSERT_TRUE(NS_SUCCEEDED(
      prefService->GetBranch("test.branch.stem.", getter_AddRefs(branch))));

  RefPtr<TestPrefBranchObserver> obs = new TestPrefBranchObserver();
  ASSERT_TRUE(NS_SUCCEEDED(branch->AddObserver("", obs, false)));

  Preferences::SetBool("test.branch.stem", true);
  EXPECT_EQ(obs->mNotifyCount, 0);

  Preferences::SetBool("test.branch.stem.child", true);
  EXPECT_EQ(obs->mNotifyCount, 1);
  EXPECT_TRUE(obs->mLastData.EqualsLiteral("child"));

  branch->RemoveObserver("", obs);
}

// ---------------------------------------------------------------------------
// Compact / MarkDead tests — verify lazy-unregister behavior.
// ---------------------------------------------------------------------------

// Unregistering a callback marks it dead immediately; a pref change before
// the idle sweep does not fire the dead callback but does fire live siblings
// registered at the same path.
TEST(PrefsCallbackTrie, DeadCallbackSkippedBeforeSweep)
{
  int deadCount = 0;
  int liveCount = 0;

  Preferences::SetBool("test.trie.dead1", false);
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterCallback(
      IncrementCount, "test.trie.dead1", &deadCount)));
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterCallback(
      IncrementCount, "test.trie.dead1", &liveCount)));

  Preferences::UnregisterCallback(IncrementCount, "test.trie.dead1",
                                  &deadCount);

  // Fire before the idle sweep — the dead callback must not fire.
  Preferences::SetBool("test.trie.dead1", true);
  EXPECT_EQ(deadCount, 0);
  EXPECT_EQ(liveCount, 1);

  Preferences::UnregisterCallback(IncrementCount, "test.trie.dead1",
                                  &liveCount);
}

// A "one-shot" callback that unregisters itself during its own execution.
// NotifyCallbacks does not Compact; the self-removed callback is only marked
// dead (Func == null) and the node lingers until the idle sweep.  A deeper
// callback at the same change must still fire on the first notification, and
// the dead callback must not fire on subsequent notifications because
// NotifyMatching skips null-Func nodes when building each snapshot.
namespace {

struct OneShotData {
  int count = 0;
  nsCString path;
  static void Callback(const char*, void* aData) {
    auto* d = static_cast<OneShotData*>(aData);
    ++d->count;
    Preferences::UnregisterPrefixCallback(Callback, d->path, d);
  }
};

}  // namespace

TEST(PrefsCallbackTrie, DeadNodeSkippedAfterSelfUnregister)
{
  int deeperCount = 0;
  OneShotData oneShot;
  oneShot.path = "test.trie.oneshot.a.b"_ns;

  Preferences::SetBool("test.trie.oneshot.a.b.c", false);
  // Register one-shot at ancestor (prefix) and a persistent callback at deeper
  // path.
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterPrefixCallback(
      OneShotData::Callback, "test.trie.oneshot.a.b"_ns, &oneShot)));
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterCallback(
      IncrementCount, "test.trie.oneshot.a.b.c"_ns, &deeperCount)));

  // First change: one-shot fires (and marks itself dead), deeper fires.  The
  // dead node is not compacted here; it is just skipped on later snapshots.
  Preferences::SetBool("test.trie.oneshot.a.b.c", true);
  EXPECT_EQ(oneShot.count, 1);
  EXPECT_EQ(deeperCount, 1);

  // Second change: one-shot is gone; deeper still fires.
  Preferences::SetBool("test.trie.oneshot.a.b.c", false);
  EXPECT_EQ(oneShot.count, 1);
  EXPECT_EQ(deeperCount, 2);

  Preferences::UnregisterCallback(IncrementCount, "test.trie.oneshot.a.b.c"_ns,
                                  &deeperCount);
}

// A callback at an ancestor unregisters a callback at a descendant path while
// both are in the same notification round.  The descendant callback is in the
// snapshot taken by NotifyMatching but must be skipped (Func == null) when the
// notification loop reaches it.  Subsequent notifications only fire the
// ancestor.
namespace {

struct CrossUnregData {
  int count = 0;
  int* targetCount = nullptr;  // points to the count of the node to unregister

  static void Callback(const char*, void* aData) {
    auto* d = static_cast<CrossUnregData*>(aData);
    ++d->count;
    if (d->targetCount) {
      Preferences::UnregisterCallback(IncrementCount, "test.trie.cross.a.b",
                                      d->targetCount);
      d->targetCount = nullptr;
    }
  }
};

}  // namespace

TEST(PrefsCallbackTrie, DeadNodeSkippedAfterCrossNodeUnregister)
{
  int descendantCount = 0;
  CrossUnregData ancestor;
  ancestor.targetCount = &descendantCount;

  Preferences::SetBool("test.trie.cross.a.b", false);
  // Ancestor fires first (shallower depth, prefix registration).
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterPrefixCallback(
      CrossUnregData::Callback, "test.trie.cross.a"_ns, &ancestor)));
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterCallback(
      IncrementCount, "test.trie.cross.a.b"_ns, &descendantCount)));

  // First change: ancestor fires and unregisters the descendant.
  // The descendant is in the toNotify snapshot but is skipped (Func == null).
  Preferences::SetBool("test.trie.cross.a.b", true);
  EXPECT_EQ(ancestor.count, 1);
  EXPECT_EQ(descendantCount, 0);

  // Second change: only the ancestor fires; descendant was removed by Compact.
  Preferences::SetBool("test.trie.cross.a.b", false);
  EXPECT_EQ(ancestor.count, 2);
  EXPECT_EQ(descendantCount, 0);

  Preferences::UnregisterPrefixCallback(CrossUnregData::Callback,
                                        "test.trie.cross.a"_ns, &ancestor);
}

// Regression test for bug 2051106. A callback that, mid-notification, both
// unregisters a co-notified callback and lets the dead-node sweep run frees
// that callback's trie node while it is still in NotifyCallbacks' snapshot. In
// the wild the sweep runs because the callback spins a nested event loop in
// which the idle ReapAndCompactCallbacks task fires; here we run it
// synchronously via ReapCallbacksForTesting to make the race deterministic.
// The snapshot now holds strong references, so compaction only unlinks the
// node and it stays alive (skipped, because unregistration cleared its func)
// until the round ends. Before the fix the snapshot held raw pointers and the
// loop dereferenced freed memory -- a use-after-free this asserts against
// (ASan turns the regression into a hard failure).
namespace {

struct CompactDuringNotifyData {
  int ancestorCount = 0;
  int victimCount = 0;

  static void Victim(const char*, void* aData) {
    ++static_cast<CompactDuringNotifyData*>(aData)->victimCount;
  }

  static void Ancestor(const char*, void* aData) {
    auto* d = static_cast<CompactDuringNotifyData*>(aData);
    ++d->ancestorCount;
    // Unregister the co-notified victim (marks it dead, schedules the sweep),
    // then force the sweep now, while this notification round is still
    // iterating its snapshot. Compact() frees the victim's trie node here.
    Preferences::UnregisterCallback(Victim, "test.trie.uaf.a.b"_ns, d);
    Preferences::ReapCallbacksForTesting();
  }
};

}  // namespace

TEST(PrefsCallbackTrie, CompactDuringNotifyDoesNotFreeSnapshot)
{
  CompactDuringNotifyData data;

  Preferences::SetBool("test.trie.uaf.a.b", false);
  // Ancestor (prefix) fires before the victim (exact) in the same round.
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterPrefixCallback(
      CompactDuringNotifyData::Ancestor, "test.trie.uaf.a"_ns, &data)));
  ASSERT_TRUE(NS_SUCCEEDED(Preferences::RegisterCallback(
      CompactDuringNotifyData::Victim, "test.trie.uaf.a.b"_ns, &data)));

  Preferences::SetBool("test.trie.uaf.a.b", true);
  EXPECT_EQ(data.ancestorCount, 1);
  EXPECT_EQ(data.victimCount, 0);

  Preferences::UnregisterPrefixCallback(CompactDuringNotifyData::Ancestor,
                                        "test.trie.uaf.a"_ns, &data);
}
