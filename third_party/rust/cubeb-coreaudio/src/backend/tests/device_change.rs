// NOTICE:
// Avoid running TestDeviceSwitcher with TestDevicePlugger or active full-duplex streams
// sequentially!
//
// The TestDeviceSwitcher cannot work with any test that will create an aggregate device that is
// soon being destroyed. The TestDeviceSwitcher will cache the available devices, upon it's
// created, as the candidates for the default device. Therefore, those created aggregate devices
// may be cached in TestDeviceSwitcher. However, those aggregate devices may be destroyed when
// TestDeviceSwitcher is using them or they are in the cached list of TestDeviceSwitcher.
//
// Running those tests by setting `test-threads=1` doesn't really help (e.g.,
// `cargo test test_register_device_changed_callback -- --ignored --nocapture --test-threads=1`).
// The aggregate device won't be destroyed immediately when `kAudioPlugInDestroyAggregateDevice`
// is set. As a result, the following tests requiring changing the devices will be run separately
// in the run_tests.sh script and marked by `ignore` by default.

use super::utils::{
    get_devices_info_in_scope, test_create_device_change_listener, test_device_in_scope,
    test_get_default_device, test_get_devices_in_scope,
    test_get_stream_with_default_data_callback_by_type, test_ops_stream_operation,
    test_set_default_device, Scope, StreamType, TestDevicePlugger, TestDeviceSwitcher,
};
use super::*;
use std::sync::{LockResult, WaitTimeoutResult};
use std::thread;
use std::time;

// Switch default devices used by the active streams, to test stream reinitialization
// ================================================================================================
#[ignore]
#[test]
fn test_switch_device() {
    test_switch_device_in_scope(Scope::Input);
    test_switch_device_in_scope(Scope::Output);
}

fn test_switch_device_in_scope(scope: Scope) {
    println!(
        "Switch default device for {:?} while the stream is working.",
        scope
    );

    // Do nothing if there is no 2 available devices at least.
    let devices = test_get_devices_in_scope(scope.clone());
    if devices.len() < 2 {
        println!("Need 2 devices for {:?} at least. Skip.", scope);
        return;
    }

    let mut device_switcher = TestDeviceSwitcher::new(scope.clone());

    let notifier = Arc::new(Notifier::new(0));
    let also_notifier = notifier.clone();
    let listener = run_serially(|| {
        test_create_device_change_listener(scope.clone(), move |_addresses| {
            let mut cnt = notifier.lock().unwrap();
            *cnt += 1;
            notifier.notify(cnt);
            NO_ERR
        })
    });
    run_serially(|| listener.start());

    let changed_watcher = Watcher::new(&also_notifier);
    test_get_started_stream_in_scope(scope.clone(), move |_stream| loop {
        let start_cnt = *changed_watcher.lock().unwrap();
        device_switcher.next();
        let mut guard = changed_watcher.lock().unwrap();
        guard = changed_watcher
            .wait_while(guard, |cnt| *cnt == start_cnt)
            .unwrap();
        if *guard >= devices.len() {
            break;
        }
    });
}

#[ignore]
#[test]
fn test_switch_while_paused() {
    test_switch_device_in_scope_while_paused(Scope::Input);
    test_switch_device_in_scope_while_paused(Scope::Output);
}

