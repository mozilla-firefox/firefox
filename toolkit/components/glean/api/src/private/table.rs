// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::borrow::Cow;
use std::marker::PhantomData;
use std::ops::Deref;
use std::sync::OnceLock;

use glean::traits::ObjectSerialize;
use glean::HistogramType;

use super::{
    BaseMetricId, BooleanMetric, CommonMetricData, CounterMetric, CustomDistributionMetric,
    DatetimeMetric, DenominatorMetric, DualLabeledCounterMetric, EventMetric, ExtraKeys,
    LabeledBooleanMetric, LabeledCounterMetric, LabeledCustomDistributionMetric,
    LabeledMemoryDistributionMetric, LabeledMetric, LabeledMetricData, LabeledQuantityMetric,
    LabeledStringMetric, LabeledTimingDistributionMetric, Lifetime, MemoryDistributionMetric,
    MemoryUnit, NoExtraKeys, NumeratorMetric, ObjectMetric, QuantityMetric, RateMetric,
    StringListMetric, StringMetric, TextMetric, TimeUnit, TimespanMetric,
    TimingDistributionMetric, UrlMetric, UuidMetric,
};
use crate::metrics::DynamicLabel;

/// Everything a metric constructor needs, for every metric type.
/// Fields a type doesn't use keep the values from [`MetricDef::DEFAULT`].
pub struct MetricDef {
    pub id: u32,
    pub category: &'static str,
    pub name: &'static str,
    pub send_in_pings: &'static [&'static str],
    pub lifetime: Lifetime,
    pub disabled: bool,
    pub in_session: bool,
    pub unordered_ipc: bool,
    pub time_unit: TimeUnit,
    pub memory_unit: MemoryUnit,
    pub range_min: i64,
    pub range_max: i64,
    pub bucket_count: i64,
    pub histogram_type: HistogramType,
    pub allowed_extra_keys: &'static [&'static str],
    /// Static labels of a labeled metric; empty means dynamic labels.
    pub labels: &'static [&'static str],
    pub keys: &'static [&'static str],
    pub categories: &'static [&'static str],
    pub numerators: &'static [MetricDef],
}

impl MetricDef {
    pub const DEFAULT: MetricDef = MetricDef {
        id: 0,
        category: "",
        name: "",
        send_in_pings: &[],
        lifetime: Lifetime::Ping,
        disabled: false,
        in_session: false,
        unordered_ipc: false,
        time_unit: TimeUnit::Millisecond,
        memory_unit: MemoryUnit::Byte,
        range_min: 0,
        range_max: 0,
        bucket_count: 0,
        histogram_type: HistogramType::Linear,
        allowed_extra_keys: &[],
        labels: &[],
        keys: &[],
        categories: &[],
        numerators: &[],
    };

    fn base_id(&self) -> BaseMetricId {
        BaseMetricId(self.id)
    }

    fn cmd(&self) -> CommonMetricData {
        CommonMetricData {
            name: self.name.into(),
            category: self.category.into(),
            send_in_pings: self.send_in_pings.iter().map(|p| (*p).into()).collect(),
            lifetime: self.lifetime,
            disabled: self.disabled,
            in_session: self.in_session,
            ..Default::default()
        }
    }

    fn cow_list(list: &'static [&'static str]) -> Option<Vec<Cow<'static, str>>> {
        if list.is_empty() {
            None
        } else {
            Some(list.iter().map(|s| Cow::Borrowed(*s)).collect())
        }
    }
}

/// Builds a metric of the implementing type from its definition.
pub trait FromDef: Sized {
    fn from_def(def: &MetricDef) -> Self;
}

/// A table entry: the definition plus the metric, constructed on first
/// dereference.
pub struct TableMetric<T> {
    def: MetricDef,
    cell: OnceLock<T>,
}

impl<T> TableMetric<T> {
    pub const fn new(def: MetricDef) -> Self {
        TableMetric {
            def,
            cell: OnceLock::new(),
        }
    }

    pub fn id(&self) -> BaseMetricId {
        self.def.base_id()
    }

    pub fn def(&self) -> &MetricDef {
        &self.def
    }

    /// The metric, if it has already been constructed.
    pub fn peek(&self) -> Option<&T> {
        self.cell.get()
    }
}

