// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::{
    ffi::{OsStr, OsString},
    path::Path,
    vec,
};

use anyhow::Result;
use nimbus_fml::command_line::do_main;

pub(crate) fn fml_cli(args: &Vec<OsString>, cwd: &Path) -> Result<bool> {
    // We prepend the string `nimbus-cli fml` to the args to pass to FML
    // because the clap uses the 0th argument for help messages; so the FML's command line processor
    // will report an error with a usage message of `nimbus-cli fml generate [FLAGS] INPUT OUTPUT`.
    let first = OsStr::new("nimbus-cli fml").to_os_string();
    let mut cli_args = vec![&first];

    let help = OsStr::new("--help").to_os_string();
    if args.is_empty() {
        // If the user has just typed `nimbus-cli fml`– with no further arguments— then the rather unhelpful message
        // `not implemented: Command  not implemented` is displayed. This will change if and when we upgrade the nimbus-fml
        // to use cli-derive, but until then, we can do a simple thing to make the experience a bit nicer, by adding
        // the `--help` flag, so the user gets the nimbus-fml command line help.
        cli_args.push(&help);
    }

    // Finally, send all the args after `nimbus-cli fml` verbatim to the FML clap cli.
    cli_args.extend(args);
    do_main(cli_args, cwd)?;
    Ok(true)
}
