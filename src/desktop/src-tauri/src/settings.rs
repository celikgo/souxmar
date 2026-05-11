// SPDX-License-Identifier: Apache-2.0
//
// User-scoped settings file for the desktop app. JSON, schema-
// discriminated, atomic write (tmp + rename). Today carries one
// field (onboarding_completed); Sprint 11+ adds chat-history,
// recent-projects, etc.

use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
pub struct Settings {
    pub schema: u32,
    #[serde(default)]
    pub onboarding_completed: bool,
}

impl Default for Settings {
    fn default() -> Self {
        Settings { schema: 1, onboarding_completed: false }
    }
}

pub fn settings_path() -> PathBuf {
    let base = dirs::config_dir().unwrap_or_else(|| PathBuf::from("."));
    base.join("souxmar").join("settings.json")
}

impl Settings {
    pub fn load(path: &Path) -> Option<Settings> {
        let bytes = fs::read(path).ok()?;
        serde_json::from_slice::<Settings>(&bytes).ok()
    }

    pub fn save(&self, path: &Path) -> std::io::Result<()> {
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }
        let tmp = path.with_extension("json.tmp");
        let body = serde_json::to_vec_pretty(self).map_err(std::io::Error::other)?;
        {
            let mut f = fs::File::create(&tmp)?;
            f.write_all(&body)?;
            f.sync_all()?;
        }
        fs::rename(&tmp, path)
    }
}
