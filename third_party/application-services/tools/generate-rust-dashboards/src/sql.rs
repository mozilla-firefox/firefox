/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// Very simple SQL query builder
///
/// Use this if it helps or use raw SQL if it's easier.
#[derive(Debug, Default)]
pub struct Query {
    pub select: Vec<String>,
    pub from: String,
    pub joins: Vec<String>,
    pub where_: Vec<String>,
    pub group_by: Option<String>,
    pub order_by: Option<String>,
    pub limit: Option<u32>,
    pub prep_statements: Vec<String>,
}

impl Query {
    pub fn sql(&self) -> String {
        let mut sql = String::default();
        for stmt in self.prep_statements.iter() {
            sql.push_str(&format!("{stmt};\n"));
        }

        sql.push_str(&format!("SELECT {}\n", self.select.join(", ")));
        sql.push_str(&format!("FROM {}\n", self.from));
        for join in self.joins.iter() {
            sql.push_str(&format!("{join}\n"));
        }
        if !self.where_.is_empty() {
            sql.push_str(&format!(
                "WHERE {}\n",
                self.where_
                    .iter()
                    .map(|w| format!("({w})"))
                    .collect::<Vec<_>>()
                    .join(" AND ")
            ));
        }
        if let Some(group_by) = &self.group_by {
            sql.push_str(&format!("GROUP BY {group_by}\n"));
        }
        if let Some(order_by) = &self.order_by {
            sql.push_str(&format!("ORDER BY {order_by}\n"));
        }
        if let Some(limit) = &self.limit {
            sql.push_str(&format!("LIMIT {limit}\n"));
        }
        sql
    }

    pub fn as_subquery(&self) -> String {
        format!("(\n{})", self.sql())
    }

    pub fn add_count_per_day_column(
        &mut self,
        count_expr: impl Into<String>,
        name: impl Into<String>,
    ) {
        let count_expr = count_expr.into();
        let name = name.into();
        let ms_per_day = 86400000;
        self.select.push(format!(
            "(({count_expr}) / ($__interval_ms / {ms_per_day})) as {name}"
        ));
    }

    pub fn add_standard_glean_columns(&mut self) {
        self.select.extend([
            "client_info.app_display_version as app_display_version".into(),
            "client_info.architecture as architecture".into(),
            "client_info.device_manufacturer as device_manufacturer".into(),
            "client_info.device_model as device_model".into(),
            "client_info.locale as locale".into(),
            "client_info.os as os".into(),
            "client_info.os_version as os_version".into(),
            "submission_timestamp".into(),
        ])
    }

    pub fn add_from_using_application_var(&mut self, table_name: &str) {
        // TODO: Add UNIONs once we are enable the glean pipeline for iOS and/or Desktop
        //     let from = format!("
        // (
        //     SELECT * FROM mozdata.fenix.{table_name} WHERE '${{application}}' = 'firefox_android'
        //     UNION ALL SELECT * FROM mozdata.firefox_ios.{table_name} WHERE '${{application}}' = 'firefox_ios'
        //     UNION ALL SELECT * FROM mozdata.firefox_desktop.{table_name} WHERE '${{application}}' = 'firefox_desktop'
        // )");
        self.from = format!(
            "(SELECT * FROM mozdata.fenix.{table_name} WHERE '${{application}}' = 'firefox_android')"
        );
    }
}

/// Union query
///
/// Like `Query`, use this if it helps or use raw SQL if it's easier.
#[derive(Debug, Default)]
pub struct Union {
    pub queries: Vec<Query>,
    pub order_by: Option<String>,
}

impl Union {
    pub fn sql(&self) -> String {
        let mut sql = String::default();

        for (i, q) in self.queries.iter().enumerate() {
            if i != 0 {
                sql.push_str("UNION ALL\n");
            }
            sql.push_str(&format!("{}\n", q.sql()));
        }
        sql
    }
}