fn test_switch_device_in_scope_while_paused(scope: Scope) {
    println!(
        "Switch default device for {:?} while the stream is paused.",
        scope
    );

    // Do nothing if there is no 2 available devices at least.
    let devices = test_get_devices_in_scope(scope.clone());
    if devices.len() < 2 {
        println!("Need 2 devices for {:?} at least. Skip.", scope);
        return;
    }

    let mut device_switcher = TestDeviceSwitcher::new(scope.clone());

    let notifier = Arc::new(Notifier::new(0));
    let also_notifier = notifier.clone();
    let listener = run_serially(|| {
        test_create_device_change_listener(scope.clone(), move |_addresses| {
            let mut cnt = notifier.lock().unwrap();
            *cnt += 1;
            notifier.notify(cnt);
            NO_ERR
        })
    });
    run_serially(|| listener.start());

    let changed_watcher = Watcher::new(&also_notifier);
    test_get_started_stream_in_scope(scope.clone(), move |stream| loop {
        // pause the stream, change device, start the stream
        // peek under the hood to the find the device in use currently
        let stm = unsafe { &mut *(stream as *mut AudioUnitStream) };
        let before = if scope == Scope::Output {
            stm.core_stream_data.output_unit
        } else {
            stm.core_stream_data.input_unit
        };

        let check_devices = |current| {
            stm.queue.run_sync(|| {
                let (bus, unit, id) = if scope == Scope::Output {
                    (
                        AU_OUT_BUS,
                        stm.core_stream_data.output_unit,
                        stm.core_stream_data.output_device.id,
                    )
                } else {
                    (
                        AU_IN_BUS,
                        stm.core_stream_data.input_unit,
                        stm.core_stream_data.input_device.id,
                    )
                };
                let mut device_id: AudioDeviceID = 0;
                let mut size = std::mem::size_of::<AudioDeviceID>();
                let status = audio_unit_get_property(
                    unit,
                    kAudioOutputUnitProperty_CurrentDevice,
                    kAudioUnitScope_Global,
                    bus,
                    &mut device_id,
                    &mut size,
                );
                if status != NO_ERR {
                    panic!("Could not get device ID from audiounit");
                }
                assert_eq!(id, current);
                assert_eq!(device_id, current);
            });
        };

        check_devices(device_switcher.current());

        // Pause the stream, and change the default device
        assert_eq!(unsafe { OPS.stream_stop.unwrap()(stream) }, ffi::CUBEB_OK);

        let start_cnt = *changed_watcher.lock().unwrap();
        device_switcher.next();
        let mut guard = changed_watcher.lock().unwrap();
        guard = changed_watcher
            .wait_while(guard, |cnt| *cnt == start_cnt)
            .unwrap();
        if *guard >= devices.len() {
            break;
        }

        // Wait until the switch is effective and the stream has been marked for delayed
        // reinitialization. delayed_reinit can only be access from the queue thread. Depending on
        // the setup this can take some time so we sleep a bit to not hammer the main thread.
        let mut switched = false;
        while !switched {
            switched = stm.queue.run_sync(|| stm.delayed_reinit).unwrap();
            if !switched {
                let ten_millis = time::Duration::from_millis(10);
                thread::sleep(ten_millis);
            }
        }

        // Start the stream, and check that the device in use isn't the same as before pausing
        assert_eq!(unsafe { OPS.stream_start.unwrap()(stream) }, ffi::CUBEB_OK);

        check_devices(device_switcher.current());

        let after = if scope == Scope::Output {
            stm.core_stream_data.output_unit
        } else {
            stm.core_stream_data.input_unit
        };
        assert_ne!(before, after);
    });
}

fn test_get_started_stream_in_scope<F>(scope: Scope, operation: F)
where
    F: FnOnce(*mut ffi::cubeb_stream),
{
    use std::f32::consts::PI;
    const SAMPLE_FREQUENCY: u32 = 48_000;

    // Make sure the parameters meet the requirements of AudioUnitContext::stream_init
    // (in the comments).
    let mut stream_params = ffi::cubeb_stream_params::default();
    stream_params.format = ffi::CUBEB_SAMPLE_S16NE;
    stream_params.rate = SAMPLE_FREQUENCY;
    stream_params.prefs = ffi::CUBEB_STREAM_PREF_NONE;
    stream_params.channels = 1;
    stream_params.layout = ffi::CUBEB_LAYOUT_MONO;

    let (input_params, output_params) = match scope {
        Scope::Input => (
            &mut stream_params as *mut ffi::cubeb_stream_params,
            ptr::null_mut(),
        ),
        Scope::Output => (
            ptr::null_mut(),
            &mut stream_params as *mut ffi::cubeb_stream_params,
        ),
    };

    extern "C" fn state_callback(
        stream: *mut ffi::cubeb_stream,
        user_ptr: *mut c_void,
        state: ffi::cubeb_state,
    ) {
        assert!(!stream.is_null());
        assert!(!user_ptr.is_null());
        assert_ne!(state, ffi::CUBEB_STATE_ERROR);
    }

    extern "C" fn input_data_callback(
        stream: *mut ffi::cubeb_stream,
        user_ptr: *mut c_void,
        input_buffer: *const c_void,
        output_buffer: *mut c_void,
        nframes: i64,
    ) -> i64 {
        assert!(!stream.is_null());
        assert!(!user_ptr.is_null());
        assert!(!input_buffer.is_null());
        assert!(output_buffer.is_null());
        nframes
    }

    let mut position: i64 = 0; // TODO: Use Atomic instead.

    fn f32_to_i16_sample(x: f32) -> i16 {
        (x * f32::from(i16::max_value())) as i16
    }

    extern "C" fn output_data_callback(
        stream: *mut ffi::cubeb_stream,
        user_ptr: *mut c_void,
        input_buffer: *const c_void,
        output_buffer: *mut c_void,
        nframes: i64,
    ) -> i64 {
        assert!(!stream.is_null());
        assert!(!user_ptr.is_null());
        assert!(input_buffer.is_null());
        assert!(!output_buffer.is_null());

        let buffer = unsafe {
            let ptr = output_buffer as *mut i16;
            let len = nframes as usize;
            slice::from_raw_parts_mut(ptr, len)
        };

        let position = unsafe { &mut *(user_ptr as *mut i64) };

        // Generate tone on the fly.
        for data in buffer.iter_mut() {
            let t1 = (2.0 * PI * 350.0 * (*position) as f32 / SAMPLE_FREQUENCY as f32).sin();
            let t2 = (2.0 * PI * 440.0 * (*position) as f32 / SAMPLE_FREQUENCY as f32).sin();
            *data = f32_to_i16_sample(0.5 * (t1 + t2));
            *position += 1;
        }

        nframes
    }

    test_ops_stream_operation(
        "stream",
        ptr::null_mut(), // Use default input device.
        input_params,
        ptr::null_mut(), // Use default output device.
        output_params,
        4096, // TODO: Get latency by get_min_latency instead ?
        match scope {
            Scope::Input => Some(input_data_callback),
            Scope::Output => Some(output_data_callback),
        },
        Some(state_callback),
        &mut position as *mut i64 as *mut c_void,
        |stream| {
            assert_eq!(unsafe { OPS.stream_start.unwrap()(stream) }, ffi::CUBEB_OK);
            operation(stream);
            assert_eq!(unsafe { OPS.stream_stop.unwrap()(stream) }, ffi::CUBEB_OK);
        },
    );
}

