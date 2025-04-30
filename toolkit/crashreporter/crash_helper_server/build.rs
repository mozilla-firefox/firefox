/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::env;
use std::fs::{self, File};
use std::io::{Error, Write};
use std::path::Path;
use std::vec::Vec;

use yaml_rust::{Yaml, YamlLoader};

fn main() -> Result<(), Error> {
    generate_annotations()?;
    Ok(())
}

struct Annotation {
    name: String,
    type_string: String,
    altname: Option<String>,
    skip_if: Option<String>,
}

impl Annotation {
    fn new(key: &Yaml, value: &Yaml) -> Result<Annotation, Error> {
        let name = key.as_str().ok_or(Error::other("Not a string"))?;
        let raw_type = value["type"].as_str().ok_or(Error::other("Missing type"))?;
        let type_string = Annotation::type_str_to_value(raw_type)?;
        let altname = value["altname"].as_str().map(str::to_owned);
        // We don't care about the contents of the `ping` field for the time being
        let skip_if = value["skip_if"].as_str().map(str::to_owned);

        Ok(Annotation {
            name: name.to_owned(),
            type_string,
            altname,
            skip_if,
        })
    }

    fn type_str_to_value(raw_type: &str) -> Result<String, Error> {
        match raw_type {
            "string" => Ok("String"),
            "boolean" => Ok("Boolean"),
            "u32" => Ok("U32"),
            "u64" => Ok("U64"),
            "usize" => Ok("USize"),
            "object" => Ok("Object"),
            _ => Err(Error::other("Invalid type")),
        }
        .map(str::to_owned)
    }
}

fn read_annotations(doc: &Yaml) -> Result<Vec<Annotation>, Error> {
    let raw_annotations = doc.as_hash().ok_or(Error::other(
        "Invalid data in YAML file, expected a hash".to_string(),
    ))?;

    let mut annotations = Vec::<Annotation>::new();
    for annotation in raw_annotations {
        let annotation = Annotation::new(annotation.0, annotation.1)?;
        annotations.push(annotation);
    }

    annotations.sort_by(|a, b| {
        let a_lower = a.name.to_ascii_lowercase();
        let b_lower = b.name.to_ascii_lowercase();
        a_lower.cmp(&b_lower)
    });

    Ok(annotations)
}

fn generate_annotation_enum(annotations: &[Annotation]) -> Result<String, Error> {
    let mut annotations_enum = String::new();

    for (index, value) in annotations.iter().enumerate() {
        let entry = format!("    {} = {index:},\n", value.name);
        annotations_enum.push_str(&entry);
    }

    let count = format!("    Count = {},", annotations.len());
    annotations_enum.push_str(&count);

    Ok(annotations_enum)
}

fn generate_annotation_types(annotations: &[Annotation]) -> Result<String, Error> {
    let mut types_array = String::new();
    for value in annotations.iter() {
        let entry = format!("    CrashAnnotationType::{},\n", value.type_string);
        types_array.push_str(&entry);
    }

    // Pop the last newline
    types_array.pop();

    Ok(types_array)
}

fn generate_annotation_names(annotations: &[Annotation]) -> Result<String, Error> {
    let mut names_array = String::new();
    for value in annotations.iter() {
        let name = value.altname.as_ref().unwrap_or(&value.name);
        let entry = format!("    \"{}\",\n", name);
        names_array.push_str(&entry);
    }

    // Pop the last newline
    names_array.pop();

    Ok(names_array)
}

fn generate_annotation_skiplist(annotations: &[Annotation]) -> Result<String, Error> {
    let mut skiplist = String::new();
    for annotation in annotations.iter() {
        if let Some(skip_if) = &annotation.skip_if {
            let entry = format!(
                "    CrashAnnotationSkipValue {{ annotation: CrashAnnotation::{}, value: b\"{}\" }},\n",
                &annotation.name, skip_if
            );
            skiplist.push_str(&entry);
        }
    }

    // Pop the last newline
    skiplist.pop();

    Ok(skiplist)
}

// Generate Rust code to manipulate crash annotations
fn generate_annotations() -> Result<(), Error> {
    const CRASH_ANNOTATIONS_YAML: &str = "../CrashAnnotations.yaml";
    const CRASH_ANNOTATIONS_TEMPLATE: &str = "../crash_annotations.rs.in";

    let out_dir = env::var("OUT_DIR").unwrap();
    let annotations_path = Path::new(&out_dir).join("crash_annotations.rs");
    let mut annotations_file = File::create(annotations_path)?;

    let template = fs::read_to_string(CRASH_ANNOTATIONS_TEMPLATE)?;
    let yaml_str = fs::read_to_string(CRASH_ANNOTATIONS_YAML)?;
    let yaml_doc = YamlLoader::load_from_str(&yaml_str)
        .map_err(|e| Error::other(format!("Failed to parse YAML file: {}", e)))?;

    let doc = &yaml_doc[0];
    let annotations = read_annotations(doc)?;

    let annotations_enum = generate_annotation_enum(&annotations)?;
    let annotations_types = generate_annotation_types(&annotations)?;
    let annotations_names = generate_annotation_names(&annotations)?;
    let skiplist = generate_annotation_skiplist(&annotations)?;

    let compiled_template = template
        .replace("${enum}", &annotations_enum)
        .replace("${types}", &annotations_types)
        .replace("${names}", &annotations_names)
        .replace("${skiplist}", &skiplist);
    write!(&mut annotations_file, "{}", compiled_template)?;

    println!("cargo:rerun-if-changed={CRASH_ANNOTATIONS_YAML}");
    println!("cargo:rerun-if-changed={CRASH_ANNOTATIONS_TEMPLATE}");

    Ok(())
}
