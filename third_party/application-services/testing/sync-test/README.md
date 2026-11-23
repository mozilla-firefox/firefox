# End-to-End Tests for Sync

This package implements "end-to-end" integration tests for syncing various data types -
two clients using a real live account and a real live sync server, exchanging
data and asserting that the exchange works as intended.

## Running the tests

Run the tests using `cargo run`.

Use `cargo run -- --help` to see the available options.

## Adding tests

For each datatype managed by sync, there should be a suite of corresponding tests.
To add some:

0. In `auth.rs`, add support your sync engine to the `TestClient` struct.
0. Create a file `./src/<datatype>.rs` to hold the tests; `logins.rs` may provide a useful example.
  0. Create a `test_<name>` function for each scenario you want to exercise. The function should take
     two `TestClient` instances as arguments, and use them to drive a simulated sync between two clients.
  0. Define a `get_test_group()` function that returns your test scenarios in a `TestGroup` struct.
0. Add your test group to the `main` function defined in `main.rs` for execution.

## Simulating old and new clients

While per-device engine choices roll out, an account will have clients using
both models. Each `TestClient` has an `engine_choices_model`: leave it as the
default (`EngineChoicesModel::Account`) for an old, per-account client, or set
it to `EngineChoicesModel::Device` for a new, per-device one. It can be changed
between syncs to simulate the pref flipping. `server_meta_global()` and
`server_sync_ids()` return those records, so tests can check what each kind of
client leaves behind, and `update_server_meta_global()` and
`update_server_sync_ids()` change them as another client might. The
`engine_choices` group has examples; run just those with
`cargo run -- engine_choices`.
