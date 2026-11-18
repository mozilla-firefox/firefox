# Instrumenting Rust

There are multiple ways to use the profiler with Rust. Native stack sampling already
includes the Rust frames without special handling. There is the "Native Stacks"
profiler feature (via <about:profiling>), which enables stack walking for native code.
This is most likely turned on already for every profiler presets.

In addition to that, there is a profiler Rust API to instrument the Rust code
and add more information to the profile data. There are three main functionalities
to use:

1. Register Rust threads with the profiler, so the profiler can record these threads.
2. Add stack frame labels to annotate and categorize a part of the stack.
3. Add markers to specifically mark instants in time, or durations. This can be
   helpful to make sense of a particular piece of the code, or record events that
   normally wouldn't show up in samples.

## Crate to Include as a Dependency

Profiler Rust API is located inside the `gecko-profiler` crate. This needs to
be included in the project dependencies before the following functionalities can
be used.

To be able to include it, a new dependency entry needs to be added to the project's
`Cargo.toml` file like this:

```toml
[dependencies]
gecko-profiler = { path = "../../tools/profiler/rust-api" }
```

Note that the relative path needs to be updated depending on the project's location
in the Firefox repository.

After updating `Cargo.toml`, `cargo update -p gkrust-shared` needs to be
executed to update the `Cargo.lock` file.

## Registering Threads

To be able to see the threads in the profile data, they need to be registered
with the profiler. Also, they need to be unregistered when they are exiting.
It's important to give a unique name to the thread, so they can be filtered easily.

Registering and unregistering a thread is straightforward:

```rust
// Register it with a given name.
gecko_profiler::register_thread("Thread Name");
// After doing some work, and right before exiting the thread, unregister it.
gecko_profiler::unregister_thread();
```

For example, here's how to register and unregister a simple thread:

```rust
let thread_name = "New Thread";
std::thread::Builder::new()
    .name(thread_name.into())
    .spawn(move || {
        gecko_profiler::register_thread(thread_name);
        // DO SOME WORK
        gecko_profiler::unregister_thread();
    })
    .unwrap();
```

Or with a thread pool:

```rust
let worker = rayon::ThreadPoolBuilder::new()
    .thread_name(move |idx| format!("Worker#{}", idx))
    .start_handler(move |idx| {
        gecko_profiler::register_thread(&format!("Worker#{}", idx));
    })
    .exit_handler(|_idx| {
        gecko_profiler::unregister_thread();
    })
    .build();
```

:::{note}
Registering a thread only will not make it appear in the profile data. In
addition, it needs to be added to the "Threads" filter in <about:profiling>.
This filter input is a comma-separated list. It matches partial names and
supports the wildcard `*`.
:::

## Adding Stack Frame Labels

Stack frame labels are useful for annotating a part of the call stack with a
category. The category will appear in the various places on the Firefox Profiler
analysis page like timeline, call tree tab, flame graph tab, etc.

`gecko_profiler_label!` macro is used to add a new label frame. The added label
frame will exist between the call of this macro and the end of the current scope.

Adding a stack frame label:

```rust
// Marking the stack as "Layout" category, no subcategory provided.
gecko_profiler_label!(Layout);
// Marking the stack as "JavaScript" category and "Parsing" subcategory.
gecko_profiler_label!(JavaScript, Parsing);

// Or the entire function scope can be marked with a procedural macro. This is
// essentially a syntactical sugar and it expands into a function with a
// gecko_profiler_label! call at the very start:
#[gecko_profiler_fn_label(DOM)]
fn foo(bar: u32) -> u32 {
    bar
}
```

See the list of all profiling categories in the {searchfox}`profiling_categories.yaml <mozglue/baseprofiler/build/profiling_categories.yaml>` file.

## Adding Markers

Markers are packets of arbitrary data that are added to a profile by the Firefox code,
usually to indicate something important happening at a point in time, or during an interval of time.

