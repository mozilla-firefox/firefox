(sccache-dist)=

# Distributed sccache (sccache-dist)

[sccache](https://github.com/mozilla/sccache) is a ccache-like tool written in
Rust by Mozilla and many contributors.

sccache-dist, its distributed variant, elevates this functionality by enabling
the distribution and caching of Rust compilations across multiple machines.
Please consider using sccache-dist when you have several machines
compiling Firefox on the same network.

The steps for setting up your machine as an sccache-dist server are detailed below.

In addition to improved security properties, distributed sccache offers
distribution and caching of rust compilation, so it should be an improvement
above and beyond what we see with icecc. Build servers run on Linux and
distributing builds is currently supported from Linux, macOS, and Windows.

## Steps for distributing a build as an sccache-dist client

Start by following the instructions at <https://github.com/mozilla/sccache/blob/master/docs/DistributedQuickstart.md#configure-a-client>
to configure your sccache distributed client.
*NOTE* If you're distributing from Linux a toolchain will be packaged
automatically and provided to the build server. If you're distributing from
Windows or macOS, start by using the cross-toolchains provided by
`./mach bootstrap` rather than attempting to use `icecc-create-env`.
sccache 0.2.12 or above is recommended, and the auth section of your config
must read:

```
[dist.auth]
type = "mozilla"
```

- If you're compiling from a macOS client, there are a handful of additional
  considerations outlined here:
  <https://github.com/mozilla/sccache/blob/master/docs/DistributedQuickstart.md#considerations-when-distributing-from-macos>.

  Run `./mach bootstrap` to download prebuilt toolchains to
  `~/.mozbuild/clang-dist-toolchain.tar.xz` and
  `~/.mozbuild/rustc-dist-toolchain.tar.xz`. This is an example of the paths
  that should be added to your client config to specify toolchains to build on
  macOS, located at `~/Library/Application Support/Mozilla.sccache/config`:

  ```
  [[dist.toolchains]]
  type = "path_override"
  compiler_executable = "/path/to/home/.rustup/toolchains/stable-x86_64-apple-darwin/bin/rustc"
  archive = "/path/to/home/.mozbuild/rustc-dist-toolchain.tar.xz"
  archive_compiler_executable = "/builds/worker/toolchains/rustc/bin/rustc"

  [[dist.toolchains]]
  type = "path_override"
  compiler_executable = "/path/to/home/.mozbuild/clang/bin/clang"
  archive = "/path/to/home/.mozbuild/clang-dist-toolchain.tar.xz"
  archive_compiler_executable = "/builds/worker/toolchains/clang/bin/clang"

  [[dist.toolchains]]
  type = "path_override"
  compiler_executable = "/path/to/home/.mozbuild/clang/bin/clang++"
  archive = "/path/to/home/.mozbuild/clang-dist-toolchain.tar.xz"
  archive_compiler_executable = "/builds/worker/toolchains/clang/bin/clang"
  ```

  Note that the version of `rustc` found in `rustc-dist-toolchain.tar.xz`
  must match the version of `rustc` used locally. The distributed archive
  will contain the version of `rustc` used by automation builds, which may
  lag behind stable for a few days after Rust releases, which is specified by
  the task definition in
  [this file](https://searchfox.org/firefox-main/source/taskcluster/kinds/toolchain/dist-toolchains.yml).
  For instance, to specify 1.37.0 rather than the current stable, run
  `rustup toolchain add 1.37.0` and point to
  `/path/to/home/.rustup/toolchains/1.37.0-x86_64-apple-darwin/bin/rustc` in your
  client config.

  The build system currently requires an explicit target to be passed with
  `HOST_CFLAGS` and `HOST_CXXFLAGS` e.g.:

  ```
  export HOST_CFLAGS="--target=x86_64-apple-darwin16.0.0"
  export HOST_CXXFLAGS="--target=x86_64-apple-darwin16.0.0"
  ```

- Compiling from a Windows client is supported but hasn't seen as much testing
  as other platforms. The following example mozconfig can be used as a guide:

  ```
  ac_add_options CCACHE="C:/Users/<USER>/.mozbuild/sccache/sccache.exe"

  export CC="C:/Users/<USER>/.mozbuild/clang/bin/clang-cl.exe --driver-mode=cl"
  export CXX="C:/Users/<USER>/.mozbuild/clang/bin/clang-cl.exe --driver-mode=cl"
  export HOST_CC="C:/Users/<USER>/.mozbuild/clang/bin/clang-cl.exe --driver-mode=cl"
  export HOST_CXX="C:/Users/<USER>/.mozbuild/clang/bin/clang-cl.exe --driver-mode=cl"
  ```

  The client config should be located at
  `~/AppData/Roaming/Mozilla/sccache/config/config`, and as on macOS custom
  toolchains should be obtained with `./mach bootstrap` and specified in the
  client config, for example:

  ```
  [[dist.toolchains]]
  type = "path_override"
  compiler_executable = "C:/Users/<USER>/.mozbuild/clang/bin/clang-cl.exe"
  archive = "C:/Users/<USER>/.mozbuild/clang-dist-toolchain.tar.xz"
  archive_compiler_executable = "/builds/worker/toolchains/clang/bin/clang"

  [[dist.toolchains]]
  type = "path_override"
  compiler_executable = "C:/Users/<USER>/.rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe"
  archive = "C:/Users/<USER>/.mozbuild/rustc-dist-toolchain.tar.xz"
  archive_compiler_executable = "/builds/worker/toolchains/rustc/bin/rustc"
  ```

- Add the following to your mozconfig:

  ```
  ac_add_options CCACHE=/path/to/home/.mozbuild/sccache/sccache
  ```

  If you're compiling from a macOS client, you might need some additional configuration:

  ```
  # Set the target flag to Darwin
  export CFLAGS="--target=x86_64-apple-darwin16.0.0"
  export CXXFLAGS="--target=x86_64-apple-darwin16.0.0"
  export HOST_CFLAGS="--target=x86_64-apple-darwin16.0.0"
  export HOST_CXXFLAGS="--target=x86_64-apple-darwin16.0.0"

  # Specify the macOS SDK to use
  ac_add_options --with-macos-sdk=/path/to/MacOSX-SDKs/MacOSX13.3.sdk
  ```

  You can get the right macOS SDK by downloading an old version of XCode from
  [developer.apple.com](https://developer.apple.com) and unpacking the SDK
  from it.

- When attempting to get your client running, the output of `sccache -s` should
  be consulted to confirm compilations are being distributed. To receive helpful
  logging from the local daemon in case they aren't, run
  `SCCACHE_NO_DAEMON=1 SCCACHE_START_SERVER=1 SCCACHE_LOG=sccache=trace path/to/sccache`
  in a terminal window separate from your build prior to building. *NOTE* use
  `RUST_LOG` instead of `SCCACHE_LOG` if your build of `sccache` does not
  include [pull request 822](https://github.com/mozilla/sccache/pull/822). (`sccache` binaries from
  `mach bootstrap` do include this PR.)

- Run `./mach build -j<value>` with an appropriately large `<value>`.
  `sccache --dist-status` should provide the number of cores available to you
  (or a message if you're not connected). In the future this will be integrated
  with the build system to automatically select an appropriate value.

This should be enough to distribute your build and replace your use of icecc.
Bear in mind there may be a few speedbumps, and please ensure your version of
sccache is current before investigating further. Please see the common questions
section below and ask for help if anything is preventing you from using it over
email (dev-builds), on slack in #sccache, or in #build on irc.

## Steps for setting up a server

Build servers must run linux and use bubblewrap 0.3.0+ for sandboxing of compile
processes. This requires a kernel 4.6 or greater, so Ubuntu 18+, RHEL 8, or
similar.

- Run `./mach bootstrap` or
  `./mach artifact toolchain --from-build linux64-sccache` to acquire a recent
  version of `sccache-dist`. Please use a `sccache-dist` binary acquired in
  this fashion to ensure compatibility with statically linked dependencies.

- The instructions at <https://github.com/mozilla/sccache/blob/master/docs/DistributedQuickstart.md#configure-a-build-server>
  should contain everything else required to configure and run the server.

  *NOTE* Port 10500 will be used by convention for builders.
  Please use port 10500 in the `public_addr` section of your builder config.

  Extra logging may be helpful when setting up a server. To enable logging,
  run your server with
  `sudo env SCCACHE_LOG=sccache=trace ~/.mozbuild/sccache/sccache-dist server --config ~/.config/sccache/server.conf`
  (or similar). *NOTE* `sudo` *must* come before setting environment variables
  for this to work. *NOTE* use `RUST_LOG` instead of `SCCACHE_LOG` if your
  build of `sccache` does not include [pull request 822](https://github.com/mozilla/sccache/pull/822). (`sccache` binaries from
  `mach bootstrap` do include this PR.)

## Common questions/considerations

- My build is still slow: scache-dist can only do so much with parts of the
  build that aren't able to be parallelized. To start debugging a slow build,
  ensure the "Successful distributed compilations" line in the output of
  `sccache -s` dominates other counts. For a full build, at least a 2-3x
  improvement should be observed.
- My build output is incomprehensible due to a flood of warnings: clang will
  treat some warnings differently when it's fed preprocessed code in a separate
  invocation (preprocessing occurs locally with sccache-dist). Adding
  `rewrite_includes_only = true` to the `dist` section of your client config
  will improve this; however, setting this will cause build failures with a
  commonly deployed version of `glibc`. This option will default to `true`
  once the fix is more widely available. Details of this fix can be found in
  [this patch](https://sourceware.org/ml/libc-alpha/2019-11/msg00431.html).
- My build fails with a message about incompatible versions of rustc between
  dependent crates: if you're using a custom toolchain check that the version
  of rustc in your `rustc-dist-toolchain.tar.xz` is the same as the version
  you're running locally.
