# How Rust Engines are implemented

There are two main components to engines implemented in Rust

## The bridged-engine

Because Rust engines still need to work with the existing Sync infrastructure,
there's the concept of a {searchfox}`bridged-engine <services/sync/modules/bridged_engine.sys.mjs>`.
In short, this is just a shim between the existing
{searchfox}`Sync Service <services/sync/modules/service.sys.mjs>`
and the Rust code.