Each marker has a name, a category, some common optional information (timing, backtrace, etc.),
and an optional payload of a specific type (containing arbitrary data relevant to that type).

:::{note}
This guide explains Rust markers in depth. To learn more about how to add a
marker in C++, JavaScript or JVM, please take a look at their documentation
in {doc}`markers-guide` or {doc}`instrumenting-javascript`,
{doc}`instrumenting-android` respectively.
:::

### Examples

Short examples, details are below.

```rust
// Record a simple marker with the category of Graphics, DisplayListBuilding.
gecko_profiler::add_untyped_marker(
    // Name of the marker as a string.
    "Marker Name",
    // Category with an optional sub-category.
    gecko_profiler_category!(Graphics, DisplayListBuilding),
    // MarkerOptions that keeps options like marker timing and marker stack.
    // It will be a point in type by default.
    Default::default(),
);
```

```rust
// Create a marker with some additional text information.
let info = "info about this marker";
gecko_profiler::add_text_marker(
    // Name of the marker as a string.
    "Marker Name",
    // Category with an optional sub-category.
    gecko_profiler_category!(DOM),
    // MarkerOptions that keeps options like marker timing and marker stack.
    MarkerOptions {
        timing: MarkerTiming::instant_now(),
        ..Default::default()
    },
    // Additional information as a string.
    info,
);
```

```rust
// Record a custom marker of type `ExampleNumberMarker` (see definition below).
gecko_profiler::add_marker(
    // Name of the marker as a string.
    "Marker Name",
    // Category with an optional sub-category.
    gecko_profiler_category!(Graphics, DisplayListBuilding),
    // MarkerOptions that keeps options like marker timing and marker stack.
    Default::default(),
    // Marker payload.
    ExampleNumberMarker { number: 5 },
);

....

// Marker type definition. It needs to derive Serialize, Deserialize.
#[derive(Serialize, Deserialize, Debug)]
pub struct ExampleNumberMarker {
    number: i32,
}

// Marker payload needs to implement the ProfilerMarker trait.
impl gecko_profiler::ProfilerMarker for ExampleNumberMarker {
    // Unique marker type name.
    fn marker_type_name() -> &'static str {
        "example number"
    }
    // Data specific to this marker type, serialized to JSON for profiler.firefox.com.
    fn stream_json_marker_data(&self, json_writer: &mut gecko_profiler::JSONWriter) {
        json_writer.int_property("number", self.number.into());
    }
    // Where and how to display the marker and its data.
    fn marker_type_display() -> gecko_profiler::MarkerSchema {
        use gecko_profiler::marker::schema::*;
        let mut schema = MarkerSchema::new(&[Location::MarkerChart]);
        schema.set_chart_label("Name: {marker.name}");
        schema.add_key_label_format("number", "Number", Format::Integer);
        schema
    }
}
```

### Untyped Markers

Untyped markers don't carry any information apart from common marker data:
Name, category, options.

```rust
gecko_profiler::add_untyped_marker(
    // Name of the marker as a string.
    "Marker Name",
    // Category with an optional sub-category.
    gecko_profiler_category!(Graphics, DisplayListBuilding),
    // MarkerOptions that keeps options like marker timing and marker stack.
    MarkerOptions {
        timing: MarkerTiming::instant_now(),
        ..Default::default()
    },
);
```

1. Marker name
   : The first argument is the name of this marker. This will be displayed in most places
   the marker is shown. It can be a literal string, or any dynamic string.
2. {searchfox}`Profiling category pair <__GENERATED__/tools/profiler/rust-api/src/gecko_bindings/profiling_categories.rs>`
   : A category + subcategory pair from the {searchfox}`the list of categories <mozglue/baseprofiler/build/profiling_categories.yaml>`.
   `gecko_profiler_category!` macro should be used to create a profiling category
   pair since it's easier to use, e.g. `gecko_profiler_category!(JavaScript, Parsing)`.
   Second parameter can be omitted to use the default subcategory directly.
   `gecko_profiler_category!` macro is encouraged to use, but `ProfilingCategoryPair`
   enum can also be used if needed.
