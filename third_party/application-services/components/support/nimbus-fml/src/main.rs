/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

mod backends;
mod command_line;
mod defaults;
mod editing;
mod error;
#[cfg(test)]
mod fixtures;
mod frontend;
mod intermediate_representation;
mod lints;
mod parser;
mod schema;
mod util;

use anyhow::Result;

const SUPPORT_URL_LOADING: bool = true;

fn main() -> Result<()> {
    crate::command_line::do_main(std::env::args_os(), &std::env::current_dir()?)
}