// Plug and unplug devices, to test device collection changed callback
// ================================================================================================
#[ignore]
#[test]
fn test_plug_and_unplug_device() {
    test_plug_and_unplug_device_in_scope(Scope::Input);
    test_plug_and_unplug_device_in_scope(Scope::Output);
}

fn test_plug_and_unplug_device_in_scope(scope: Scope) {
    let default_device = test_get_default_device(scope.clone());
    if default_device.is_none() {
        println!("No device for {:?} to test", scope);
        return;
    }

    println!("Run test for {:?}", scope);
    println!("NOTICE: The test will hang if the default input or output is an aggregate device.\nWe will fix this later.");

    let default_device = default_device.unwrap();
    let is_input = test_device_in_scope(default_device, Scope::Input);
    let is_output = test_device_in_scope(default_device, Scope::Output);

    let mut context = AudioUnitContext::new();

    // Register the devices-changed callbacks.
    #[derive(Clone, PartialEq)]
    struct Counts {
        input: u32,
        output: u32,
    }
    impl Counts {
        fn new() -> Self {
            Self {
                input: 0,
                output: 0,
            }
        }
    }
    let counts = Arc::new(Notifier::new(Counts::new()));
    let counts_notifier_ptr = counts.as_ref() as *const Notifier<Counts>;

    assert!(context
        .register_device_collection_changed(
            DeviceType::INPUT,
            Some(input_changed_callback),
            counts_notifier_ptr as *mut c_void,
        )
        .is_ok());

    assert!(context
        .register_device_collection_changed(
            DeviceType::OUTPUT,
            Some(output_changed_callback),
            counts_notifier_ptr as *mut c_void,
        )
        .is_ok());

    let counts_watcher = Watcher::new(&counts);

    let mut device_plugger = TestDevicePlugger::new(scope).unwrap();

    {
        // Simulate adding devices and monitor the devices-changed callbacks.
        let mut counts_guard = counts.lock().unwrap();
        let counts_start = counts_guard.clone();

        assert!(device_plugger.plug().is_ok());

        counts_guard = counts_watcher
            .wait_while(counts_guard, |counts| {
                (is_input && counts.input == counts_start.input)
                    || (is_output && counts.output == counts_start.output)
            })
            .unwrap();

        // Check changed count.
        assert_eq!(counts_guard.input, if is_input { 1 } else { 0 });
        assert_eq!(counts_guard.output, if is_output { 1 } else { 0 });
    }

    {
        // Simulate removing devices and monitor the devices-changed callbacks.
        let mut counts_guard = counts.lock().unwrap();
        let counts_start = counts_guard.clone();

        assert!(device_plugger.unplug().is_ok());

        counts_guard = counts_watcher
            .wait_while(counts_guard, |counts| {
                (is_input && counts.input == counts_start.input)
                    || (is_output && counts.output == counts_start.output)
            })
            .unwrap();

        // Check changed count.
        assert_eq!(counts_guard.input, if is_input { 2 } else { 0 });
        assert_eq!(counts_guard.output, if is_output { 2 } else { 0 });
    }

    extern "C" fn input_changed_callback(context: *mut ffi::cubeb, data: *mut c_void) {
        println!(
            "Input device collection @ {:p} is changed. Data @ {:p}",
            context, data
        );
        let notifier = unsafe { &*(data as *const Notifier<Counts>) };
        {
            let mut counts = notifier.lock().unwrap();
            counts.input += 1;
            notifier.notify(counts);
        }
    }

    extern "C" fn output_changed_callback(context: *mut ffi::cubeb, data: *mut c_void) {
        println!(
            "output device collection @ {:p} is changed. Data @ {:p}",
            context, data
        );
        let notifier = unsafe { &*(data as *const Notifier<Counts>) };
        {
            let mut counts = notifier.lock().unwrap();
            counts.output += 1;
            notifier.notify(counts);
        }
    }

    context.register_device_collection_changed(DeviceType::OUTPUT, None, ptr::null_mut());
    context.register_device_collection_changed(DeviceType::INPUT, None, ptr::null_mut());
}

