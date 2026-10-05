# Real plugin validation

These opt-in tools check plugin discovery, processing, state, editors and isolated workers. The normal CTest suite does not download or load third-party plugins. Keep each report with the tested executable hashes and plugin versions; passing one fixture does not establish compatibility with every plugin or device.

## VST3 manifest regression

The opt-in `Tests/scanner-manifest-regression.py` copies a supplied VST3 bundle into a unique ignored `out/manifest-regression` directory. It exercises missing, valid, invalid and stale manifests, compares full class IDs against the actual factory, and requires final module-fingerprint verification. It never edits the installed/original plugin.

```powershell
python Tests/scanner-manifest-regression.py --scanner out/build/windows-vs2022/LightHostModern_artefacts/Release/LightHostModernScanner.exe --module out/real-plugin-test/plugins/dragonfly-reverb-3.2.10/DragonflyRoomReverb.vst3
```

Use the actual extracted bundle path if the archive layout differs.

## Scanner fixture

The opt-in fixture uses [Dragonfly Reverb 3.2.10 for Windows x64](https://github.com/michaelwillis/dragonfly-reverb/releases/tag/3.2.10), a free plugin suite from its official publisher repository. It tests Early Reflections, Hall, Plate and Room in VST2 and VST3 format.

Build the Release host/scanner and CTest targets, then run from the repository:

```powershell
python Tests/real-plugin-scan.py --download
```

On a build without VST2, use `--formats VST3`. The normal CTest suite does not download or load third-party plugins. Subsequent explicit fixture runs reuse the cached archive and verify its SHA-256 before extraction. The hash is a reproducibility pin from the official downloaded artifact, not a publisher signature.

The archive, extracted binaries and JSON test report remain under ignored `out/real-plugin-test`. Nothing is installed into system plugin folders or copied into application release staging. The runner only uses `PluginScanController`: real modules load in the scanner child under its timeout and Windows Job Object. No audio devices, active host chain or session settings are created by the runner.

Checks include:

- Four validated modules per format, with distinct identities and stereo input/output counts.
- Module discovery through paths containing spaces, accented characters and Japanese characters.
- Exclusion of DLLs inside other plugin-format bundles from VST2 discovery.
- Description XML serialization and identity preservation.
- Acceptance of JUCE's Windows VST3 inner-binary identifiers while rejecting a description from another bundle.
- Cache reuse by a fresh controller with a deliberately nonexistent scanner executable, proving no worker launch is needed for unchanged restored descriptions.

`scan-results.json` records source URL, archive/scanner/runner hashes, timings and both formats' output. This validates discovery and cache behavior only. Audible processing, editors, device reconfiguration, MSI installation and compatibility with other publishers remain separate checks.

## Offline processing, state and editors

`LightHostModernRealPluginTests` is an opt-in Release target. It uses the actual realtime processor, slots and editor windows without constructing an audio device manager. Each module runs in its own process with a 60-second deadline:

```powershell
cmake --build out/build/windows-vs2022 --config Release --target LightHostModernRealPluginTests
python -X utf8 Tests/real-plugin-processing.py --download
```

The additional implementation is [Airwindows PurestGain](https://www.airwindows.com/purestgain-vst/), downloaded from the publisher's [original ZIP](https://www.airwindows.com/wp-content/uploads/2016/11/PurestGain.zip). Its pinned archive SHA-256 is `0a79f7b3c2d35fe7e3819edb64f680d6a141495bc6e7bb4bbca77e494f8fcc6e`. Only `PurestGain64.dll` is extracted.

Processing checks submit 17/64/257/1001-sample blocks, verify finite nonzero output and exact block/sample accounting, capture distinct original/duplicate states, toggle mute and global bypass, and run callbacks while coordinated state capture, reordering, preparation, editor reuse and destruction take place. VST3 parameter changes are delivered through a process boundary before state capture; synthetic bypass/program parameters are excluded. The allocation audit covers host-image C++/CRT calls, not allocations inside third-party DLLs.

## Isolated-worker checks with supplied plugins

Build `LightHostModernIsolatedRealPluginTests` and the Release worker, then pass
existing plugin paths to the opt-in runner. It does not download plugins.

```powershell
cmake --build out/build/windows-vs2022 --config Release --target LightHostModernIsolatedRealPluginTests LightHostModernWorker
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tests/IsolatedRealPluginTests.ps1 -PluginPaths "C:\YOUR PLUGINS\Example.vst3" -Format VST3
```

`-BuildDirectory` selects a different native build tree; `-TimeoutSeconds` defaults
to 120 per plugin. Reports are written to unique folders under
`out/isolated-real-plugins/`. The checks cover worker loading, editor lifecycle,
synthetic audio, state capture and restore. They do not open a real audio device
or establish long-session compatibility. See [process isolation](plugin-isolation.md)
for its runtime limits.
