/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::storage::{RemoteTab, TabGroup, Window};
use serde_derive::{Deserialize, Serialize};
use std::collections::HashMap;
use types::Timestamp;

// copy/pasta...
fn skip_if_default<T: PartialEq + Default>(v: &T) -> bool {
    *v == T::default()
}

#[derive(Debug, Clone, Hash, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct TabsRecordTab {
    pub title: String,
    pub url_history: Vec<String>,
    pub icon: Option<String>,
    pub last_used: i64, // Seconds since epoch!
    #[serde(default, skip_serializing_if = "skip_if_default")]
    pub inactive: bool,
    #[serde(default, skip_serializing_if = "skip_if_default")]
    pub pinned: bool,
    #[serde(default, skip_serializing_if = "skip_if_default")]
    pub index: u32, // the position
    #[serde(default, skip_serializing_if = "skip_if_default")]
    pub tab_group_id: String,
    #[serde(default, skip_serializing_if = "skip_if_default")]
    pub window_id: String,
}

impl From<RemoteTab> for TabsRecordTab {
    fn from(tab: RemoteTab) -> Self {
        Self {
            title: tab.title,
            url_history: tab.url_history,
            icon: tab.icon,
            last_used: tab.last_used.checked_div(1000).unwrap_or_default(),
            inactive: tab.inactive,
            pinned: tab.pinned,
            index: tab.index,
            tab_group_id: tab.tab_group_id,
            window_id: tab.window_id,
        }
    }
}

impl From<TabsRecordTab> for RemoteTab {
    fn from(tab: TabsRecordTab) -> Self {
        Self {
            title: tab.title,
            url_history: tab.url_history,
            icon: tab.icon,
            last_used: tab.last_used.checked_mul(1000).unwrap_or_default(),
            inactive: tab.inactive,
            pinned: tab.pinned,
            index: tab.index,
            tab_group_id: tab.tab_group_id,
            window_id: tab.window_id,
        }
    }
}

#[derive(Debug, Clone, Hash, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct TabsRecordWindow {
    pub id: String,
    pub last_used: Timestamp,
    pub index: u32,
    #[serde(default, skip_serializing_if = "skip_if_default")]
    pub window_type: u8, // The repr of a `crate::storage::WindowType`
}

impl From<Window> for TabsRecordWindow {
    fn from(w: Window) -> Self {
        Self {
            id: w.id,
            last_used: w.last_used,
            index: w.index,
            window_type: w.window_type as u8,
        }
    }
}

impl From<TabsRecordWindow> for Window {
    fn from(w: TabsRecordWindow) -> Self {
        Self {
            id: w.id,
            last_used: w.last_used,
            index: w.index,
            window_type: w.window_type.into(),
        }
    }
}

#[derive(Debug, Clone, Hash, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct TabsRecordTabGroup {
    // Empty and closed tab groups are not included.
    pub id: String,
    pub name: String,
    pub color: String,
    pub collapsed: bool,
}

impl From<TabGroup> for TabsRecordTabGroup {
    fn from(group: TabGroup) -> Self {
        Self {
            id: group.id,
            name: group.name,
            color: group.color,
            collapsed: group.collapsed,
        }
    }
}

impl From<TabsRecordTabGroup> for TabGroup {
    fn from(group: TabsRecordTabGroup) -> Self {
        Self {
            id: group.id,
            name: group.name,
            color: group.color,
            collapsed: group.collapsed,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[cfg_attr(test, derive(Default))]
#[serde(rename_all = "camelCase")]
/// This struct mirrors what is stored on the server as the top-level payload.
pub struct TabsRecord {
    // `String` instead of `SyncGuid` because `SyncGuid` is optimized for short uids,
    // but we expect these to be long "xxx-xxx-xxx-xxx" FxA device uids.
    pub id: String,
    pub client_name: String,
    pub tabs: Vec<TabsRecordTab>,
    #[serde(default, skip_serializing_if = "skip_if_default")]
    pub tab_groups: HashMap<String, TabsRecordTabGroup>,
    #[serde(default, skip_serializing_if = "skip_if_default")]
    pub windows: HashMap<String, TabsRecordWindow>,
}

// Intended so logging is concise and doesn't leak tab titles, urls, etc.
// Like `Debug`, but counts the vecs instead of recursing them.
impl std::fmt::Display for TabsRecord {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "TabsRecord {{ id: {}, client_name: {}, tabs: {}, tab_groups: {}, windows: {} }}",
            self.id,
            self.client_name,
            self.tabs.len(),
            self.tab_groups.len(),
            self.windows.len()
        )
    }
}

#[cfg(test)]
pub mod test {
    use super::*;
    use serde_json::json;