// Switch default devices used by the active streams, to test device changed callback
// ================================================================================================
#[ignore]
#[test]
fn test_register_device_changed_callback_to_check_default_device_changed_input() {
    test_register_device_changed_callback_to_check_default_device_changed(StreamType::INPUT);
}

#[ignore]
#[test]
fn test_register_device_changed_callback_to_check_default_device_changed_output() {
    test_register_device_changed_callback_to_check_default_device_changed(StreamType::OUTPUT);
}

#[ignore]
#[test]
fn test_register_device_changed_callback_to_check_default_device_changed_duplex() {
    test_register_device_changed_callback_to_check_default_device_changed(StreamType::DUPLEX);
}

fn test_register_device_changed_callback_to_check_default_device_changed(stm_type: StreamType) {
    println!("NOTICE: The test will hang if the default input or output is an aggregate device.\nWe will fix this later.");

    let inputs = if stm_type.contains(StreamType::INPUT) {
        let devices = test_get_devices_in_scope(Scope::Input).len();
        if devices >= 2 {
            Some(devices)
        } else {
            None
        }
    } else {
        None
    };

    let outputs = if stm_type.contains(StreamType::OUTPUT) {
        let devices = test_get_devices_in_scope(Scope::Output).len();
        if devices >= 2 {
            Some(devices)
        } else {
            None
        }
    } else {
        None
    };

    if inputs.is_none() && outputs.is_none() {
        println!("No enough devices to run the test!");
        return;
    }

    let changed_count = Arc::new(Notifier::new(0u32));
    let notifier_ptr = changed_count.as_ref() as *const Notifier<u32>;

    test_get_stream_with_device_changed_callback(
        "stream: test callback for default device changed",
        stm_type,
        None, // Use default input device.
        None, // Use default output device.
        notifier_ptr as *mut c_void,
        state_callback,
        device_changed_callback,
        |stream| {
            // If the duplex stream uses different input and output device,
            // an aggregate device will be created and it will work for this duplex stream.
            // This aggregate device will be added into the device list, but it won't
            // be assigned to the default device, since the device list for setting
            // default device is cached upon {input, output}_device_switcher is initialized.

            let changed_watcher = Watcher::new(&changed_count);

            if let Some(devices) = inputs {
                let mut device_switcher = TestDeviceSwitcher::new(Scope::Input);
                for _ in 0..devices {
                    // While the stream is re-initializing for the default device switch,
                    // switching for the default device again will be ignored.
                    while stream
                        .switching_device
                        .load(std::sync::atomic::Ordering::SeqCst)
                    {
                        std::hint::spin_loop()
                    }
                    let guard = changed_watcher.lock().unwrap();
                    let start_cnt = *guard;
                    device_switcher.next();
                    changed_watcher
                        .wait_while(guard, |cnt| *cnt == start_cnt)
                        .unwrap();
                }
            }

            if let Some(devices) = outputs {
                let mut device_switcher = TestDeviceSwitcher::new(Scope::Output);
                for _ in 0..devices {
                    // While the stream is re-initializing for the default device switch,
                    // switching for the default device again will be ignored.
                    while stream
                        .switching_device
                        .load(std::sync::atomic::Ordering::SeqCst)
                    {
                        std::hint::spin_loop()
                    }
                    let guard = changed_watcher.lock().unwrap();
                    let start_cnt = *guard;
                    device_switcher.next();
                    changed_watcher
                        .wait_while(guard, |cnt| *cnt == start_cnt)
                        .unwrap();
                }
            }
        },
    );

    extern "C" fn state_callback(
        stream: *mut ffi::cubeb_stream,
        _user_ptr: *mut c_void,
        state: ffi::cubeb_state,
    ) {
        assert!(!stream.is_null());
        assert_ne!(state, ffi::CUBEB_STATE_ERROR);
    }

    extern "C" fn device_changed_callback(data: *mut c_void) {
        println!("Device change callback. data @ {:p}", data);
        let notifier = unsafe { &*(data as *const Notifier<u32>) };
        let mut count_guard = notifier.lock().unwrap();
        *count_guard += 1;
        notifier.notify(count_guard);
    }
}

// Unplug the devices used by the active streams, to test
// 1) device changed callback, or state callback
// 2) stream reinitialization that may race with stream destroying
// ================================================================================================

// Input-only stream
// -----------------

// Unplug the non-default input device for an input stream
// ------------------------------------------------------------------------------------------------

#[ignore]
#[test]
fn test_destroy_input_stream_after_unplugging_a_nondefault_input_device() {
    // The stream can be destroyed before running device-changed event handler
    test_unplug_a_device_on_an_active_stream(StreamType::INPUT, Scope::Input, false, 0);
}

#[ignore]
#[test]
fn test_suspend_input_stream_by_unplugging_a_nondefault_input_device() {
    // Expect to get an error state callback by device-changed event handler
    test_unplug_a_device_on_an_active_stream(StreamType::INPUT, Scope::Input, false, 2000);
}

