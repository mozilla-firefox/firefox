#![allow(dead_code)]

use std::{collections::HashMap, fs::File};

use serde::Serialize;

#[derive(Debug, Serialize)]
struct Config {
    boolean: bool,
    float: f32,
    map: HashMap<u8, char>,
    nested: Nested,
    tuple: (u32, u32),
    vec: Vec<Nested>,
}

#[derive(Debug, Serialize)]
struct Nested {
    a: String,
    b: char,
}

fn main() {
    let config = Config {
        boolean: true,
        float: 8.2,
        map: [(1, '1'), (2, '4'), (3, '9'), (4, '1'), (5, '2'), (6, '3')]
            .into_iter()
            .collect(),
        nested: Nested {
            a: String::from("Decode me!"),
            b: 'z',
        },
        tuple: (3, 7),
        vec: vec![
            Nested {
                a: String::from("Nested 1"),
                b: 'x',
            },
            Nested {
                a: String::from("Nested 2"),
                b: 'y',
            },
            Nested {
                a: String::from("Nested 3"),
                b: 'z',
            },
        ],
    };

    let f = File::options()
        .create(true)
        .write(true)
        .open("example-out.ron")
        .expect("Failed opening file");

    ron::Options::default()
        .to_io_writer_pretty(f, &config, ron::ser::PrettyConfig::new())
        .expect("Failed to write to file");
}
