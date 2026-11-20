// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

mod common;
use crate::common::*;

use serde_json::json;

use glean_core::metrics::*;
use glean_core::storage::StorageManager;
use glean_core::{test_get_num_recorded_errors, ErrorType};
use glean_core::{CommonMetricData, Lifetime};

#[test]
fn can_create_dual_labeled_counter_metric() {
    let (glean, _t) = new_glean(None);
    let dual_labeled_counter = DualLabeledCounterMetric::new(
        CommonMetricData {
            name: "dual_labeled_counter".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        Some(vec!["key1".into()]),
        Some(vec!["category1".into()]),
    );

    let metric = dual_labeled_counter.get("key1", "category1");
    metric.add_sync(&glean, 1);

    let snapshot = StorageManager
        .snapshot_as_json(glean.storage(), "store1", true)
        .unwrap();

    assert_eq!(
        json!({
            "dual_labeled_counter": {
                "telemetry.dual_labeled_counter": {
                    "key1": {
                        "category1": 1
                    }
                }
            }
        }),
        snapshot
    );
}

#[test]
fn can_use_multiple_labels() {
    let (glean, _t) = new_glean(None);
    let dual_labeled_counter = DualLabeledCounterMetric::new(
        CommonMetricData {
            name: "dual_labeled_counter".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        None,
        None,
    );

    let metric = dual_labeled_counter.get("key1", "category1");
    metric.add_sync(&glean, 1);

    let metric = dual_labeled_counter.get("key1", "category2");
    metric.add_sync(&glean, 2);

    let metric = dual_labeled_counter.get("key2", "category1");
    metric.add_sync(&glean, 3);

    let metric = dual_labeled_counter.get("key2", "category3");
    metric.add_sync(&glean, 4);

    let snapshot = StorageManager
        .snapshot_as_json(glean.storage(), "store1", true)
        .unwrap();

    assert_eq!(
        json!({
            "dual_labeled_counter": {
                "telemetry.dual_labeled_counter": {
                    "key1": {
                        "category1": 1,
                        "category2": 2
                    },
                    "key2": {
                        "category1": 3,
                        "category3": 4
                    }
                }
            }
        }),
        snapshot
    );
}

#[test]
fn can_record_error_for_submetric() {
    let (glean, _t) = new_glean(None);
    let dual_labeled_counter = DualLabeledCounterMetric::new(
        CommonMetricData {
            name: "dual_labeled_counter".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        Some(vec!["key1".into()]),
        Some(vec!["category1".into()]),
    );

    let metric = dual_labeled_counter.get("key1", "category1");
    metric.add_sync(&glean, -1);

    // Make sure that the errors have been recorded
    assert_eq!(
        Ok(1),
        test_get_num_recorded_errors(&glean, metric.meta(), ErrorType::InvalidValue)
    );
}

#[test]
fn labels_are_checked_against_static_list() {
    let (glean, _t) = new_glean(None);
    let dual_labeled_counter = DualLabeledCounterMetric::new(
        CommonMetricData {
            name: "dual_labeled_counter".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        Some(vec!["key1".into(), "key2".into()]),
        Some(vec!["category1".into(), "category2".into()]),
    );

    let metric = dual_labeled_counter.get("key1", "category1");
    metric.add_sync(&glean, 1);

    let metric = dual_labeled_counter.get("key1", "category2");
    metric.add_sync(&glean, 2);

    let metric = dual_labeled_counter.get("key2", "category1");
    metric.add_sync(&glean, 3);

    let metric = dual_labeled_counter.get("key2", "category2");
    metric.add_sync(&glean, 4);

    // All non-registed labels get mapped to the `other` label
    let metric = dual_labeled_counter.get("key3", "category1");
    metric.add_sync(&glean, 5);
    let metric = dual_labeled_counter.get("key1", "category3");
    metric.add_sync(&glean, 6);
    let metric = dual_labeled_counter.get("key3", "category3");
    metric.add_sync(&glean, 7);

    let snapshot = StorageManager
        .snapshot_as_json(glean.storage(), "store1", true)
        .unwrap();

    assert_eq!(
        json!({
            "dual_labeled_counter": {
                "telemetry.dual_labeled_counter": {
                    "key1": {
                        "category1": 1,
                        "category2": 2,
                        "__other__": 6
                    },
                    "key2": {
                        "category1": 3,
                        "category2": 4
                    },
                    "__other__": {
                        "category1": 5,
                        "__other__": 7
                    }
                }
            }
        }),
        snapshot
    );
}

#[test]
fn dynamic_labels_too_long() {
    let (glean, _t) = new_glean(None);
    let dual_labeled_counter = DualLabeledCounterMetric::new(
        CommonMetricData {
            name: "dual_labeled_counter".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        None,
        None,
    );

    let metric = dual_labeled_counter.get("1".repeat(112), "2".repeat(112));
    metric.add_sync(&glean, 1);

    let snapshot = StorageManager
        .snapshot_as_json(glean.storage(), "store1", true)
        .unwrap();

    // We end up with 2x InvalidLabel errors, one each for key and category which are both too long.
    assert_eq!(
        json!({
            "dual_labeled_counter": {
                "telemetry.dual_labeled_counter": {
                    "__other__": {
                        "__other__": 1
                    }
                }
            },
            "labeled_counter": {
                "glean.error.invalid_label": { "telemetry.dual_labeled_counter": 2 }
            }
        }),
        snapshot
    );
}

#[test]
fn dynamic_labels_regex_allowed() {
    let (glean, _t) = new_glean(None);
    let dual_labeled_counter = DualLabeledCounterMetric::new(
        CommonMetricData {
            name: "dual_labeled_counter".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        None,
        None,
    );

    let labels_validating = vec![
        ("key.is.fine", "cat.is.fine"),
        ("key_is_fine_too", "cat_is_fine_too"),
        ("key.is_still_fine", "cat.is_still_fine"),
        ("keyisfine", "catisfine"),
        ("_.key-is_fine", "_.cat-is_fine"),
        ("key.is-fine", "cat.is-fine"),
        ("key-is-fine", "cat-is-fine"),
    ];

    for (key, category) in &labels_validating {
        dual_labeled_counter.get(key, category).add_sync(&glean, 1);
    }

    let snapshot = StorageManager
        .snapshot_as_json(glean.storage(), "store1", true)
        .unwrap();

    assert_eq!(
        json!({
            "dual_labeled_counter": {
                "telemetry.dual_labeled_counter": {
                    "key.is.fine": {
                        "cat.is.fine": 1
                    },
                    "key_is_fine_too": {
                        "cat_is_fine_too": 1
                    },
                    "key.is_still_fine": {
                        "cat.is_still_fine": 1
                    },
                    "keyisfine": {
                        "catisfine": 1
                    },
                    "_.key-is_fine": {
                        "_.cat-is_fine": 1
                    },
                    "key.is-fine": {
                        "cat.is-fine": 1
                    },
                    "key-is-fine": {
                        "cat-is-fine": 1
                    }
                }
            }
        }),
        snapshot
    );
}

#[test]
fn seen_labels_get_reloaded_from_disk() {
    let (mut tempdir, _) = tempdir();

    let (glean, dir) = new_glean(Some(tempdir));
    tempdir = dir;

    let dual_labeled_counter = DualLabeledCounterMetric::new(
        CommonMetricData {
            name: "dual_labeled_counter".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        None,
        None,
    );

    // Store some data into labeled metrics
    {
        // Set the maximum number of labels
        for i in 1..=16 {
            let key = format!("key{i}");
            let category = format!("category{i}");
            dual_labeled_counter.get(key, category).add_sync(&glean, i);
        }

        let snapshot = StorageManager
            .snapshot_as_json(glean.storage(), "store1", false)
            .unwrap();

        // Check that the data is there
        for i in 1..=16 {
            let key = format!("key{i}");
            let category = format!("category{i}");
            assert_eq!(
                i,
                snapshot["dual_labeled_counter"]["telemetry.dual_labeled_counter"][&key][&category]
            );
        }

        drop(glean);
    }

    // Force a reload
    {
        let (glean, _t) = new_glean(Some(tempdir));

        // Try to store another label
        dual_labeled_counter
            .get("new_key", "new_category")
            .add_sync(&glean, 40);

        let snapshot = StorageManager
            .snapshot_as_json(glean.storage(), "store1", false)
            .unwrap();

        // Check that the old data is still there
        for i in 1..=16 {
            let key = format!("key{i}");
            let category = format!("category{i}");
            assert_eq!(
                i,
                snapshot["dual_labeled_counter"]["telemetry.dual_labeled_counter"][&key][&category]
            );
        }

        // The new label lands in the __other__ bucket, due to too many labels
        assert_eq!(
            40,
            snapshot["dual_labeled_counter"]["telemetry.dual_labeled_counter"]["__other__"]
                ["__other__"]
        );
    }
}

#[test]
fn caching_metrics_with_dynamic_labels() {
    let (glean, _t) = new_glean(None);
    let dual_labeled_counter = DualLabeledCounterMetric::new(
        CommonMetricData {
            name: "dual_labeled_counter".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        None,
        None,
    );

    // Create multiple metric instances and cache them for later use.
    let metrics = (1..=20)
        .map(|i| {
            let key = format!("key{i}");
            let category = format!("category{i}");
            dual_labeled_counter.get(key, category)
        })
        .collect::<Vec<_>>();

    // Only now use them.
    for metric in metrics {
        metric.add_sync(&glean, 1);
    }

    // The maximum number of labels we store is 16.
    // So we should have put 4 metrics in the __other__ bucket.
    let other = dual_labeled_counter.get("__other__", "__other__");
    assert_eq!(Some(4), other.get_value(&glean, Some("store1")));
}

#[test]
fn metrics_with_static_keys_and_dynamic_categories() {
    let (glean, _t) = new_glean(None);
    let dual_labeled_counter = DualLabeledCounterMetric::new(
        CommonMetricData {
            name: "dual_labeled_counter".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        Some(vec![
            std::borrow::Cow::Borrowed("key1"),
            std::borrow::Cow::Borrowed("key2"),
            std::borrow::Cow::Borrowed("key3"),
            std::borrow::Cow::Borrowed("key4"),
        ]),
        None,
    );

    // Create multiple metric instances and cache them for later use.
    // We also purposefully create a single key that should fail static
    // validation and be replaced with `__other__`
    let mut metrics: Vec<std::sync::Arc<CounterMetric>> = Vec::new();
    for i in 1..=5 {
        let key = format!("key{i}");
        for j in 1..=20 {
            let category = format!("category{j}");
            metrics.push(dual_labeled_counter.get(&key, &category));
        }
    }

    // Only now use them.
    for metric in metrics {
        metric.add_sync(&glean, 1);
    }

    // The maximum number of categories we store is 16.
    // So we should have put 4 metrics in the __other__ bucket for every key.
    for i in 1..=5 {
        let other = dual_labeled_counter.get(format!("key{i}"), "__other__".into());
        let recorded_value = other.get_value(&glean, Some("store1"));
        assert_eq!(
            Some(4),
            recorded_value,
            "Other value should be 4, found {recorded_value:?} for key{i}"
        );
    }

    // The above would have asked for "key5", but since that doesn't exist as a static key it should have fallen into
    // `__other__` as a key, the way `get()` works the above assert wouldn't fail but the actual key would have been
    // renamed. Let's check for that explicitly, just to verify.
    let other = dual_labeled_counter.get("__other__".to_string(), "__other__".to_string());
    let recorded_value = other.get_value(&glean, Some("store1"));
    assert_eq!(
        Some(4),
        recorded_value,
        "Other value should be 4, found {recorded_value:?}"
    );
}

#[test]
fn metrics_with_dynamic_keys_and_static_categories() {
    let (glean, _t) = new_glean(None);
    let dual_labeled_counter = DualLabeledCounterMetric::new(
        CommonMetricData {
            name: "dual_labeled_counter".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        None,
        Some(vec![
            std::borrow::Cow::Borrowed("category1"),
            std::borrow::Cow::Borrowed("category2"),
            std::borrow::Cow::Borrowed("category3"),
            std::borrow::Cow::Borrowed("category4"),
        ]),
    );

    // Create multiple metric instances and cache them for later use.
    // We also purposefully create a single key that should fail static
    // validation and be replaced with `__other__`
    let mut metrics: Vec<std::sync::Arc<CounterMetric>> = Vec::new();
    for i in 1..=20 {
        let key = format!("key{i}");
        for j in 1..=5 {
            let category = format!("category{j}");
            metrics.push(dual_labeled_counter.get(&key, &category));
        }
    }

    // Only now use them.
    for metric in metrics {
        metric.add_sync(&glean, 1);
    }

    for i in 1..=16 {
        for j in 1..=4 {
            let metric = dual_labeled_counter.get(format!("key{i}"), format!("category{j}"));
            let recorded_value = metric.get_value(&glean, Some("store1"));
            // The maximum number of keys we store is 16.
            // The first 16 keys are valid, but we also used an invalid static category ("category5"), so each of the
            // valid keys should have 1 count in the `_other_` category too, we will check that explicitly later in this test.
            assert_eq!(
                Some(1),
                recorded_value,
                "Value should be 1, found {recorded_value:?} for key{i}/category{j}"
            );
        }
        // Now we test for the invalid static category ("category5") that we used above went into the `__other__` category.
        let metric = dual_labeled_counter.get(format!("key{i}"), "__other__".to_string());
        let recorded_value = metric.get_value(&glean, Some("store1"));
        assert_eq!(
            Some(1),
            recorded_value,
            "Value should be 1, found {recorded_value:?} for key{i}/__other__"
        );
    }

    // Now lets check that key17 through key20 landed in the `__other__` key, and have counts in the proper categories. Each
    // category should have 4 counts since we had 4 keys that resulted in `__other__`
    for i in 1..=4 {
        let metric = dual_labeled_counter.get("__other__".into(), format!("category{i}"));
        let recorded_value = metric.get_value(&glean, Some("store1"));
        // The maximum number of keys we store is 16.
        // The first 16 keys are valid, but we also used an invalid static category ("category5"), so each of the
        // valid keys should have 1 count in the `_other_` category too, we will check that explicitly later in this test.
        assert_eq!(
            Some(4),
            recorded_value,
            "Value should be 4, found {recorded_value:?} for __other__/category{i}"
        );
    }

    // Finally, 4 keys each should have had one category that was invalid so we should have 4 counts in `__other__`/`__other__`
    let metric = dual_labeled_counter.get("__other__", "__other__");
    let recorded_value = metric.get_value(&glean, Some("store1"));
    assert_eq!(
        Some(4),
        recorded_value,
        "Value should be 4, found {recorded_value:?} for __other__/__other__"
    );
}

#[test]
fn caching_metrics_with_dynamic_labels_across_pings() {
    let (glean, _t) = new_glean(None);
    let dual_labeled_counter = DualLabeledCounterMetric::new(
        CommonMetricData {
            name: "dual_labeled_counter".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        None,
        None,
    );

    // Create multiple metric instances and cache them for later use.
    let metrics = (1..=20)
        .map(|i| {
            let key = format!("key{i}");
            let category = format!("category{i}");
            dual_labeled_counter.get(key, category)
        })
        .collect::<Vec<_>>();

    // Only now use them.
    for metric in &metrics {
        metric.add_sync(&glean, 1);
    }

    // The maximum number of keys/categories we store is 16.
    // So we should have put 4 metrics in the __other__ bucket.
    let other = dual_labeled_counter.get("__other__", "__other__");
    assert_eq!(Some(4), other.get_value(&glean, Some("store1")));

    // Snapshot (so we can inspect the JSON)
    // and clear out storage (the same way submitting a ping would)
    let snapshot = StorageManager
        .snapshot_as_json(glean.storage(), "store1", true)
        .unwrap();

    // We didn't send the 20th key/category
    assert_eq!(
        json!(null),
        snapshot["dual_labeled_counter"]["telemetry.dual_labeled_counter"]["key20"]["category20"]
    );

    // We now set the ones that ended up in `__other__` before.
    // Note: indexing is zero-based,
    // but we later check the names, so let's offset it by 1.
    metrics[16].add_sync(&glean, 17);
    metrics[17].add_sync(&glean, 18);
    metrics[18].add_sync(&glean, 19);
    metrics[19].add_sync(&glean, 20);

    assert_eq!(Some(17), metrics[16].get_value(&glean, Some("store1")));
    assert_eq!(Some(18), metrics[17].get_value(&glean, Some("store1")));
    assert_eq!(Some(19), metrics[18].get_value(&glean, Some("store1")));
    assert_eq!(Some(20), metrics[19].get_value(&glean, Some("store1")));
    assert_eq!(None, other.get_value(&glean, Some("store1")));

    let snapshot = StorageManager
        .snapshot_as_json(glean.storage(), "store1", true)
        .unwrap();

    let cached_labels = &snapshot["dual_labeled_counter"]["telemetry.dual_labeled_counter"];
    assert_eq!(json!(17), cached_labels["key17"]["category17"]);
    assert_eq!(json!(18), cached_labels["key18"]["category18"]);
    assert_eq!(json!(19), cached_labels["key19"]["category19"]);
    assert_eq!(json!(20), cached_labels["key20"]["category20"]);
    assert_eq!(json!(null), cached_labels["__other__"]["__other__"]);
}

#[test]
fn labels_containing_a_record_separator_record_an_error() {
    let (glean, _t) = new_glean(None);
    let dual_labeled_counter = DualLabeledCounterMetric::new(
        CommonMetricData {
            name: "dual_labeled_counter".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        None,
        None,
    );

    let metric = dual_labeled_counter.get("record\x1eseparator", "category1");
    metric.add_sync(&glean, 1);

    let snapshot = StorageManager
        .snapshot_as_json(glean.storage(), "store1", true)
        .unwrap();

    // We end up with 1x InvalidLabel error because the key contains an ASCII record separator, but both
    // labels end up as other because the use of the record separator prevents us from knowing which one
    // contained it (or both) at this point in exection.
    assert_eq!(
        json!({
            "dual_labeled_counter": {
                "telemetry.dual_labeled_counter": {
                    "__other__": {
                        "__other__": 1
                    }
                }
            },
            "labeled_counter": {
                "glean.error.invalid_label": { "telemetry.dual_labeled_counter": 1 }
            }
        }),
        snapshot
    );
}

#[test]
fn empty_dual_labels() {
    let (glean, _t) = new_glean(None);
    let dual_labeled_counter = DualLabeledCounterMetric::new(
        CommonMetricData {
            name: "empty_dual_labeled".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        None,
        None,
    );

    dual_labeled_counter.get("key1", "").add_sync(&glean, 1);
    dual_labeled_counter
        .get("", "category1")
        .add_sync(&glean, 1);
    dual_labeled_counter.get("", "").add_sync(&glean, 1);

    let snapshot = StorageManager
        .snapshot_as_json(glean.storage(), "store1", true)
        .unwrap();

    assert_eq!(
        json!({
            "dual_labeled_counter": {
                "telemetry.empty_dual_labeled": {
                    "key1": {
                        "": 1
                    },
                    "": {
                        "category1": 1,
                        "": 1
                    }
                }
            }
        }),
        snapshot
    );
}

#[cfg(not(feature = "sqlite"))]
#[test]
fn invalid_labels_in_the_database_dont_break_recording() {
    // Injecting an invalid key for the sake of testing
    let temp = {
        use rkv::Rkv;
        use rkv::StoreOptions;

        let (glean, temp) = new_glean(None);
        let path = temp.path().join("db");
        let rkv = Rkv::new::<rkv::backend::SafeMode>(&path).unwrap();
        let user_store = rkv.open_single("ping", StoreOptions::create()).unwrap();

        let mut writer = rkv.write().unwrap();

        // Broken dual labels: multiple record separators
        let key = "store1#telemetry.dual_labeled_counter/key1\x1Ecat1\x1Einvalid";
        let encoded = bincode::serialize(&Metric::Counter(3)).unwrap();
        let blob = rkv::Value::Blob(&encoded);
        user_store.put(&mut writer, key, &blob).unwrap();

        // Invalid dual label metric: only one label
        let key = "store1#telemetry.dual_labeled_counter/key1";
        let encoded = bincode::serialize(&Metric::Counter(4)).unwrap();
        let blob = rkv::Value::Blob(&encoded);
        user_store.put(&mut writer, key, &blob).unwrap();

        writer.commit().unwrap();

        drop(glean);
        temp
    };

    let (glean, _temp) = new_glean(Some(temp));

    let dual_labeled_counter = DualLabeledCounterMetric::new(
        CommonMetricData {
            name: "dual_labeled_counter".into(),
            category: "telemetry".into(),
            send_in_pings: vec!["store1".into()],
            disabled: false,
            lifetime: Lifetime::Ping,
            ..Default::default()
        },
        None,
        None,
    );

    let metric = dual_labeled_counter.get("key1", "category1");
    metric.add_sync(&glean, 1);

    let snapshot = StorageManager
        .snapshot_as_json(glean.storage(), "store1", true)
        .unwrap();

    assert_eq!(
        json!({
            "dual_labeled_counter": {
                "telemetry.dual_labeled_counter": {
                    "key1": {
                        "cat1\x1Einvalid": 3,
                        "category1": 1,
                    }
                }
            },
            "labeled_counter": {
                "glean.error.invalid_label": {
                    "telemetry.dual_labeled_counter": 1,
                },
                // NOTE: This metric ends up in the wrong category.
                // This can't happen under actual use
                // except for the edge case when a metric's type is changed.
                "telemetry.dual_labeled_counter": {
                    "key1": 4
                },
            }
        }),
        snapshot
    );
}