// Unplug the default input device for an input stream
// ------------------------------------------------------------------------------------------------
#[ignore]
#[test]
fn test_destroy_input_stream_after_unplugging_a_default_input_device() {
    // Expect to get an device-changed callback by device-changed event handler,
    // which will reinitialize the stream behind the scenes, at the same when
    // the stream is being destroyed
    test_unplug_a_device_on_an_active_stream(StreamType::INPUT, Scope::Input, true, 0);
}

#[ignore]
#[test]
fn test_reinit_input_stream_by_unplugging_a_default_input_device() {
    // Expect to get an device-changed callback by device-changed event handler,
    // which will reinitialize the stream behind the scenes
    test_unplug_a_device_on_an_active_stream(StreamType::INPUT, Scope::Input, true, 2000);
}

// Output-only stream
// ------------------

// Unplug the non-default output device for an output stream
// ------------------------------------------------------------------------------------------------
#[ignore]
#[test]
fn test_destroy_output_stream_after_unplugging_a_nondefault_output_device() {
    test_unplug_a_device_on_an_active_stream(StreamType::OUTPUT, Scope::Output, false, 0);
}

#[ignore]
#[test]
fn test_suspend_output_stream_by_unplugging_a_nondefault_output_device() {
    test_unplug_a_device_on_an_active_stream(StreamType::OUTPUT, Scope::Output, false, 2000);
}

// Unplug the default output device for an output stream
// ------------------------------------------------------------------------------------------------

#[ignore]
#[test]
fn test_destroy_output_stream_after_unplugging_a_default_output_device() {
    // Expect to get an device-changed callback by device-changed event handler,
    // which will reinitialize the stream behind the scenes, at the same when
    // the stream is being destroyed
    test_unplug_a_device_on_an_active_stream(StreamType::OUTPUT, Scope::Output, true, 0);
}

#[ignore]
#[test]
fn test_reinit_output_stream_by_unplugging_a_default_output_device() {
    // Expect to get an device-changed callback by device-changed event handler,
    // which will reinitialize the stream behind the scenes
    test_unplug_a_device_on_an_active_stream(StreamType::OUTPUT, Scope::Output, true, 2000);
}

// Duplex stream
// -------------

// Unplug the non-default input device for a duplex stream
// ------------------------------------------------------------------------------------------------

#[ignore]
#[test]
fn test_destroy_duplex_stream_after_unplugging_a_nondefault_input_device() {
    // The stream can be destroyed before running device-changed event handler
    test_unplug_a_device_on_an_active_stream(StreamType::DUPLEX, Scope::Input, false, 0);
}

#[ignore]
#[test]
fn test_suspend_duplex_stream_by_unplugging_a_nondefault_input_device() {
    // Expect to get an error state callback by device-changed event handler
    test_unplug_a_device_on_an_active_stream(StreamType::DUPLEX, Scope::Input, false, 2000);
}

// Unplug the non-default output device for a duplex stream
// ------------------------------------------------------------------------------------------------

#[ignore]
#[test]
fn test_destroy_duplex_stream_after_unplugging_a_nondefault_output_device() {
    test_unplug_a_device_on_an_active_stream(StreamType::DUPLEX, Scope::Output, false, 0);
}

#[ignore]
#[test]
fn test_suspend_duplex_stream_by_unplugging_a_nondefault_output_device() {
    test_unplug_a_device_on_an_active_stream(StreamType::DUPLEX, Scope::Output, false, 2000);
}

// Unplug the non-default in-out device for a duplex stream
// ------------------------------------------------------------------------------------------------
// TODO: Implement an in-out TestDevicePlugger

// Unplug the default input device for a duplex stream
// ------------------------------------------------------------------------------------------------

#[ignore]
#[test]
fn test_destroy_duplex_stream_after_unplugging_a_default_input_device() {
    // Expect to get an device-changed callback by device-changed event handler,
    // which will reinitialize the stream behind the scenes, at the same when
    // the stream is being destroyed
    test_unplug_a_device_on_an_active_stream(StreamType::DUPLEX, Scope::Input, true, 0);
}

#[ignore]
#[test]
fn test_reinit_duplex_stream_by_unplugging_a_default_input_device() {
    // Expect to get an device-changed callback by device-changed event handler,
    // which will reinitialize the stream behind the scenes
    test_unplug_a_device_on_an_active_stream(StreamType::DUPLEX, Scope::Input, true, 2000);
}

// Unplug the default ouput device for a duplex stream
// ------------------------------------------------------------------------------------------------

#[ignore]
#[test]
fn test_destroy_duplex_stream_after_unplugging_a_default_output_device() {
    // Expect to get an device-changed callback by device-changed event handler,
    // which will reinitialize the stream behind the scenes, at the same when
    // the stream is being destroyed
    test_unplug_a_device_on_an_active_stream(StreamType::DUPLEX, Scope::Output, true, 0);
}

