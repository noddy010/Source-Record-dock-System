# Source Record Times

An OBS Studio dock that shows **when each [Source Record](https://github.com/exeldro/obs-source-record)
recording started, paused, resumed and stopped**.

* **Recordings table** – one row per recording: source, status, start / end time, number of pauses,
  last pause, last resume and the live *recorded* time (pauses are not counted).
* **Event log** – every start / pause / resume / stop with its wall-clock time and the
  *position in the file* (handy for cutting later, because pauses are removed from the video).
* **Copy log / Save log as CSV / Clear** buttons.

Times marked with `~` were detected by polling (for example if a recording was already running when
OBS started) and can be off by a fraction of a second.

## How it works

Source Record does not publish events, but every recording it makes is a normal libobs output.
The plugin finds those outputs, matches them to the Source Record filter (and its source), and
listens to libobs' own `start`, `pause`, `unpause` and `stop` signals. Nothing in Source Record is
modified, and the dock never touches your recordings.

## Install (Windows)

1. Close OBS.
2. Unzip `obs-source-record-times-1.0.0-windows-x64.zip` into `C:\ProgramData\obs-studio\plugins\`
   so you end up with `C:\ProgramData\obs-studio\plugins\obs-source-record-times\bin\64bit\obs-source-record-times.dll`.
3. Start OBS, then open **Docks > Source Record Times**.

Built against the OBS 31 SDK; works with OBS 31 and newer (including 32.x).

## Build

Push this folder to a GitHub repository. The *Build Windows plugin* workflow (Actions tab) builds
the zip for you; download it from the run's **Artifacts**. Tag a commit (for example `1.0.0`) to
also attach the zip to a GitHub Release.

To build locally on Windows you need Visual Studio 2022 and CMake, then:

```
cmake --preset windows-x64
cmake --build --preset windows-x64 --config Release
```

## License

GPL-2.0-or-later.
