// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::fmt::Debug;

use http::Uri as Url;

use crate::{Error, Res};

pub trait RequestTarget: Debug {
    fn scheme(&self) -> Res<&str>;
    fn authority(&self) -> &str;
    fn path(&self) -> Res<&str>;
}

impl RequestTarget for Url {
    fn scheme(&self) -> Res<&str> {
        self.scheme_str().ok_or(Error::InvalidInput)
    }

    fn authority(&self) -> &str {
        Self::authority(self).map_or("", |a| a.as_str().rsplit('@').next().unwrap_or_default())
    }

    fn path(&self) -> Res<&str> {
        if self
            .path_and_query()
            .is_some_and(|p| p.as_str().bytes().any(|b| b.is_ascii_control()))
        {
            return Err(Error::InvalidInput);
        }
        Ok(Self::path(self))
    }
}

impl<'s, 'a, 'p> RequestTarget for (&'s str, &'a str, &'p str) {
    fn scheme(&self) -> Res<&'s str> {
        Ok(self.0)
    }

    fn authority(&self) -> &'a str {
        self.1
    }

    fn path(&self) -> Res<&'p str> {
        Ok(self.2)
    }
}