impl<T: FromDef> Deref for TableMetric<T> {
    type Target = T;

    fn deref(&self) -> &T {
        self.cell.get_or_init(|| T::from_def(&self.def))
    }
}

/// Dereference a table entry, keeping the `'static` lifetime.
pub fn force<T: FromDef>(cell: &'static TableMetric<T>) -> &'static T {
    cell
}

/// Finds a metric of this type by id in its generated lookup map.
/// Implemented by codegen for every metric type.
pub trait Lookup: Sized + 'static {
    fn lookup(id: u32) -> &'static Self;
}

/// The named API for a metric: just its id, resolved once on first use.
pub struct MetricRef<T: 'static> {
    id: u32,
    cell: OnceLock<&'static T>,
}

impl<T> MetricRef<T> {
    pub const fn new(id: u32) -> Self {
        MetricRef {
            id,
            cell: OnceLock::new(),
        }
    }

    pub fn id(&self) -> BaseMetricId {
        BaseMetricId(self.id)
    }
}

impl<T: Lookup> Deref for MetricRef<T> {
    type Target = T;

    fn deref(&self) -> &T {
        self.cell.get_or_init(|| T::lookup(self.id))
    }
}

/// The named API for an event metric with typed extra keys `K`. The
/// underlying metric validates keys at runtime, `K` only types the callers.
pub struct EventRef<K> {
    inner: MetricRef<EventMetric<NoExtraKeys>>,
    _keys: PhantomData<fn() -> K>,
}

impl<K> EventRef<K> {
    pub const fn new(id: u32) -> Self {
        EventRef {
            inner: MetricRef::new(id),
            _keys: PhantomData,
        }
    }
}

