#![cfg(any(target_os = "linux", target_os = "android"))]

use process_backend::{Backend as _, DirReader, remote};
use remote::{backend::Backend, executor, io::UnixStream, transport};
use std::{
    collections::HashSet,
    ffi::CString,
    fs::remove_file,
    os::unix::{ffi::OsStringExt as _, fs::symlink},
    thread,
};

const MAX_INPUT_LEN: usize = 32;
const MAX_OUTPUT_LEN: usize = 65536;
const MAX_FILE_READERS: usize = 5;
const MAX_DIR_READERS: usize = 5;
const MAX_MAPPED_MODULE_MEMORY_READERS: usize = 5;

#[test]
fn basic() {
    let (backend_io, executor_io) = UnixStream::pair().unwrap();

    let executor_thread = thread::spawn(move || {
        let pid = unsafe { libc::getpid() };
        let mut args_buf = [0u8; MAX_INPUT_LEN];
        let executor_transport = transport::postcard::Executor::new(executor_io, &mut args_buf);
        let mut file_readers = executor::resources::FileReader::new_array::<MAX_FILE_READERS>();
        let mut dir_readers = executor::resources::DirReader::new_array::<MAX_DIR_READERS>();
        let mut mapped_module_memory_readers =
            executor::resources::MappedModuleMemoryReader::new_array::<
                MAX_MAPPED_MODULE_MEMORY_READERS,
            >();

        let mut scratch_buf = [0u8; MAX_OUTPUT_LEN];
        let resources = executor::resources::Resources {
            file_readers: &mut file_readers,
            dir_readers: &mut dir_readers,
            mapped_module_memory_readers: &mut mapped_module_memory_readers,
            scratch_buf: &mut scratch_buf,
        };
        executor::run(pid, executor_transport, resources)
    });

    const FAKE_TARGET: &str = "/foo/bar";

    let temp_dir = std::env::temp_dir();

    let link_path = temp_dir.join("process-backend.test.link");

    if link_path.symlink_metadata().is_ok_and(|m| m.is_symlink()) {
        remove_file(&link_path).unwrap();
    }

    symlink(FAKE_TARGET, &link_path).unwrap();

    let output_buf = vec![0u8; MAX_OUTPUT_LEN];
    let backend_transport = transport::postcard::Backend::new(backend_io, output_buf);
    let backend = Backend::new(backend_transport);

    let mut link = vec![0u8; MAX_OUTPUT_LEN];

    let link_path_cstr = CString::new(link_path.into_os_string().into_vec()).unwrap();

    let len = backend.read_link(&link_path_cstr, &mut link).unwrap();
    link.truncate(len);
    let link = String::from_utf8(link).unwrap();

    assert_eq!(link, FAKE_TARGET);

    let stat = backend.stat_file(c"/").unwrap();

    // Should be pretty safe for testing -- The root directory should definitely have rwx for
    // owner and rx for everyone else. Group should at-least have r+x.
    assert!(stat.st_mode & 0o757 == 0o755);

    // A set of names we'd generally expect to find in the root directory
    let mut unencountered_dirs = HashSet::from(["bin", "dev", "etc", "lib", "opt", "tmp", "var"]);

    let mut output_buf = vec![0u8; MAX_OUTPUT_LEN];
    let mut dir_reader = backend.read_dir(c"/").unwrap();
    loop {
        let len = dir_reader.read_next_name(&mut output_buf).unwrap();
        let name = &output_buf[0..len];
        if name.is_empty() {
            break;
        }
        let Ok(name_str) = str::from_utf8(&name) else {
            continue;
        };
        unencountered_dirs.remove(name_str);
    }

    assert!(unencountered_dirs.is_empty());

    drop(dir_reader);
    drop(backend);

    executor_thread.join().unwrap().unwrap();
}

#[test]
fn poisoned_transport_returns_error() {
    let (backend_io, executor_io) = UnixStream::pair().unwrap();
    // Forcefully break the connection
    drop(executor_io);

    let output_buf = vec![0u8; MAX_OUTPUT_LEN];
    let backend = Backend::new(transport::postcard::Backend::new(backend_io, output_buf));

    assert!(matches!(
        backend.stat_file(c"/"),
        Err(process_backend::Error::Transport(_))
    ));
    assert!(matches!(
        backend.stat_file(c"/"),
        Err(process_backend::Error::TransportPoisoned)
    ));
}