#[ignore]
#[test]
fn test_reinit_duplex_stream_by_unplugging_a_default_output_device() {
    // Expect to get an device-changed callback by device-changed event handler,
    // which will reinitialize the stream behind the scenes
    test_unplug_a_device_on_an_active_stream(StreamType::DUPLEX, Scope::Output, true, 2000);
}

fn test_unplug_a_device_on_an_active_stream(
    stream_type: StreamType,
    device_scope: Scope,
    set_device_to_default: bool,
    wait_up_to_ms: u64,
) {
    let has_input = test_get_default_device(Scope::Input).is_some();
    let has_output = test_get_default_device(Scope::Output).is_some();

    if stream_type.contains(StreamType::INPUT) && !has_input {
        println!("No input device for input or duplex stream.");
        return;
    }

    if stream_type.contains(StreamType::OUTPUT) && !has_output {
        println!("No output device for output or duplex stream.");
        return;
    }

    let default_device_before_plugging = test_get_default_device(device_scope.clone()).unwrap();
    println!(
        "Before plugging, default {:?} device is {}",
        device_scope, default_device_before_plugging
    );

    let mut plugger = TestDevicePlugger::new(device_scope.clone()).unwrap();
    assert!(plugger.plug().is_ok());
    assert_ne!(plugger.get_device_id(), kAudioObjectUnknown);
    println!(
        "Create plugger device: {} for {:?}",
        plugger.get_device_id(),
        device_scope
    );

    let default_device_after_plugging = test_get_default_device(device_scope.clone()).unwrap();
    println!(
        "After plugging, default {:?} device is {}",
        device_scope, default_device_after_plugging
    );

    // The new device, plugger, is possible to be set to the default device.
    // Before running the test, we need to set the default device to the correct one.
    if set_device_to_default {
        // plugger should be the default device for the test.
        // If it's not, then set it to the default device.
        if default_device_after_plugging != plugger.get_device_id() {
            let prev_def_dev =
                test_set_default_device(plugger.get_device_id(), device_scope.clone()).unwrap();
            assert_eq!(prev_def_dev, default_device_after_plugging);
        }
    } else {
        // plugger should NOT be the default device for the test.
        // If it is, reset the default device to another one.
        if default_device_after_plugging == plugger.get_device_id() {
            let prev_def_dev =
                test_set_default_device(default_device_before_plugging, device_scope.clone())
                    .unwrap();
            assert_eq!(prev_def_dev, default_device_after_plugging);
        }
    }

    // Ignore the return devices' info since we only need to print them.
    let _ = get_devices_info_in_scope(device_scope.clone());
    println!(
        "Current default {:?} device is {}",
        device_scope,
        test_get_default_device(device_scope.clone()).unwrap()
    );

    let (input_device, output_device) = match device_scope {
        Scope::Input => (
            if set_device_to_default {
                None // default input device.
            } else {
                Some(plugger.get_device_id())
            },
            None,
        ),
        Scope::Output => (
            None,
            if set_device_to_default {
                None // default output device.
            } else {
                Some(plugger.get_device_id())
            },
        ),
    };

    #[derive(Clone, PartialEq)]
    struct Data {
        changed_count: u32,
        states: Vec<ffi::cubeb_state>,
    }

    impl Data {
        fn new() -> Self {
            Self {
                changed_count: 0,
                states: vec![],
            }
        }
    }

    let notifier = Arc::new(Notifier::new(Data::new()));
    let notifier_ptr = notifier.as_ref() as *const Notifier<Data>;

    test_get_stream_with_device_changed_callback(
        "stream: test stream reinit/destroy after unplugging a device",
        stream_type,
        input_device,
        output_device,
        notifier_ptr as *mut c_void,
        state_callback,
        device_changed_callback,
        |stream| {
            assert_eq!(stream.start(), Ok(()));

            println!(
                "Stream runs on the device {} for {:?}",
                plugger.get_device_id(),
                device_scope
            );

            let dev = plugger.get_device_id();
            let start_changed_count = {
                let guard = notifier.lock().unwrap();
                assert_eq!(guard.states.last().unwrap(), &ffi::CUBEB_STATE_STARTED);
                guard.changed_count
            };

            assert!(plugger.unplug().is_ok());

            let changed_watcher = Watcher::new(&notifier);

            if set_device_to_default {
                // The stream will be reinitialized if it follows the default input or output device.
                println!("Waiting for default device to change and reinit");
                let mut data_guard = notifier.lock().unwrap();
                data_guard = changed_watcher
                    .wait_while(data_guard, |data| {
                        data.changed_count == start_changed_count
                            || data.states.last().unwrap_or(&ffi::CUBEB_STATE_ERROR)
                                != &ffi::CUBEB_STATE_STARTED
                    })
                    .unwrap();
            } else if wait_up_to_ms > 0 {
                // stream can be dropped immediately before device-changed callback
                // so we only check the states if we wait for it explicitly.
                println!("Waiting for non-default device to enter error state");
                let mut data_guard = notifier.lock().unwrap();
                let (new_guard, timeout_res) = changed_watcher
                    .wait_timeout_while(data_guard, Duration::from_millis(wait_up_to_ms), |data| {
                        data.states.last().unwrap_or(&ffi::CUBEB_STATE_STARTED)
                            != &ffi::CUBEB_STATE_ERROR
                    })
                    .unwrap();
                assert!(!timeout_res.timed_out());
                data_guard = new_guard;
            }

            println!(
                "Device {} for {:?} has been unplugged. The default {:?} device now is {}",
                dev,
                device_scope,
                device_scope,
                test_get_default_device(device_scope.clone()).unwrap()
            );

            println!("The stream is going to be destroyed soon");
        },
    );

    extern "C" fn state_callback(
        stream: *mut ffi::cubeb_stream,
        user_ptr: *mut c_void,
        state: ffi::cubeb_state,
    ) {
        println!("Device change callback. user_ptr @ {:p}", user_ptr);
        assert!(!stream.is_null());
        println!(
            "state: {}",
            match state {
                ffi::CUBEB_STATE_STARTED => "started",
                ffi::CUBEB_STATE_STOPPED => "stopped",
                ffi::CUBEB_STATE_DRAINED => "drained",
                ffi::CUBEB_STATE_ERROR => "error",
                _ => "unknown",
            }
        );
        let notifier = unsafe { &mut *(user_ptr as *mut Notifier<Data>) };
        let mut data_guard = notifier.lock().unwrap();
        data_guard.states.push(state);
        notifier.notify(data_guard);
    }

    extern "C" fn device_changed_callback(user_ptr: *mut c_void) {
        println!("Device change callback. user_ptr @ {:p}", user_ptr);
        let notifier = unsafe { &mut *(user_ptr as *mut Notifier<Data>) };
        let mut data_guard = notifier.lock().unwrap();
        data_guard.changed_count += 1;
        notifier.notify(data_guard);
    }
}