impl<K> Deref for EventRef<K> {
    type Target = EventMetric<NoExtraKeys>;

    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

impl<K: ExtraKeys> EventRef<K> {
    pub fn record<M: Into<Option<K>>>(&self, extra: M) {
        let extra = extra
            .into()
            .map(|extra| extra.into_ffi_extra())
            .unwrap_or_default();
        self.inner.record_raw(extra);
    }
}

/// Object metrics stay typed (`ObjectMetric<K>` with a generated `K`) so
/// that values set through the FFI are validated against the structure.
/// This erases `K` for the id lookup map.
pub trait AnyObjectMetric: Sync {
    fn id(&self) -> BaseMetricId;
    fn set_string(&self, value: String);
    fn test_get_value_as_str(&self, ping_name: Option<String>) -> Option<String>;
    fn test_get_num_recorded_errors(&self, error: glean::ErrorType) -> i32;
}

impl<K> AnyObjectMetric for TableMetric<ObjectMetric<K>>
where
    K: ObjectSerialize + Clone + Send + Sync + 'static,
{
    fn id(&self) -> BaseMetricId {
        TableMetric::id(self)
    }

    fn set_string(&self, value: String) {
        (**self).set_string(value)
    }

    fn test_get_value_as_str(&self, ping_name: Option<String>) -> Option<String> {
        (**self).test_get_value_as_str(ping_name)
    }

    fn test_get_num_recorded_errors(&self, error: glean::ErrorType) -> i32 {
        (**self).test_get_num_recorded_errors(error)
    }
}

macro_rules! from_cmd {
    ($($t:ty),+ $(,)?) => {$(
        impl FromDef for $t {
            fn from_def(def: &MetricDef) -> Self {
                <$t>::new(def.base_id(), def.cmd())
            }
        }
    )+};
}

from_cmd!(
    NumeratorMetric,
    RateMetric,
    StringMetric,
    StringListMetric,
    TextMetric,
    UrlMetric,
    UuidMetric,
);

impl<K: ObjectSerialize + Clone> FromDef for ObjectMetric<K> {
    fn from_def(def: &MetricDef) -> Self {
        let metric = ObjectMetric::new(def.base_id(), def.cmd());
        #[cfg(feature = "with_gecko")]
        crate::metrics::count_metric_memory(&metric);
        metric
    }
}

macro_rules! from_cmd_maybe_unordered {
    ($($t:ty),+ $(,)?) => {$(
        impl FromDef for $t {
            fn from_def(def: &MetricDef) -> Self {
                if def.unordered_ipc {
                    <$t>::with_unordered_ipc(def.base_id(), def.cmd())
                } else {
                    <$t>::new(def.base_id(), def.cmd())
                }
            }
        }
    )+};
}

from_cmd_maybe_unordered!(BooleanMetric, QuantityMetric);

macro_rules! from_cmd_and_unit {
    ($($t:ty => $unit:ident),+ $(,)?) => {$(
        impl FromDef for $t {
            fn from_def(def: &MetricDef) -> Self {
                <$t>::new(def.base_id(), def.cmd(), def.$unit)
            }
        }
    )+};
}

from_cmd_and_unit!(
    DatetimeMetric => time_unit,
    TimespanMetric => time_unit,
    TimingDistributionMetric => time_unit,
    MemoryDistributionMetric => memory_unit,
);

impl FromDef for CounterMetric {
    fn from_def(def: &MetricDef) -> Self {
        // Same fast path codegen used for the (very common) ping-lifetime,
        // single-ping counters such as use counters.
        if def.lifetime == Lifetime::Ping && def.send_in_pings.len() == 1 {
            let ping = def.send_in_pings[0];
            if def.disabled {
                CounterMetric::codegen_disabled_new(def.id, def.category, def.name, ping)
            } else {
                CounterMetric::codegen_new(def.id, def.category, def.name, ping)
            }
        } else {
            CounterMetric::new(def.base_id(), def.cmd())
        }
    }
}

impl FromDef for CustomDistributionMetric {
    fn from_def(def: &MetricDef) -> Self {
        CustomDistributionMetric::new(
            def.base_id(),
            def.cmd(),
            def.range_min,
            def.range_max,
            def.bucket_count,
            def.histogram_type,
        )
    }
}

impl FromDef for DenominatorMetric {
    fn from_def(def: &MetricDef) -> Self {
        DenominatorMetric::new(
            def.base_id(),
            def.cmd(),
            def.numerators.iter().map(|n| n.cmd()).collect(),
        )
    }
}

impl FromDef for DualLabeledCounterMetric {
    fn from_def(def: &MetricDef) -> Self {
        DualLabeledCounterMetric::new(
            def.base_id(),
            def.cmd(),
            MetricDef::cow_list(def.keys),
            MetricDef::cow_list(def.categories),
        )
    }
}

impl FromDef for EventMetric<NoExtraKeys> {
    fn from_def(def: &MetricDef) -> Self {
        let metric = EventMetric::with_runtime_extra_keys(
            def.base_id(),
            def.cmd(),
            def.allowed_extra_keys
                .iter()
                .map(|k| (*k).to_string())
                .collect(),
        );
        #[cfg(feature = "with_gecko")]
        crate::metrics::count_metric_memory(&metric);
        metric
    }
}

macro_rules! labeled_from_def {
    ($($t:ty => |$def:ident| $meta:expr),+ $(,)?) => {$(
        impl FromDef for LabeledMetric<$t, DynamicLabel> {
            fn from_def($def: &MetricDef) -> Self {
                let meta = $meta;
                let labels = MetricDef::cow_list($def.labels);
                if $def.unordered_ipc {
                    LabeledMetric::with_unordered_ipc($def.base_id(), meta, labels)
                } else {
                    LabeledMetric::new($def.base_id(), meta, labels)
                }
            }
        }
    )+};
}

labeled_from_def!(
    LabeledBooleanMetric => |def| LabeledMetricData::Common { cmd: def.cmd() },
    LabeledCounterMetric => |def| LabeledMetricData::Common { cmd: def.cmd() },
    LabeledQuantityMetric => |def| LabeledMetricData::Common { cmd: def.cmd() },
    LabeledStringMetric => |def| LabeledMetricData::Common { cmd: def.cmd() },
    LabeledCustomDistributionMetric => |def| LabeledMetricData::CustomDistribution {
        cmd: def.cmd(),
        range_min: def.range_min,
        range_max: def.range_max,
        bucket_count: def.bucket_count,
        histogram_type: def.histogram_type,
    },
    LabeledMemoryDistributionMetric => |def| LabeledMetricData::MemoryDistribution {
        cmd: def.cmd(),
        unit: def.memory_unit,
    },
    LabeledTimingDistributionMetric => |def| LabeledMetricData::TimingDistribution {
        cmd: def.cmd(),
        unit: def.time_unit,
    },
);
