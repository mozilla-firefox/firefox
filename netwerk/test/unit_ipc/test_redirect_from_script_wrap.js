"use strict";

let observer = null;

function run_test() {
  do_await_remote_message("register-redirector").then(redirects => {
    observer = {
      QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),

      observe(subject) {
        let channel = subject.QueryInterface(Ci.nsIHttpChannel);
        let target = redirects[channel.URI.spec];
        if (target) {
          channel.redirectTo(Services.io.newURI(target));
        }
      },
    };
    Services.obs.addObserver(observer, "http-on-modify-request");
    registerCleanupFunction(() => {
      Services.obs.removeObserver(observer, "http-on-modify-request");
    });
    do_send_remote_message("register-redirector-done");
  });

  run_test_in_child("../unit/test_redirect_from_script.js");
}