3. [MarkerOptions]
   : See the options below. It can be omitted if there are no arguments with `Default::default()`.
   Some options can also be omitted, `MarkerOptions {<options>, ..Default::default()}`,
   with one or more of the following options types:

   - [MarkerTiming]
     : This specifies an instant or interval of time. It defaults to the current instant if
     left unspecified. Otherwise use `MarkerTiming::instant_at(ProfilerTime)` or
     `MarkerTiming::interval(pt1, pt2)`; timestamps are usually captured with
     `ProfilerTime::Now()`. It is also possible to record only the start or the end of an
     interval, pairs of start/end markers will be matched by their name.
   - [MarkerStack]
     : By default, markers do not record a "stack" (or "backtrace"). To record a stack at
     this point, in the most efficient manner, specify `MarkerStack::Full`. To
     capture a stack without native frames for reduced overhead, specify
     `MarkerStack::NonNative`.

     *Note: Currently, all C++ marker options are not present in the Rust side. They will
     be added in the future.*

### Text Markers

Text markers are very common, they carry an extra text as a fourth argument, in addition to
the marker name. Use the following macro:

```rust
let info = "info about this marker";
gecko_profiler::add_text_marker(
    // Name of the marker as a string.
    "Marker Name",
    // Category with an optional sub-category.
    gecko_profiler_category!(DOM),
    // MarkerOptions that keeps options like marker timing and marker stack.
    MarkerOptions {
        stack: MarkerStack::Full,
        ..Default::default()
    },
    // Additional information as a string.
    info,
);
```

As useful as it is, using an expensive `format!` operation to generate a complex text
comes with a variety of issues. It can leak potentially sensitive information
such as URLs during the profile sharing step. profiler.firefox.com cannot
access the information programmatically. It won't get the formatting benefits of the
built-in marker schema. Please consider using a custom marker type to separate and
better present the data.

### Other Typed Markers

From Rust code, a marker of some type `YourMarker` (details about type definition follow) can be
recorded like this:

```rust
gecko_profiler::add_marker(
    // Name of the marker as a string.
    "Marker Name",
    // Category with an optional sub-category.
    gecko_profiler_category!(JavaScript),
    // MarkerOptions that keeps options like marker timing and marker stack.
    Default::default(),
    // Marker payload.
    YourMarker { number: 5, text: "some string".to_string() },
);
```

After the first three common arguments (like in `gecko_profiler::add_untyped_marker`),
there is a marker payload struct and it needs to be defined. Let's take a look at
how to define it.

### How to Define New Marker Types

Each marker type must be defined once and only once.
The definition is a Rust `struct`, it's constructed when recording markers of
that type in Rust. Each marker struct holds the data that is required for them
to show in the profiler.firefox.com.
By convention, the suffix "Marker" is recommended to better distinguish them
from non-profiler entities in the source.

Each marker payload must derive `serde::Serialize` and `serde::Deserialize`.
They are also exported from `gecko-profiler` crate if a project doesn't have it.
Each marker payload should include its data as its fields like this:

```rust
#[derive(Serialize, Deserialize, Debug)]
pub struct YourMarker {
    number: i32,
    text: String,
}
```

Each marker struct must also implement the [ProfilerMarker] trait.

#### `ProfilerMarker` trait

[ProfilerMarker] trait must be implemented for all marker types. Its methods are
similar to C++ counterparts, please refer to {ref}`the C++ markers guide to learn
more about them <how-to-define-new-marker-types>`. It includes three methods that
needs to be implemented:

1. `marker_type_name() -> &'static str`:
   : A marker type must have a unique name, it is used to keep track of the type of
   markers in the profiler storage, and to identify them uniquely on profiler.firefox.com.
   (It does not need to be the same as the struct's name.)

     E.g.:

     ```rust
     fn marker_type_name() -> &'static str {
         "your marker type"
     }
     ```

2. `stream_json_marker_data(&self, json_writer: &mut JSONWriter)`
   : All markers of any type have some common data: A name, a category, options like
   timing, etc. as previously explained.

     In addition, a certain marker type may carry zero of more arbitrary pieces of
     information, and they are always the same for all markers of that type.

     These are defined in a special static member function `stream_json_marker_data`.

     It's a member method and takes a `&mut JSONWriter` as a parameter,
     it will be used to stream the data as JSON, to later be read by
     profiler.firefox.com. See [JSONWriter object and its methods].

     E.g.:

     ```rust
     fn stream_json_marker_data(&self, json_writer: &mut JSONWriter) {
         json_writer.int_property("number", self.number.into());
         json_writer.string_property("text", &self.text);
     }
     ```

3. `marker_type_display() -> schema::MarkerSchema`
   : Now that how to stream type-specific data (from Firefox to
   profiler.firefox.com) is defined, it needs to be described where and how this
   data will be displayed on profiler.firefox.com.

     The static member function `marker_type_display` returns an opaque `MarkerSchema`
     object, which will be forwarded to profiler.firefox.com.

     See the [MarkerSchema::Location enumeration for the full list]. Also see the
     [MarkerSchema struct for its possible methods].

     E.g.:

     ```rust
     fn marker_type_display() -> schema::MarkerSchema {
         // Import MarkerSchema related types for easier use.
         use crate::marker::schema::*;
         // Create a MarkerSchema struct with a list of locations provided.
         // One or more constructor arguments determine where this marker will be displayed in
         // the profiler.firefox.com UI.
         let mut schema = MarkerSchema::new(&[Location::MarkerChart]);

         // Some labels can optionally be specified, to display certain information in different
         // locations: set_chart_label, set_tooltip_label, and set_table_label``; or
         // set_all_labels to define all of them the same way.
         schema.set_all_labels("{marker.name} - {marker.data.number});

         // Next, define the main display of marker data, which will appear in the Marker Chart
         // tooltips and the Marker Table sidebar.
         schema.add_key_label_format("number", "Number", Format::Number);
         schema.add_key_label_format("text", "Text", Format::String);
         schema.add_static_label_value("Help", "This is my own marker type");

         // Lastly, return the created schema.
         schema
     }
     ```

     Note that the strings in `set_all_labels` may refer to marker data within braces:

   - `{marker.name}`: Marker name.
   - `{marker.data.X}`: Type-specific data, as streamed with property name "X"
     from `stream_json_marker_data`.

     {ref}`See the C++ markers guide for more details about it <marker-type-display-schema>`.

[jsonwriter object and its methods]: https://searchfox.org/mozilla-central/define?q=rust_analyzer::cargo::gecko_profiler::0_1_0::json_writer::JSONWriter
[markeroptions]: https://searchfox.org/mozilla-central/define?q=rust_analyzer::cargo::gecko_profiler::0_1_0::options::marker::MarkerOptions
[markerschema struct for its possible methods]: https://searchfox.org/mozilla-central/define?q=rust_analyzer::cargo::gecko_profiler::0_1_0::schema::marker::MarkerSchema
[markerschema::location enumeration for the full list]: https://searchfox.org/mozilla-central/define?q=T_mozilla%3A%3AMarkerSchema%3A%3ALocation
[markerstack]: https://searchfox.org/mozilla-central/define?q=rust_analyzer::cargo::gecko_profiler::0_1_0::options::marker::[MarkerStack]
[markertiming]: https://searchfox.org/mozilla-central/define?q=rust_analyzer::cargo::gecko_profiler::0_1_0::options::marker::MarkerTiming
[profilermarker]: https://searchfox.org/mozilla-central/define?q=rust_analyzer::cargo::gecko_profiler::0_1_0::marker::ProfilerMarker
