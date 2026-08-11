@echo off
echo Compiling GUI resources...
"C:\Program Files\CodeBlocks\MinGW\bin\windres.exe" -Isrc gui\resource.rc -o gui\resource.o
if %ERRORLEVEL% NEQ 0 (
    echo Resource compilation failed!
    exit /b 1
)

echo Building GUI executable...
"C:\Program Files\CodeBlocks\MinGW\bin\gcc.exe" -g -mwindows -std=c99 -D_USE_MATH_DEFINES -o bin\ntrip-analyser-gui.exe gui\gui_main.c gui\gui_layout.c gui\gui_events.c gui\gui_thread.c gui\gui_log.c gui\gui_parsers.c gui\gui_detail.c gui\gui_sky_window.c gui\gui_snapshot.c gui\gui_sv_detail.c gui\gui_vrs_window.c gui\gui_signal_window.c gui\gui_hist_window.c src\core\ns_stats.c src\net\ntrip_proto.c src\session\ntrip_session.c src\net\ntrip_handler.c src\core\rtcm3x_parser.c src\core\config.c src\core\nmea_parser.c src\core\sv_ephemeris.c src\core\sv_orbit.c src\core\sv_track.c src\core\iono.c src\core\rinex_nav.c lib\cJSON\cJSON.c gui\resource.o -Isrc -Ilib\cJSON -Igui -lws2_32 -lcomctl32 -lcomdlg32 -lgdiplus -lm -Wall
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    exit /b 1
)

echo.
echo GUI build successful: bin\ntrip-analyser-gui.exe