struct Notifier<T> {
    value: Mutex<T>,
    cvar: Condvar,
}

impl<T> Notifier<T> {
    fn new(value: T) -> Self {
        Self {
            value: Mutex::new(value),
            cvar: Condvar::new(),
        }
    }

    fn lock(&self) -> LockResult<MutexGuard<'_, T>> {
        self.value.lock()
    }

    fn notify(&self, _guard: MutexGuard<'_, T>) {
        self.cvar.notify_all();
    }
}

struct Watcher<T: Clone + PartialEq> {
    notifier: Arc<Notifier<T>>,
}

impl<T: Clone + PartialEq> Watcher<T> {
    fn new(value: &Arc<Notifier<T>>) -> Self {
        Self {
            notifier: Arc::clone(value),
        }
    }

    fn lock(&self) -> LockResult<MutexGuard<'_, T>> {
        self.notifier.lock()
    }

    fn wait_while<'a, F>(
        &self,
        guard: MutexGuard<'a, T>,
        condition: F,
    ) -> LockResult<MutexGuard<'a, T>>
    where
        F: FnMut(&mut T) -> bool,
    {
        self.notifier.cvar.wait_while(guard, condition)
    }

    fn wait_timeout_while<'a, F>(
        &self,
        guard: MutexGuard<'a, T>,
        dur: Duration,
        condition: F,
    ) -> LockResult<(MutexGuard<'a, T>, WaitTimeoutResult)>
    where
        F: FnMut(&mut T) -> bool,
    {
        self.notifier.cvar.wait_timeout_while(guard, dur, condition)
    }
}

fn test_get_stream_with_device_changed_callback<F>(
    name: &'static str,
    stm_type: StreamType,
    input_device: Option<AudioObjectID>,
    output_device: Option<AudioObjectID>,
    data: *mut c_void,
    state_callback: extern "C" fn(*mut ffi::cubeb_stream, *mut c_void, ffi::cubeb_state),
    device_changed_callback: extern "C" fn(*mut c_void),
    operation: F,
) where
    F: FnOnce(&mut AudioUnitStream),
{
    test_get_stream_with_default_data_callback_by_type(
        name,
        stm_type,
        input_device,
        output_device,
        state_callback,
        data,
        |stream| {
            assert!(stream
                .register_device_changed_callback(Some(device_changed_callback))
                .is_ok());
            operation(stream);
            assert!(stream.register_device_changed_callback(None).is_ok());
        },
    );
}

// test_device_collection_change_on_default_device_change
// ------------------------------------

// Helper function to test device collection change when default device changes
fn test_default_device_change_triggers_callback(scope: Scope, device_type: DeviceType) {
    test_default_device_change_triggers_callback_multiple(&[(scope, device_type)]);
}

