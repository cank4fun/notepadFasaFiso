# benchmark checks

These targets exist only with `NFF_BUILD_BENCHMARKS=ON`. They are not CTest
entries and add no work to the normal 29-test suite.

From an x64 Visual Studio developer PowerShell, with `VCPKG_ROOT` already set:

```powershell
cmake --preset windows-portable -DNFF_BUILD_TESTS=ON -DNFF_BUILD_BENCHMARKS=ON
cmake --build build/windows-portable --parallel
ctest --test-dir build/windows-portable --output-on-failure
.\build\windows-portable\benchmarks\nff_hot_path_bench.exe
foreach ($operation in 'open','move','split','restore','cache','cache-miss') {
    & .\build\windows-portable\benchmarks\nff_allocation_failure_tests.exe $operation
    if ($LASTEXITCODE -ne 0) { throw "allocation test failed: $operation" }
}
```

Run each large size as a separate process so peak memory is meaningful. Start
small. These are opt-in stress runs, with temporary generated data, not your
real files. Save testing is optional and writes only the temporary fixture.

```powershell
.\build\windows-portable\benchmarks\nff_hot_path_bench.exe --large 64 --save
.\build\windows-portable\benchmarks\nff_hot_path_bench.exe --large 256 --save
.\build\windows-portable\benchmarks\nff_hot_path_bench.exe --large 512 --save
.\build\windows-portable\benchmarks\nff_hot_path_bench.exe --large 1024 --save
```

`--large 2048` is a manual core-only option. It is not run automatically.

The wx component benchmark is built when both benchmark and wx GUI options
are enabled. It reports core materialization, wxString conversion, Scintilla
SetText, layout, memory, and separate core/Scintilla newline edits:

```powershell
.\build\windows-portable\benchmarks\nff_wx_large_bench.exe 64
.\build\windows-portable\benchmarks\nff_wx_large_bench.exe 256
.\build\windows-portable\benchmarks\nff_wx_large_bench.exe 512
.\build\windows-portable\benchmarks\nff_wx_large_bench.exe 1024
```

The window ignores close while measurements run. It closes after completion.
This measures components with wrapping disabled. It does not measure the full
app's event synchronization, coloring, undo integration, or all editor options.
Also test actual Edit Anyway, edit/undo/redo, tab switching, and save in the app.

The hot benchmark covers 1-byte, 4 KiB and 4 MiB edits with ASCII, a leading
Unicode character followed by ASCII, and invalid UTF-8 at the end; a 128-pane,
4,096-view workspace; cache hits; and 4 MiB text analysis. `kind=0` is ASCII,
`kind=1` is mixed (the 1-byte case stays ASCII), and `kind=2` is invalid.
Compare separately built reference/candidate binaries on the same machine, alternate
runs, and inspect variability. Tiny-operation timings include timer overhead.

Allocation failures preserve valid workspace/cache invariants, not every byte
of state: failed open/split operations may consume an ID. Session corruption
coverage is in the normal persistence executable and recomputes envelope
checksums so malformed payloads reach the parser.

Linux with an installed CMake/Ninja toolchain:

```bash
cmake --preset bench-release
cmake --build --preset bench-release --parallel
ctest --preset bench-release
./build/bench-release/benchmarks/nff_hot_path_bench
./build/bench-release/benchmarks/nff_allocation_failure_tests cache
```

Use `-DNFF_BUILD_WX_GUI=ON` with the benchmark preset to build the wx target;
a working graphical session and wxWidgets/GTK development packages are needed.
