# Development Notes

This file captures a few project-specific pitfalls that have come up in reviews.

## GTK Popover Lifecycle (Sudoku Cells)

- Reuse a single `Gtk.Popover` instance for all cells.
- Use `popup()` / `popdown()` for visibility; avoid `show()` and `unparent()` as lifecycle mechanisms.
- Connect `"closed"` once on the shared popover.

The goal is to avoid resource growth (e.g. file descriptors) and inconsistent popover teardown.

## Keyboard Focus After Popover Close

Focus restoration is part of UX correctness for keyboard play.

- Restore focus to the originating cell on passive dismissal (click outside, Escape, Done).
- Do not force focus restoration after action buttons (number / clear), since the action already advances state.

Implementation lives in `src/variants/classic_sudoku/manager.py` via the shared popover and the
`_restore_focus_on_popover_close` / `_last_popover_cell` state.

## Tests

- Prefer behavior-based tests.
- The FD regression test is `tests/test_fd_growth_shared_popover.py` (gated behind `SUDOKU_FD_TEST=1`).

## Android Porting: Repo-Local Reproducibility Plan

Goal:

- Build/install/launch the Android APK from this repository alone.
- Avoid any critical dependency on a private superproject.

### Inventory: Current External Dependencies

At the moment, the Android Sudoku module is still hosted outside this repo in
the superproject (`gnome-android-project/sudoku`).

The current Android path depends on external assets and scripts such as:

- `gnome-android-project/sudoku/build.gradle.kts` (Android app module)
- `gnome-android-project/sudoku/src/main/jni/*` (JNI + bootstrap sources)
- `gnome-android-project/tools/setup/install-sudoku-python-runtime.sh`
- `gnome-android-project/tools/c1/libappstream-stub/libappstream.so`
- Superproject-level Gradle and baseline/overlay path wiring (`A0_*` prefixes)

This means Android builds are currently not reproducible from this repo alone.

### Quickstart Target (to reach)

The Android quickstart for contributors should eventually be:

1. Clone this repo.
2. Run one repo-local setup command for runtime/toolchain prerequisites.
3. Run one repo-local build command.
4. Install and launch on emulator/device.

Expected command shape (target state):

```bash
./scripts/android/setup-runtime.sh
./scripts/android/build-apk.sh
./scripts/android/install-apk.sh
./scripts/android/launch.sh
```

### Migration TODO (android/wip)

- [ ] Move Android app module (`build.gradle`, manifest, JNI bootstrap) into this repo.
- [ ] Move Sudoku-specific runtime installer logic into this repo.
- [ ] Vendor or regenerate required runtime helper artifacts in-repo.
- [ ] Add a repo-local README section for Android prerequisites and smoke test.
- [ ] Add CI (or scripted check) for `assembleDebug` reproducibility.