// Helper function to test device collection change for both input/output (e.g.
// when in duplex)
fn test_default_device_change_triggers_callback_multiple(scope_types: &[(Scope, DeviceType)]) {
    use std::collections::HashMap;
    use std::sync::Arc;
    use std::time::Duration;

    #[derive(Clone, Default, PartialEq)]
    struct CallbackCounts {
        input_count: u32,
        output_count: u32,
    }

    // Check if we have enough devices for all requested scopes, and find our
    // current and next devices
    let mut device_info = HashMap::new();
    for (scope, _) in scope_types {
        let devices = test_get_devices_in_scope(scope.clone());
        if devices.len() < 2 {
            let scope_name = match scope {
                Scope::Input => "input",
                Scope::Output => "output",
            };
            eprintln!(
                "Skipping test: need at least 2 {} devices, found {}",
                scope_name,
                devices.len()
            );
            return;
        }

        let initial_default = test_get_default_device(scope.clone()).unwrap();
        let alternate_device = devices.iter().find(|&id| *id != initial_default).unwrap();

        device_info.insert(scope.clone(), (initial_default, *alternate_device));
    }

    let notifier = Arc::new(Notifier::new(CallbackCounts::default()));
    let notifier_ptr = notifier.as_ref() as *const Notifier<CallbackCounts>;

    let mut context = AudioUnitContext::new();

    extern "C" fn input_device_collection_changed_callback(
        _context: *mut ffi::cubeb,
        user_ptr: *mut c_void,
    ) {
        let notifier = unsafe { &*(user_ptr as *const Notifier<CallbackCounts>) };
        let mut guard = notifier.lock().unwrap();
        guard.input_count += 1;
        notifier.notify(guard);
    }

    extern "C" fn output_device_collection_changed_callback(
        _context: *mut ffi::cubeb,
        user_ptr: *mut c_void,
    ) {
        let notifier = unsafe { &*(user_ptr as *const Notifier<CallbackCounts>) };
        let mut guard = notifier.lock().unwrap();
        guard.output_count += 1;
        notifier.notify(guard);
    }

    let callback_data_ptr = notifier_ptr as *mut c_void;

    // Register callbacks for all requested device types
    for (_, device_type) in scope_types {
        let callback_fn = match *device_type {
            DeviceType::INPUT => input_device_collection_changed_callback,
            DeviceType::OUTPUT => output_device_collection_changed_callback,
            _ => panic!("Unsupported device type: {:?}", device_type),
        };

        assert!(context
            .register_device_collection_changed(*device_type, Some(callback_fn), callback_data_ptr)
            .is_ok());
    }

    // Change default devices and wait for callbacks
    let watcher = Watcher::new(&notifier);

    for (scope, device_type) in scope_types {
        let (_initial_default, alternate_device) = device_info.get(scope).unwrap();

        // Get current count before changing device
        let expected_count_before = {
            let guard = watcher.lock().unwrap();
            match *device_type {
                DeviceType::INPUT => guard.input_count,
                DeviceType::OUTPUT => guard.output_count,
                _ => panic!("Unsupported device type"),
            }
        };

        // Change device
        test_set_default_device(*alternate_device, scope.clone())
            .expect("Failed to set default device");

        // Wait for callback
        let guard = watcher.lock().unwrap();
        let (result_guard, _timeout_result) = watcher
            .wait_timeout_while(guard, Duration::from_secs(5), |data| match *device_type {
                DeviceType::INPUT => data.input_count == expected_count_before,
                DeviceType::OUTPUT => data.output_count == expected_count_before,
                _ => false,
            })
            .unwrap();

        let callback_received = match *device_type {
            DeviceType::INPUT => result_guard.input_count > expected_count_before,
            DeviceType::OUTPUT => result_guard.output_count > expected_count_before,
            _ => false,
        };

        assert!(
            callback_received,
            "{:?} device collection change callback was not called",
            device_type
        );

        drop(result_guard); // Release the lock before next iteration
    }

    // Unregister callbacks
    for (_, device_type) in scope_types {
        context.register_device_collection_changed(*device_type, None, ptr::null_mut());
    }

    // Restore original default devices
    for (scope, _) in scope_types {
        let (initial_default, _) = device_info.get(scope).unwrap();
        test_set_default_device(*initial_default, scope.clone())
            .expect("Failed to restore default device");
    }
}

#[ignore]
#[test]
fn test_device_collection_change_on_default_input_device_change() {
    test_default_device_change_triggers_callback(Scope::Input, DeviceType::INPUT);
}

#[ignore]
#[test]
fn test_device_collection_change_on_default_output_device_change() {
    test_default_device_change_triggers_callback(Scope::Output, DeviceType::OUTPUT);
}

#[ignore]
#[test]
fn test_device_collection_change_on_default_device_change_duplex() {
    test_default_device_change_triggers_callback_multiple(&[
        (Scope::Input, DeviceType::INPUT),
        (Scope::Output, DeviceType::OUTPUT),
    ]);
}
