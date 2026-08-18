// SPDX-License-Identifier: Apache-2.0
//
// souxmar-bridge build.rs — links against the C bridge and the engine
// archives when the `real-ffi` cargo feature is enabled.
//
// Sprint 13 push 3; link line rebuilt after it was found never to have
// worked. The previous version emitted a single `-L` for the C bridge's
// own directory, asked for a `souxmar-plugin-host` archive that is
// actually named `libsouxmar-plugin.a`, and omitted both the remaining
// engine archives and the third-party libraries they pull in — so
// `--features real-ffi` failed at link with ~30 undefined symbols and
// every BridgeFeatureSet flag stayed false in every build that shipped.
//
// Environment variables (consulted only when the feature is on):
//
//   SOUXMAR_BUILD_DIR          — a configured CMake build directory,
//                                e.g. build/dev. Every engine archive is
//                                derived from it. This is the one a
//                                developer normally sets.
//   SOUXMAR_C_BRIDGE_LIB_DIR   — directory holding libsouxmar-c-bridge.a.
//                                Defaults to <build>/src/c-bridge.
//                                Accepted on its own for backwards
//                                compatibility with the old contract.
//   SOUXMAR_C_BRIDGE_LIB_NAME  — archive base name, default
//                                "souxmar-c-bridge".
//   SOUXMAR_EXTRA_LINK_DIRS    — extra `-L` paths for the third-party
//                                libraries the engine links (yaml-cpp,
//                                fmt, sodium). Platform-separated.
//   SOUXMAR_EXTRA_LINK_LIBS    — extra library names, comma-separated.
//                                Defaults to the engine's known set.
//
// When the feature is off, this does nothing and the desktop builds
// with no C++ side at all.

use std::path::{Path, PathBuf};

/// Engine archives the C bridge pulls in, in link order. CMake emits
/// each one into its own directory under `<build>/src/`, which is why a
/// single `-L` was never going to be enough.
///
/// The tuple is (subdirectory under `<build>/src`, archive base name).
/// Note the mismatch that broke this before: the plugin host lives in
/// `plugin-host/` but its archive is `libsouxmar-plugin.a`.
const ENGINE_ARCHIVES: &[(&str, &str)] = &[
    ("pipeline", "souxmar-pipeline"),
    ("plugin-host", "souxmar-plugin"),
    ("ai", "souxmar-ai"),
    ("updater", "souxmar-update"),
    ("crypto", "souxmar-crypto"),
    ("core", "souxmar-core"),
];

fn path_separator() -> char {
    if cfg!(windows) {
        ';'
    } else {
        ':'
    }
}

fn archive_file_name(base: &str) -> String {
    if cfg!(windows) {
        format!("{base}.lib")
    } else {
        format!("lib{base}.a")
    }
}

fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    for var in [
        "SOUXMAR_BUILD_DIR",
        "SOUXMAR_C_BRIDGE_LIB_DIR",
        "SOUXMAR_C_BRIDGE_LIB_NAME",
        "SOUXMAR_EXTRA_LINK_DIRS",
        "SOUXMAR_EXTRA_LINK_LIBS",
    ] {
        println!("cargo:rerun-if-env-changed={var}");
    }

    if std::env::var_os("CARGO_FEATURE_REAL_FFI").is_none() {
        return;
    }

    // Resolve the C bridge directory, and the build root it implies.
    let (bridge_dir, build_dir): (PathBuf, Option<PathBuf>) =
        match (std::env::var("SOUXMAR_BUILD_DIR"), std::env::var("SOUXMAR_C_BRIDGE_LIB_DIR")) {
            (Ok(build), _) => {
                let build = PathBuf::from(build);
                (build.join("src").join("c-bridge"), Some(build))
            }
            (Err(_), Ok(bridge)) => {
                let bridge = PathBuf::from(bridge);
                // <build>/src/c-bridge → <build>
                let build = bridge.parent().and_then(Path::parent).map(Path::to_path_buf);
                (bridge, build)
            }
            (Err(_), Err(_)) => {
                // Don't hard-fail: `cargo check` and clippy runs without a
                // configured C++ build should still succeed.
                println!(
                    "cargo:warning=souxmar-bridge: real-ffi is on but neither \
                     SOUXMAR_BUILD_DIR nor SOUXMAR_C_BRIDGE_LIB_DIR is set; the \
                     link step will fail. Set SOUXMAR_BUILD_DIR to a configured \
                     CMake build directory (e.g. build/dev)."
                );
                return;
            }
        };

    let lib_name = std::env::var("SOUXMAR_C_BRIDGE_LIB_NAME")
        .unwrap_or_else(|_| "souxmar-c-bridge".to_string());

    if !bridge_dir.join(archive_file_name(&lib_name)).is_file() {
        println!(
            "cargo:warning=souxmar-bridge: {} not found in {} — build the C++ side first \
             (cmake --build <build> --target souxmar_c_bridge).",
            archive_file_name(&lib_name),
            bridge_dir.display()
        );
    }

    println!("cargo:rustc-link-search=native={}", bridge_dir.display());
    println!("cargo:rustc-link-lib=static={lib_name}");

    // One -L per engine archive directory. Missing archives are skipped
    // with a warning rather than silently dropped: a partial link line
    // produces a wall of undefined symbols that says nothing about the
    // cause, which is exactly how the previous version failed.
    if let Some(build) = build_dir.as_ref() {
        let src = build.join("src");
        for (subdir, archive) in ENGINE_ARCHIVES {
            let dir = src.join(subdir);
            if dir.join(archive_file_name(archive)).is_file() {
                println!("cargo:rustc-link-search=native={}", dir.display());
                println!("cargo:rustc-link-lib=static={archive}");
            } else {
                println!(
                    "cargo:warning=souxmar-bridge: {} not found under {} — skipping",
                    archive_file_name(archive),
                    dir.display()
                );
            }
        }
    }

    // Third-party libraries the engine archives reference. These come
    // from the platform package manager or vcpkg, so their location is
    // a property of the machine and has to be supplied rather than
    // guessed. CI and packaging set both variables.
    for dir in std::env::var("SOUXMAR_EXTRA_LINK_DIRS").unwrap_or_default().split(path_separator())
    {
        let dir = dir.trim();
        if !dir.is_empty() {
            println!("cargo:rustc-link-search=native={dir}");
        }
    }
    let extra_libs = std::env::var("SOUXMAR_EXTRA_LINK_LIBS")
        .unwrap_or_else(|_| "yaml-cpp,fmt,sodium".to_string());
    for lib in extra_libs.split(',') {
        let lib = lib.trim();
        if !lib.is_empty() {
            println!("cargo:rustc-link-lib={lib}");
        }
    }

    // C++ runtime. MSVC links its own automatically.
    match std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default().as_str() {
        "linux" => println!("cargo:rustc-link-lib=dylib=stdc++"),
        "macos" => println!("cargo:rustc-link-lib=dylib=c++"),
        "windows" => {}
        other => println!(
            "cargo:warning=souxmar-bridge: unknown target_os '{other}'; no C++ runtime linked"
        ),
    }
}
