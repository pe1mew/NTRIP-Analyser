#!/usr/bin/env pwsh
# Build script for NTRIP-Analyser GUI

$ErrorActionPreference = "Stop"

Write-Host "Compiling GUI resources..." -ForegroundColor Cyan
& 'C:/Program Files/CodeBlocks/MinGW/bin/windres.exe' gui/resource.rc -o gui/resource.o
if ($LASTEXITCODE -ne 0) {
    Write-Host "Resource compilation failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Building GUI executable..." -ForegroundColor Cyan
& 'C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe' -g -mwindows -std=c99 -D_USE_MATH_DEFINES `
    -o bin/ntrip-analyser-gui.exe `
    gui/gui_main.c gui/gui_layout.c gui/gui_events.c gui/gui_thread.c gui/gui_log.c gui/gui_parsers.c gui/gui_detail.c gui/gui_sky_window.c gui/gui_snapshot.c gui/gui_sv_detail.c `
    src/ntrip_handler.c src/rtcm3x_parser.c src/config.c src/nmea_parser.c src/sv_ephemeris.c src/sv_orbit.c src/rinex_nav.c `
    lib/cJSON/cJSON.c gui/resource.o `
    -Isrc -Ilib/cJSON -Igui `
    -lws2_32 -lcomctl32 -lcomdlg32 -lgdiplus -lm -Wall

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "GUI build successful: bin/ntrip-analyser-gui.exe" -ForegroundColor Green
