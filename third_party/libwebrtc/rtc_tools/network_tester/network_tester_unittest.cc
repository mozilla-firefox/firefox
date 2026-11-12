/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifdef WEBRTC_NETWORK_TESTER_TEST_ENABLED

#include <string>

#include "api/environment/environment.h"
#include "rtc_tools/network_tester/test_controller.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#include "test/testsupport/file_utils.h"
#include "test/wait_until.h"

namespace webrtc {

TEST(NetworkTesterTest, ServerClient) {
  test::RunLoop main_thread;
  Environment env = CreateTestEnvironment();

  const std::string client_log =
      test::TempFilename(test::OutputPath(), "client_packet_log.dat");
  const std::string server_log =
      test::TempFilename(test::OutputPath(), "server_packet_log.dat");
  {
    TestController client(
        env, 0, 0, test::ResourcePath("network_tester/client_config", "dat"),
        client_log);
    TestController server(
        env, 0, 0, test::ResourcePath("network_tester/server_config", "dat"),
        server_log);
    client.SendConnectTo("127.0.0.1", server.local_port());
    EXPECT_TRUE(
        WaitUntil([&] { return server.IsTestDone() && client.IsTestDone(); }));
  }
  EXPECT_TRUE(test::RemoveFile(client_log));
  EXPECT_TRUE(test::RemoveFile(server_log));
}

}  // namespace webrtc

#endif  // WEBRTC_NETWORK_TESTER_TEST_ENABLED
