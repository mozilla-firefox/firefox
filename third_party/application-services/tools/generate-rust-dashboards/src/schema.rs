/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Grafana JSON schemas
//!
//! This is an incomplete representation of Grafana's JSON model.
//! It was created by looking at the "JSON Model" settings tab and finding the settings there.
//! Feel free to add new fields/structs if you need additional functionality.

use std::cmp::max;

use anyhow::anyhow;
use serde::{Serialize, Serializer};

use crate::{config::TeamConfig, Result};

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Dashboard {
    pub editable: bool,
    pub panels: Vec<Panel>,
    pub refresh: String,
    pub schema_version: u32,
    pub style: String,
    pub templating: Templating,
    pub time: Timespan,
    pub timezone: String,
    pub title: String,
    pub uid: String,
}

#[derive(Serialize)]
#[serde(tag = "type")]
#[serde(rename_all = "lowercase")]
pub enum Panel {
    Row(PanelRow),
    Text(TextPanel),
    Logs(LogPanel),
    TimeSeries(TimeSeriesPanel),
    PieChart(PieChartPanel),
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PanelRow {
    pub title: String,
    pub collapsed: bool,
    pub grid_pos: GridPos,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TextPanel {
    pub content: String,
    pub mode: String,
    pub grid_pos: GridPos,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LogPanel {
    pub title: String,
    pub options: LogOptions,
    pub datasource: Datasource,
    pub grid_pos: GridPos,
    pub targets: Vec<Target>,
    pub transformations: Vec<Transformation>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LogOptions {
    pub dedup_strategy: String,
    pub details_mode: String,
    pub enable_infinite_scrolling: bool,
    pub enable_log_details: bool,
    pub prettify_log_message: bool,
    pub show_common_labels: bool,
    pub show_labels: bool,
    pub show_time: bool,
    pub sort_order: SortOrder,
    pub wrap_log_message: bool,
}

pub enum SortOrder {
    Descending,
    Ascending,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TimeSeriesPanel {
    pub title: String,
    pub datasource: Datasource,
    pub field_config: FieldConfig,
    pub grid_pos: GridPos,
    pub interval: String,
    pub options: TimeseriesOptions,
    pub targets: Vec<Target>,
    pub transformations: Vec<Transformation>,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FieldConfig {
    pub defaults: FieldConfigDefaults,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FieldConfigDefaults {
    // Grafana defines lots more, but this is all we need so far
    pub links: Vec<DataLink>,
    pub custom: FieldConfigCustom,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub unit: Option<Unit>,
}

#[derive(Clone, Copy, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Unit {
    #[serde(rename = "s")]
    Seconds,
    #[serde(rename = "ms")]
    Milliseconds,
    #[serde(rename = "µs")]
    Microseconds,
    #[serde(rename = "ns")]
    Nanoseconds,
    #[serde(rename = "sishort")]
    SiShort,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FieldConfigCustom {
    // Grafana defines lots more, but this is all we need so far
    pub axis_border_show: bool,
    pub axis_centered_zero: bool,
    pub axis_color_mode: String,
    pub axis_label: String,
    pub axis_soft_min: u32,
    pub axis_soft_max: u32,
    pub scale_distribution: ScaleDistribution,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ScaleDistribution {
    #[serde(rename = "type")]
    pub type_: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub log: Option<u32>,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DataLink {
    pub title: String,
    pub url: String,
    pub target_blank: bool,
    pub one_click: bool,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TimeseriesOptions {
    pub legend: Legend,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Legend {
    pub calcs: Vec<String>,
    pub display_mode: String,
    pub placement: String,
    pub show_legend: bool,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Datasource {
    #[serde(rename = "type")]
    pub type_: Option<String>,
    pub uid: Option<String>,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PieChartPanel {
    pub title: String,
    pub datasource: Datasource,
    pub grid_pos: GridPos,
    pub interval: String,
    pub options: PieChartOptions,
    pub targets: Vec<Target>,
    pub transformations: Vec<Transformation>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PieChartOptions {
    pub display_labels: Vec<String>,
    pub legend: Legend,
    pub pie_type: String,
    pub reduce_options: PieChartReduceOptions,
    pub sort: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PieChartReduceOptions {
    pub calcs: Vec<String>,
    pub fields: String,
    pub values: bool,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Target {
    pub datasource: Datasource,
    pub format: TargetFormat,
    pub raw_query: bool,
    pub raw_sql: String,
}

#[derive(Default)]
pub enum TargetFormat {
    #[default]
    Timeseries,
    Table,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
#[serde(tag = "id", content = "options")]
pub enum Transformation {
    #[serde(rename_all = "camelCase")]
    PartitionByValues {
        fields: Vec<String>,
        keep_fields: bool,
    },
    #[serde(rename_all = "camelCase")]
    RenameByRegex {
        regex: String,
        rename_pattern: String,
    },
    #[serde(rename_all = "camelCase")]
    CalculateField(CalculateFieldOptions),
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
#[serde(tag = "mode")]
pub enum CalculateFieldOptions {
    #[serde(rename_all = "camelCase")]
    WindowFunctions {
        window: WindowFunctionWindow,
        replace_fields: bool,
    },
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WindowFunctionWindow {
    pub field: String,
    pub reducer: String,
    pub window_alignment: String,
    pub window_size_mode: String,
    pub window_size: f32,
}

#[derive(Default, Serialize, Clone, Copy)]
#[serde(rename_all = "camelCase")]
pub struct GridPos {
    pub x: u32,
    pub y: u32,
    pub w: u32,
    pub h: u32,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Timespan {
    pub from: String,
    pub to: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Templating {
    pub enable: bool,
    pub list: Vec<Variable>,
}

#[derive(Serialize)]
#[serde(tag = "type")]
#[serde(rename_all = "lowercase")]
pub enum Variable {
    Custom(CustomVariable),
    Query(QueryVariable),
    TextBox(TextBoxVariable),
    AdHoc(AdHocFiltersVariable),
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CustomVariable {
    pub include_all: bool,
    pub name: String,
    pub label: String,
    pub multi: bool,
    pub query: String,
    pub allow_custom_value: bool,
    pub current: CustomVariableSelection,
    pub hide: VariableHide,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct QueryVariable {
    pub datasource: Datasource,
    pub name: String,
    pub label: String,
    pub multi: bool,
    pub allow_custom_value: bool,
    pub query: QueryVariableQuery,
    pub sort: Option<VariableSortOrder>,
    pub hide: VariableHide,
    pub include_all: bool,
    pub all_value: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct QueryVariableQuery {
    pub editor_mode: String,
    pub format: TargetFormat,
    pub raw_query: bool,
    pub raw_sql: String,
    pub regex: String,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TextBoxVariable {
    pub name: String,
    pub label: String,
    pub current: TextBoxSelection,
    pub hide: VariableHide,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TextBoxSelection {
    pub text: String,
    pub value: String,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AdHocFiltersVariable {
    pub base_filters: Vec<String>,
    pub datasource: Datasource,
    pub name: String,
    pub filters: Vec<AdHocFilter>,
}

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AdHocFilter {
    pub key: String,
    pub operation: String,
    pub value: String,
}

#[derive(Default)]
pub enum VariableHide {
    #[default]
    Nothing,
    Label,
    Variable,
}

#[derive(Serialize)]
#[serde(untagged)]
pub enum CustomVariableSelection {
    Single(String),
    Multiple { value: Vec<String> },
}

pub enum VariableSortOrder {
    AlphabeticalAscending,
    AlphabeticalDescending,
    NumericalAscending,
    NumericalDescending,
    AlphabeticalCaseInsensitiveAscending,
    AlphabeticalCaseInsensitiveDescending,
    NaturalAscending,
    NaturalDescending,
}

impl Default for Dashboard {
    fn default() -> Self {
        Self {
            editable: false,
            panels: vec![],
            refresh: "1h".into(),
            schema_version: 36,
            style: "dark".into(),
            templating: Templating::default(),
            time: Timespan::default(),
            timezone: "browser".into(),
            title: "".into(),
            uid: "".into(),
        }
    }
}

impl Default for PieChartOptions {
    fn default() -> Self {
        Self {
            display_labels: vec!["percent".into()],
            legend: Legend::default(),
            pie_type: "pie".into(),
            reduce_options: PieChartReduceOptions::default(),
            sort: "desc".into(),
        }
    }
}

impl Default for PieChartReduceOptions {
    fn default() -> Self {
        Self {
            calcs: vec![],
            fields: "".into(),
            values: true,
        }
    }
}

impl Default for Legend {
    fn default() -> Self {
        Self {
            calcs: vec![],
            display_mode: "list".into(),
            placement: "bottom".into(),
            show_legend: true,
        }
    }
}

impl Default for Timespan {
    fn default() -> Self {
        Timespan {
            from: "now-2w".into(),
            to: "now".into(),
        }
    }
}

impl Default for Templating {
    fn default() -> Self {
        Self {
            enable: true,
            list: vec![],
        }
    }
}

impl Default for LogOptions {
    fn default() -> Self {
        Self {
            dedup_strategy: "none".into(),
            details_mode: "inline".into(),
            enable_infinite_scrolling: false,
            enable_log_details: true,
            prettify_log_message: false,
            show_common_labels: false,
            show_labels: false,
            show_time: true,
            sort_order: SortOrder::Descending,
            wrap_log_message: true,
        }
    }
}

impl Default for ScaleDistribution {
    fn default() -> Self {
        Self {
            type_: "linear".into(),
            log: None,
        }
    }
}

impl Default for QueryVariableQuery {
    fn default() -> Self {
        Self {
            editor_mode: "code".into(),
            format: TargetFormat::Table,
            raw_query: true,
            raw_sql: String::default(),
            regex: String::default(),
        }
    }
}

impl Default for CustomVariableSelection {
    fn default() -> Self {
        Self::single("")
    }
}

impl Panel {
    fn grid_pos_mut(&mut self) -> &mut GridPos {
        match self {
            Self::Row(p) => &mut p.grid_pos,
            Self::Text(p) => &mut p.grid_pos,
            Self::Logs(p) => &mut p.grid_pos,
            Self::TimeSeries(p) => &mut p.grid_pos,
            Self::PieChart(p) => &mut p.grid_pos,
        }
    }
}

impl From<PanelRow> for Panel {
    fn from(p: PanelRow) -> Self {
        Self::Row(p)
    }
}

impl From<TextPanel> for Panel {
    fn from(p: TextPanel) -> Self {
        Self::Text(p)
    }
}

impl From<LogPanel> for Panel {
    fn from(p: LogPanel) -> Self {
        Self::Logs(p)
    }
}

impl From<TimeSeriesPanel> for Panel {
    fn from(p: TimeSeriesPanel) -> Self {
        Self::TimeSeries(p)
    }
}

impl From<PieChartPanel> for Panel {
    fn from(p: PieChartPanel) -> Self {
        Self::PieChart(p)
    }
}

impl From<TextBoxVariable> for Variable {
    fn from(v: TextBoxVariable) -> Self {
        Self::TextBox(v)
    }
}

impl From<AdHocFiltersVariable> for Variable {
    fn from(v: AdHocFiltersVariable) -> Self {
        Self::AdHoc(v)
    }
}

impl From<CustomVariable> for Variable {
    fn from(v: CustomVariable) -> Self {
        Self::Custom(v)
    }
}

impl From<QueryVariable> for Variable {
    fn from(v: QueryVariable) -> Self {
        Self::Query(v)
    }
}

impl Serialize for TargetFormat {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match self {
            Self::Timeseries => serializer.serialize_i32(0),
            Self::Table => serializer.serialize_i32(1),
        }
    }
}

impl Serialize for VariableHide {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match self {
            Self::Nothing => serializer.serialize_i32(0),
            Self::Label => serializer.serialize_i32(1),
            Self::Variable => serializer.serialize_i32(2),
        }
    }
}

impl Serialize for SortOrder {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match self {
            Self::Descending => serializer.serialize_str("Descending"),
            Self::Ascending => serializer.serialize_str("Ascending"),
        }
    }
}

impl Serialize for VariableSortOrder {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match self {
            Self::AlphabeticalAscending => serializer.serialize_u32(1),
            Self::AlphabeticalDescending => serializer.serialize_u32(2),
            Self::NumericalAscending => serializer.serialize_u32(3),
            Self::NumericalDescending => serializer.serialize_u32(4),
            Self::AlphabeticalCaseInsensitiveAscending => serializer.serialize_u32(5),
            Self::AlphabeticalCaseInsensitiveDescending => serializer.serialize_u32(6),
            Self::NaturalAscending => serializer.serialize_u32(7),
            Self::NaturalDescending => serializer.serialize_u32(8),
        }
    }
}

impl GridPos {
    /// Create a GridPos from a height only
    ///
    /// Use this with `DashboardPacker`, which will automatically set all other fields
    pub fn height(h: u32) -> Self {
        Self {
            h,
            ..Self::default()
        }
    }
}

impl CustomVariableSelection {
    pub fn single(selected: impl std::fmt::Display) -> Self {
        Self::Single(selected.to_string())
    }

    pub fn multi(selected: impl IntoIterator<Item = impl std::fmt::Display>) -> Self {
        Self::Multiple {
            value: selected.into_iter().map(|s| s.to_string()).collect(),
        }
    }
}

impl QueryVariableQuery {
    pub fn from_sql(raw_sql: impl Into<String>) -> Self {
        Self {
            raw_query: true,
            raw_sql: raw_sql.into(),
            ..QueryVariableQuery::default()
        }
    }
}

impl Datasource {
    pub fn bigquery() -> Self {
        Self {
            type_: Some("grafana-bigquery-datasource".into()),
            uid: None,
        }
    }
}

impl Target {
    pub fn timeseries(sql: impl Into<String>) -> Self {
        Self {
            datasource: Datasource::bigquery(),
            format: TargetFormat::Timeseries,
            raw_query: true,
            raw_sql: sql.into(),
        }
    }

    pub fn table(sql: impl Into<String>) -> Self {
        Self {
            datasource: Datasource::bigquery(),
            format: TargetFormat::Table,
            raw_query: true,
            raw_sql: sql.into(),
        }
    }
}

pub struct DashboardBuilder {
    pub dashboard: Dashboard,
    col: u32,
    row: u32,
    current_row_height: u32,
}

impl DashboardBuilder {
    pub fn new(title: impl Into<String>, uid: impl Into<String>) -> Self {
        Self {
            dashboard: Dashboard {
                title: title.into(),
                uid: uid.into(),
                ..Dashboard::default()
            },
            col: 0,
            row: 0,
            current_row_height: 0,
        }
    }

    pub fn add_variable(&mut self, v: impl Into<Variable>) {
        self.dashboard.templating.list.push(v.into());
    }

    /// Add an `application` variable that the user can select
    pub fn add_application_variable(&mut self, config: &TeamConfig) -> Result<()> {
        let applications = config.applications();

        let first_application = applications
            .iter()
            .next()
            .ok_or_else(|| anyhow!("Application list empty for {}", config.team_name))?;

        self.add_variable(CustomVariable {
            label: "Application".into(),
            name: "application".into(),
            query: applications
                .iter()
                .map(|a| format!("{a} : {}", a.slug()))
                .collect::<Vec<_>>()
                .join(","),
            current: CustomVariableSelection::single(first_application.slug()),
            ..CustomVariable::default()
        });
        Ok(())
    }

    /// Add an `channel` variable that the user can select a release channel from
    pub fn add_channel_variable(&mut self) {
        self.add_variable(CustomVariable {
            label: "Channel".into(),
            name: "channel".into(),
            multi: false,
            query: "nightly,beta,release".into(),
            ..CustomVariable::default()
        });
    }

    pub fn add_panel_title(&mut self, title: impl Into<String>) {
        self.add_panel_full(PanelRow {
            title: title.into(),
            collapsed: false,
            grid_pos: GridPos::height(1),
        })
    }

    pub fn add_panel_third(&mut self, p: impl Into<Panel>) {
        if self.col > 16 {
            self.start_new_row();
        }
        let mut p = p.into();
        let pos = p.grid_pos_mut();
        pos.x = self.col;
        pos.y = self.row;
        pos.w = 8;
        self.current_row_height = max(self.current_row_height, pos.h);
        self.col += 8;

        self.dashboard.panels.push(p);
    }

    pub fn add_panel_full(&mut self, p: impl Into<Panel>) {
        self.start_new_row();
        let mut p = p.into();
        let pos = p.grid_pos_mut();
        pos.x = self.col;
        pos.y = self.row;
        pos.w = 24;
        self.row += pos.h;

        self.dashboard.panels.push(p);
    }

    pub fn start_new_row(&mut self) {
        self.row += self.current_row_height;
        self.current_row_height = 0;
        self.col = 0;
    }
}
