# Changelog

## Unreleased

- Warm the build caches from the default branch three times a week, so a
  renamed release branch no longer starts from nothing.
- Drive `Release` and `Cache warm` through one set of composite actions, and
  cache the Windows compiler for the first time.
- Report per-target build times and compiler cache statistics in the run
  summary, and lint the composite actions in `Source checks`.

## 7.2.8 Windows 11 package 1 - 2026-09-16

- Build the Windows x64 application with Windows 11 SDK 10.0.26100.0.
- Enable Per-Monitor V2 DPI behavior and long-path-aware file access while
  retaining Windows 10 compatibility.
- Publish separately named Setup and Portable packages, verify both package
  layouts, and include SHA-256 checksums and the full pinned source archive.

## 7.2.8 - 2026-09-15

- Updated the application base to Telegram Desktop 7.2.8, which fixes crashes
  on invalid Lottie files.
- Picked up the 7.2.6 feature release: image editor text tool, folder and file
  set sending, GIF editing before send, and call rating in the call panel.
- Replaced the rlottie animation library with tlottie, following upstream;
  preparation now builds a Rust static library under ThirdParty.
- Built the two macOS architectures in separate release jobs, because one cold
  universal build exceeds the six-hour job limit.

## 7.2.5 - 2026-09-06

- Updated the application base to Telegram Desktop 7.2.5.
- Preserved AyuGram features and upstream attribution without fork-specific branding.
- Fixed language synchronization, macOS bundle identity and application icons.
- Added reproducible technical packages for universal macOS and Windows x64.
- Disabled automatic updates until a signed update channel is available.
