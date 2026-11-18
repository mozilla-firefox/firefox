This directory exists to provide the C++ interface to the mp4parse-rust code.
The code itself is hosted at https://github.com/mozilla/mp4parse-rust and
vendored into the /third_party/rust directory, so the only things here are the
moz.build file which specifies how the dynamically generated bindings header
should be generated and mp4parse.h, the header which consumers should include.
It includes the dynamically-generated bindings header as well as any
additional support code for FFI.

# Updating the version from the github repository

1. In /toolkit/library/rust/shared/Cargo.toml, Set the `rev` attribute of the
   `mp4parse_capi` dependency to the revision you want to use.
2. Run `mach vendor rust` (`--force` may be necessary since the `mp4parse`
   crate's lib.rs is quite large).
3. Verify the expected changes in /third_party/rust.
4. Build, run try, etc.

NOTE: Git has no concept of checking out a subdirectory, so `cargo` will
search the whole repository to find the crate. Because the `mp4parse_capi`
depends on the `mp4parse` crate in the same repository via a relative path,
both crates will be vendored into /third_party/rust and should be part of the
same revision of mp4parse-rust.

# Developing changes to mp4parse-rust crates

Before committing changes to the github repository, it's a good idea to test
them out in the context of Firefox. There are a number of ways to
achieve this with various trade-offs, but here are the two I recommend. Both
start the same way:

1. `git clone https://github.com/mozilla/mp4parse-rust` somewhere outside
   the Firefox repository. For example: /Users/your_username/src/mp4parse-rust.

## For rapid iteration on local, uncommitted changes

2. In /toolkit/library/rust/shared/Cargo.toml, change the `mp4parse_capi`
   dependency to
   ```
   mp4parse_capi = { path = "/Users/your_username/src/mp4parse-rust/mp4parse_capi" }
   ```
3. Run `mach vendor rust`; the code in third_party/rust/mp4parse_capi and
   third_party/rust/mp4parse will be removed, indicating the code in your
   local checkout is being used instead.
4. In the moz.build in this directory, in `ffi_generated.inputs`, change
   '/third_party/rust/mp4parse_capi' to
   '//Users/your_username/src/mp4parse-rust/mp4parse_capi'. Note the
   double-slash, it's required to reference paths outside the Firefox repository.
5. Build and run the code or tests normally to exercise the code in
   /Users/your_username/src/mp4parse-rust.

This is a fast way to try experiment with the rust code, but because it exists
outside the Firefox source tree, it cannot be used with try.

## For validation of local, committed changes

2. In /toolkit/library/rust/shared/Cargo.toml, change the `mp4parse_capi`
   dependency to
   ```
   mp4parse_capi = { git = "file:///Users/your_username/src/mp4parse-rust" }
   ```
3. Run `mach vendor rust`; the local, committed code will be copied into
   third_party/rust/mp4parse_capi and third_party/rust/mp4parse. Confirm the
   diff is what you expect.
4. Unlike above, no changes to moz.build are necessary; if locally changed,
   make sure to revert.
5. Build and run the code or tests normally to exercise the code in
   /Users/your_username/src/mp4parse-rust. This can include try runs, but if
   you make any additional changes, you must be sure to commit them in your
   local git checkout of mp4parse-rust and re-run `mach vendor rust`.

This is a more thorough way to test local changes to the rust code since try
is available, but it's slower than the `path` dependency approach above
because every change must be committed and copied into the Firefox
source tree with `mach vendor rust`.
