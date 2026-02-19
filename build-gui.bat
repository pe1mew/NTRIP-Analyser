@echo off
echo Compiling GUI resources...
"C:\Program Files\CodeBlocks\MinGW\bin\windres.exe" gui\resource.rc -o gui\resource.o
if %ERRORLEVEL% NEQ 0 (
    echo Resource compilation failed!
    exit /b 1
)

echo Building GUI executable...
"C:\Program Files\CodeBlocks\MinGW\bin\gcc.exe" -g -mwindows -std=c99 -D_USE_MATH_DEFINES -o bin\ntrip-analyser-gui.exe gui\gui_main.c gui\gui_layout.c gui\gui_events.c gui\gui_thread.c gui\gui_log.c gui\gui_parsers.c gui\gui_detail.c src\ntrip_handler.c src\rtcm3x_parser.c src\config.c src\nmea_parser.c lib\cJSON\cJSON.c gui\resource.o -Isrc -Ilib\cJSON -Igui -lws2_32 -lcomctl32 -lcomdlg32 -lm -Wall
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    exit /b 1
)

echo.
echo GUI build successful: bin\ntrip-analyser-gui.exe
