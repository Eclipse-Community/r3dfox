/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::{env, path::Path};

fn main() {
    windows_manifest();
    crash_annotations();
    set_mock_cfg();
    generate_buildid_section();
}

fn windows_manifest() {
    use embed_manifest::{embed_manifest, manifest, new_manifest};

    if std::env::var_os("CARGO_CFG_WINDOWS").is_none() {
        return;
    }

    // See https://docs.rs/embed-manifest/1.4.0/embed_manifest/fn.new_manifest.html for what the
    // default manifest includes. The defaults include almost all of the settings used in the old
    // crash reporter.
    let manifest = new_manifest("CrashReporter")
        // Use legacy active code page because GDI doesn't support per-process UTF8 (and older
        // win10 may not support this setting anyway).
        .active_code_page(manifest::ActiveCodePage::Legacy)
        // GDI scaling is not enabled by default but we need it to make the GDI-drawn text look
        // nice on high-DPI displays.
        .gdi_scaling(manifest::Setting::Enabled);

    embed_manifest(manifest).expect("unable to embed windows manifest file");

    println!("cargo:rerun-if-changed=build.rs");
}

/// Generate crash annotation information from the yaml definition file.
fn crash_annotations() {
    use std::fs::File;
    use std::io::{BufWriter, Write};
    use yaml_rust::{Yaml, YamlLoader};

    let crash_annotations = Path::new("../../CrashAnnotations.yaml")
        .canonicalize()
        .unwrap();
    println!("cargo:rerun-if-changed={}", crash_annotations.display());

    let crash_ping_file = Path::new(&env::var("OUT_DIR").unwrap()).join("crash_annotations.rs");

    let yaml = std::fs::read_to_string(crash_annotations).unwrap();
    let Yaml::Hash(entries) = YamlLoader::load_from_str(&yaml)
        .unwrap()
        .into_iter()
        .next()
        .unwrap()
    else {
        panic!("unexpected crash annotations root type");
    };

    let mut ping_annotations = phf_codegen::Set::new();
    let mut annotations = phf_codegen::Set::new();

    for (k, v) in entries {
        let scope = v["scope"].as_str().unwrap_or("client");
        match scope {
            "ping-only" => {
                ping_annotations.entry(k.into_string().unwrap());
            }
            "ping" => {
                ping_annotations.entry(k.clone().into_string().unwrap());
                annotations.entry(k.into_string().unwrap());
            }
            "report" => {
                annotations.entry(k.into_string().unwrap());
            }
            _ => (),
        }
    }

    let mut file = BufWriter::new(File::create(&crash_ping_file).unwrap());
    writeln!(
        &mut file,
        "static PING_ANNOTATIONS: phf::Set<&'static str> = {};",
        ping_annotations.build(),
    )
    .unwrap();
    writeln!(
        &mut file,
        "static ALL_REPORT_ANNOTATIONS: phf::Set<&'static str> = {};",
        annotations.build(),
    )
    .unwrap();
}

/// Set the mock configuration option when tests are enabled or when the mock feature is enabled.
fn set_mock_cfg() {
    // Very inconveniently, there's no way to detect `cfg(test)` from build scripts. See
    // https://github.com/rust-lang/cargo/issues/4789. This seems like an arbitrary and pointless
    // limitation, and only complicates the evaluation of mock behavior. Because of this, we have a
    // `mock` feature which is activated by `toolkit/library/rust/moz.build`.
    println!("cargo:rustc-check-cfg=cfg(mock)");
    if env::var_os("CARGO_FEATURE_MOCK").is_some() || mozbuild::config::MOZ_CRASHREPORTER_MOCK {
        println!("cargo:rustc-cfg=mock");
    }
}

/// Generate the buildid section name (we read the buildid at runtime using buildid_reader).
fn generate_buildid_section() {
    use mozbuild::config::BINDGEN_SYSTEM_FLAGS as CFLAGS;

    let defines = match std::env::var("CARGO_CFG_TARGET_OS").unwrap().as_str() {
        "macos" => "#define XP_DARWIN",
        "windows" => "#define XP_WIN",
        _ => "",
    };

    let bindings = bindgen::Builder::default()
        .header_contents("defines.h", defines)
        .header(format!(
            "{}/toolkit/library/buildid_section.h",
            mozbuild::TOPSRCDIR.display()
        ))
        .clang_args(CFLAGS)
        .generate_cstr(true)
        .generate()
        .expect("unable to generate buildid_section.h");
    let out_path = std::path::PathBuf::from(std::env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("buildid_section.rs"))
        .expect("failed to write buildid section");
}