    #[test]
    fn test_payload() {
        let payload = json!({
            "id": "JkeBPC50ZI0m",
            "clientName": "client name",
            "tabs": [{
                "title": "the title",
                "urlHistory": [
                    "https://mozilla.org/"
                ],
                "icon": "https://mozilla.org/icon",
                "lastUsed": 1643764207
            }]
        });
        let record: TabsRecord = serde_json::from_value(payload).expect("should work");
        assert_eq!(record.id, "JkeBPC50ZI0m");
        assert_eq!(record.client_name, "client name");
        assert_eq!(record.tabs.len(), 1);
        assert_eq!(record.windows.len(), 0);
        assert_eq!(record.tab_groups.len(), 0);
        let tab = &record.tabs[0];
        assert_eq!(tab.title, "the title");
        assert_eq!(tab.icon, Some("https://mozilla.org/icon".to_string()));
        assert_eq!(tab.last_used, 1643764207);
        assert!(!tab.inactive);
    }

    #[test]
    fn test_roundtrip() {
        let tab = TabsRecord {
            id: "JkeBPC50ZI0m".into(),
            client_name: "client name".into(),
            tabs: vec![TabsRecordTab {
                title: "the title".into(),
                url_history: vec!["https://mozilla.org/".into()],
                icon: Some("https://mozilla.org/icon".into()),
                last_used: 1643764207,
                inactive: true,
                ..Default::default()
            }],
            tab_groups: HashMap::new(),
            windows: HashMap::new(),
        };
        let round_tripped =
            serde_json::from_value(serde_json::to_value(tab.clone()).unwrap()).unwrap();
        assert_eq!(tab, round_tripped);
    }

    #[test]
    fn test_extra_fields() {
        let payload = json!({
            "id": "JkeBPC50ZI0m",
            // Let's say we agree on new tabs to record, we want old versions to
            // ignore them!
            "ignoredField": "??",
            "foo": [1, 2, 3],
            "bar": [{"id": 1}],
            "clientName": "client name",
            "tabs": [{
                "title": "the title",
                "urlHistory": [
                    "https://mozilla.org/"
                ],
                "icon": "https://mozilla.org/icon",
                "lastUsed": 1643764207,
                // Ditto - make sure we ignore unexpected fields in each tab.
                "ignoredField": "??",
            }]
        });
        let record: TabsRecord = serde_json::from_value(payload).unwrap();
        // The point of this test is really just to ensure the deser worked, so
        // just check the ID.
        assert_eq!(record.id, "JkeBPC50ZI0m");
    }

    #[test]
    fn test_windows_tab_groups() {
        let payload = json!({
            "id": "JkeBPC50ZI0m",
            "clientName": "client name",
            "tabs": [{
                "title": "the title",
                "urlHistory": [
                    "https://mozilla.org/"
                ],
                "icon": "https://mozilla.org/icon",
                "lastUsed": 1643764207
            }],
            "windows" : {
                "window-1" : {
                    "id": "window-1",
                    "lastUsed": 1,
                    "index": 0,
                    "windowType": 1
                }
            }
        });
        let record: TabsRecord = serde_json::from_value(payload).expect("should work");
        assert_eq!(record.windows.len(), 1);
        assert_eq!(record.windows.get("window-1").unwrap().id, "window-1");
        assert_eq!(
            record.windows.get("window-1").unwrap().last_used,
            types::Timestamp(1)
        );
        assert_eq!(record.tab_groups.len(), 0);
    }
}
