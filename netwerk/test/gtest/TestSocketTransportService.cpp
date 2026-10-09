#include "gtest/gtest.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/TimeStamp.h"
#include "nsCOMPtr.h"
#include "nsISocketTransport.h"
#include "nsString.h"
#include "nsComponentManagerUtils.h"
#include "../../base/nsSocketTransport2.h"
#include "../../base/nsSocketTransportService2.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"

namespace mozilla {
namespace net {

TEST(TestSocketTransportService, PortRemappingPreferenceReading)
{
  nsCOMPtr<nsISocketTransportService> service =
      do_GetService("@mozilla.org/network/socket-transport-service;1");
  ASSERT_TRUE(service);

  auto* sts = gSocketTransportService;
  ASSERT_TRUE(sts);

  NS_DispatchAndSpinEventLoopUntilComplete(
      "test"_ns, sts, NS_NewRunnableFunction("test", [&]() {
        auto CheckPortRemap = [&](uint16_t input, uint16_t output) -> bool {
          sts->ApplyPortRemap(&input);
          return input == output;
        };

        // Ill-formed prefs
        ASSERT_FALSE(sts->UpdatePortRemapPreference(";"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference(" ;"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("; "_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("foo"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference(" foo"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference(" foo "_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("10=20;"_ns));

        ASSERT_FALSE(sts->UpdatePortRemapPreference("1"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1="_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1,="_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1-="_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1-,="_ns));

        ASSERT_FALSE(sts->UpdatePortRemapPreference("1=2,"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1=2-3"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1-2,=3"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1-2,3"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1-2,3-4"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1-2,3-4,"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1-2,3-4="_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("10000000=10"_ns));

        ASSERT_FALSE(sts->UpdatePortRemapPreference("1=2;3"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1-4=2;3"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1-4=2;3="_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1-foo=2;3=15"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1-4=foo;3=15"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1-4=2;foo=15"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1-4=2;3=foo"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1-4=2x3=15"_ns));
        ASSERT_FALSE(sts->UpdatePortRemapPreference("1+4=2;3=15"_ns));

        // Well-formed prefs
        ASSERT_TRUE(sts->UpdatePortRemapPreference("1=2"_ns));
        ASSERT_TRUE(CheckPortRemap(1, 2));
        ASSERT_TRUE(CheckPortRemap(2, 2));
        ASSERT_TRUE(CheckPortRemap(3, 3));

        ASSERT_TRUE(sts->UpdatePortRemapPreference("10=20"_ns));
        ASSERT_TRUE(CheckPortRemap(1, 1));
        ASSERT_TRUE(CheckPortRemap(2, 2));
        ASSERT_TRUE(CheckPortRemap(3, 3));
        ASSERT_TRUE(CheckPortRemap(10, 20));

        ASSERT_TRUE(sts->UpdatePortRemapPreference("100-200=1000"_ns));
        ASSERT_TRUE(CheckPortRemap(10, 10));
        ASSERT_TRUE(CheckPortRemap(99, 99));
        ASSERT_TRUE(CheckPortRemap(100, 1000));
        ASSERT_TRUE(CheckPortRemap(101, 1000));
        ASSERT_TRUE(CheckPortRemap(200, 1000));
        ASSERT_TRUE(CheckPortRemap(201, 201));

        ASSERT_TRUE(sts->UpdatePortRemapPreference("100-200,500=1000"_ns));
        ASSERT_TRUE(CheckPortRemap(10, 10));
        ASSERT_TRUE(CheckPortRemap(99, 99));
        ASSERT_TRUE(CheckPortRemap(100, 1000));
        ASSERT_TRUE(CheckPortRemap(101, 1000));
        ASSERT_TRUE(CheckPortRemap(200, 1000));
        ASSERT_TRUE(CheckPortRemap(201, 201));
        ASSERT_TRUE(CheckPortRemap(499, 499));
        ASSERT_TRUE(CheckPortRemap(500, 1000));
        ASSERT_TRUE(CheckPortRemap(501, 501));

        ASSERT_TRUE(sts->UpdatePortRemapPreference("1-3=10;5-8,12=20"_ns));
        ASSERT_TRUE(CheckPortRemap(1, 10));
        ASSERT_TRUE(CheckPortRemap(2, 10));
        ASSERT_TRUE(CheckPortRemap(3, 10));
        ASSERT_TRUE(CheckPortRemap(4, 4));
        ASSERT_TRUE(CheckPortRemap(5, 20));
        ASSERT_TRUE(CheckPortRemap(8, 20));
        ASSERT_TRUE(CheckPortRemap(11, 11));
        ASSERT_TRUE(CheckPortRemap(12, 20));

        ASSERT_TRUE(sts->UpdatePortRemapPreference("80=8080;443=8080"_ns));
        ASSERT_TRUE(CheckPortRemap(80, 8080));
        ASSERT_TRUE(CheckPortRemap(443, 8080));

        // Later rules rewrite earlier rules
        ASSERT_TRUE(sts->UpdatePortRemapPreference("10=100;10=200"_ns));
        ASSERT_TRUE(CheckPortRemap(10, 200));

        ASSERT_TRUE(sts->UpdatePortRemapPreference("10-20=100;10-20=200"_ns));
        ASSERT_TRUE(CheckPortRemap(10, 200));
        ASSERT_TRUE(CheckPortRemap(20, 200));

        ASSERT_TRUE(sts->UpdatePortRemapPreference("10-20=100;15=200"_ns));
        ASSERT_TRUE(CheckPortRemap(10, 100));
        ASSERT_TRUE(CheckPortRemap(15, 200));
        ASSERT_TRUE(CheckPortRemap(20, 100));

        ASSERT_TRUE(sts->UpdatePortRemapPreference(
            " 100 - 200 = 1000 ; 150 = 2000 "_ns));
        ASSERT_TRUE(CheckPortRemap(100, 1000));
        ASSERT_TRUE(CheckPortRemap(150, 2000));
        ASSERT_TRUE(CheckPortRemap(200, 1000));

        // Turn off any mapping
        ASSERT_TRUE(sts->UpdatePortRemapPreference(""_ns));
        for (uint32_t port = 0; port < 65536; ++port) {
          ASSERT_TRUE(CheckPortRemap((uint16_t)port, (uint16_t)port));
        }
      }));
}

namespace {

// Re-dispatches itself to the socket thread at high priority until its
// deadline, simulating a continuous stream of prioritized socket thread work.
class PrioritySpinner final : public nsIRunnable, public nsIRunnablePriority {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIRUNNABLE
  NS_DECL_NSIRUNNABLEPRIORITY

  PrioritySpinner(nsSocketTransportService* aSTS, TimeDuration aDuration)
      : mSTS(aSTS), mDeadline(TimeStamp::Now() + aDuration) {}

 private:
  ~PrioritySpinner() = default;

  const RefPtr<nsSocketTransportService> mSTS;
  const TimeStamp mDeadline;
};

NS_IMPL_ISUPPORTS(PrioritySpinner, nsIRunnable, nsIRunnablePriority)

NS_IMETHODIMP PrioritySpinner::GetPriority(uint32_t* aPriority) {
  *aPriority = nsIRunnablePriority::PRIORITY_MEDIUMHIGH;
  return NS_OK;
}

NS_IMETHODIMP PrioritySpinner::Run() {
  if (TimeStamp::Now() > mDeadline) {
    return NS_OK;
  }
  nsCOMPtr<nsIRunnable> self(this);
  return mSTS->Dispatch(self.forget(), NS_DISPATCH_NORMAL);
}

}  // namespace

TEST(TestSocketTransportService, HighPriorityRunnablesDoNotBlockShutdown)
{
  nsCOMPtr<nsISocketTransportService> service =
      do_GetService("@mozilla.org/network/socket-transport-service;1");
  ASSERT_TRUE(service);

  auto* sts = gSocketTransportService;
  ASSERT_TRUE(sts);
  ASSERT_TRUE(StaticPrefs::network_socket_prioritize_runnables());

  RefPtr<PrioritySpinner> spinner =
      new PrioritySpinner(sts, TimeDuration::FromSeconds(10));
  nsCOMPtr<nsIRunnable> spin = static_cast<nsIRunnable*>(spinner.get());
  ASSERT_TRUE(NS_SUCCEEDED(sts->Dispatch(spin.forget(), NS_DISPATCH_NORMAL)));

  TimeStamp start = TimeStamp::Now();
  ASSERT_TRUE(NS_SUCCEEDED(sts->Shutdown(false)));
  double elapsed = (TimeStamp::Now() - start).ToMilliseconds();

  // Bring the service back up for the tests running after this one. This is
  // the same shutdown/restart cycle nsIOService performs when going offline
  // and back online.
  ASSERT_TRUE(NS_SUCCEEDED(sts->Init()));

  EXPECT_LT(elapsed, 4000.0)
      << "socket thread shutdown was delayed by high priority events";
}

TEST(TestSocketTransportService, StatusValues)
{
  static_assert(static_cast<nsresult>(nsISocketTransport::STATUS_RESOLVING) ==
                NS_NET_STATUS_RESOLVING_HOST);
  static_assert(static_cast<nsresult>(nsISocketTransport::STATUS_RESOLVED) ==
                NS_NET_STATUS_RESOLVED_HOST);
  static_assert(
      static_cast<nsresult>(nsISocketTransport::STATUS_CONNECTING_TO) ==
      NS_NET_STATUS_CONNECTING_TO);
  static_assert(
      static_cast<nsresult>(nsISocketTransport::STATUS_CONNECTED_TO) ==
      NS_NET_STATUS_CONNECTED_TO);
  static_assert(static_cast<nsresult>(nsISocketTransport::STATUS_SENDING_TO) ==
                NS_NET_STATUS_SENDING_TO);
  static_assert(static_cast<nsresult>(nsISocketTransport::STATUS_WAITING_FOR) ==
                NS_NET_STATUS_WAITING_FOR);
  static_assert(
      static_cast<nsresult>(nsISocketTransport::STATUS_RECEIVING_FROM) ==
      NS_NET_STATUS_RECEIVING_FROM);
  static_assert(static_cast<nsresult>(
                    nsISocketTransport::STATUS_TLS_HANDSHAKE_STARTING) ==
                NS_NET_STATUS_TLS_HANDSHAKE_STARTING);
  static_assert(
      static_cast<nsresult>(nsISocketTransport::STATUS_TLS_HANDSHAKE_ENDED) ==
      NS_NET_STATUS_TLS_HANDSHAKE_ENDED);
}

// PR_END_OF_FILE_ERROR, PR_CONNECT_RESET_ERROR, and PR_CONNECT_ABORTED_ERROR
// should all map to NS_ERROR_NET_RESET so that HTTP transactions automatically
// retry on unexpected connection drops.
TEST(TestSocketTransportService, ErrorAccordingToNSPR)
{
  EXPECT_EQ(ErrorAccordingToNSPR(PR_END_OF_FILE_ERROR), NS_ERROR_NET_RESET);
  EXPECT_EQ(ErrorAccordingToNSPR(PR_CONNECT_RESET_ERROR), NS_ERROR_NET_RESET);
  EXPECT_EQ(ErrorAccordingToNSPR(PR_CONNECT_ABORTED_ERROR), NS_ERROR_NET_RESET);
}

}  // namespace net
}  // namespace mozilla
