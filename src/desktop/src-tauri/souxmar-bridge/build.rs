// SPDX-License-Identifier: Apache-2.0
//
// souxmar-bridge build.rs — links against libsouxmar-c-bridge.a
// when the `real-ffi` cargo feature is enabled.
//
// Sprint 13 push 3.
//
// Environment variables consulted (when feature is on):
//
//   SOUXMAR_C_BRIDGE_LIB_DIR  — directory containing
//                               libsouxmar-c-bridge.a. Required.
//   SOUXMAR_C_BRIDGE_LIB_NAME — archive base name, defaults to
//                               "souxmar-c-bridge".
//
// The CMake build emits the archive under
// `<build-dir>/src/c-bridge/`. The Tauri build (`cargo tauri
// build` or `cargo build -p souxmar-bridge --features real-ffi`)
// is invoked with that path exported in CI; locally a developer
// sets it manually.
//
// When the feature is off, build.rs does nothing — the Bridge
// uses its previous scaffolding stubs and the desktop builds
// without needing a C++ side at all.

fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=SOUXMAR_C_BRIDGE_LIB_DIR");
    println!("cargo:rerun-if-env-changed=SOUXMAR_C_BRIDGE_LIB_NAME");

    if std::env::var_os("CARGO_FEATURE_REAL_FFI").is_none() {
        return;
    }

    let lib_dir = match std::env::var("SOUXMAR_C_BRIDGE_LIB_DIR") {
        Ok(v) => v,
        Err(_) => {
            // Don't hard-fail — cargo check / clippy runs without
            // the env var should still succeed. Emit a warning so
            // the human notices when they wire up a real build.
            println!(
                "cargo:warning=souxmar-bridge: real-ffi feature is on but \
                 SOUXMAR_C_BRIDGE_LIB_DIR is unset; the linker will fail \
                 unless this resolves before the final link step."
            );
            return;
        }
    };
    let lib_name = std::env::var("SOUXMAR_C_BRIDGE_LIB_NAME")
        .unwrap_or_else(|_| "souxmar-c-bridge".to_string());

    println!("cargo:rustc-link-search=native={}", lib_dir);
    println!("cargo:rustc-link-lib=static={}", lib_name);

    // The C bridge depends on libsouxmar-pipeline + libsouxmar-core
    // + libsouxmar-plugin-host, which themselves bring transitive
    // C++ dependencies. Link the C++ standard library + the
    // dependent souxmar archives explicitly. On macOS this is
    // libc++; on Linux libstdc++.
    let target_os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    match target_os.as_str() {
        "linux" => {
            println!("cargo:rustc-link-lib=static=souxmar-pipeline");
            println!("cargo:rustc-link-lib=static=souxmar-core");
            println!("cargo:rustc-link-lib=static=souxmar-plugin-host");
            println!("cargo:rustc-link-lib=dylib=stdc++");
        }
        "macos" => {
            println!("cargo:rustc-link-lib=static=souxmar-pipeline");
            println!("cargo:rustc-link-lib=static=souxmar-core");
            println!("cargo:rustc-link-lib=static=souxmar-plugin-host");
            println!("cargo:rustc-link-lib=dylib=c++");
        }
        "windows" => {
            // MSVC links libcmt / libcpmt automatically when
            // `/MT` is set; nothing extra needed here. The
            // souxmar archives still need to be on the link
            // path, but their names match.
            println!("cargo:rustc-link-lib=static=souxmar-pipeline");
            println!("cargo:rustc-link-lib=static=souxmar-core");
            println!("cargo:rustc-link-lib=static=souxmar-plugin-host");
        }
        other => {
            println!(
                "cargo:warning=souxmar-bridge: unknown target_os '{}'; \
                 only stdc++/c++ runtimes for linux/macos are wired",
                other
            );
        }
    }
}
