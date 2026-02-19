# Compiling

The code is setup for compilation in Windows and Linux using *#DEFINES*.

## CLI Application

### Windows
This code was originally developed on Windows using the Mingw compiler that comes with Code::Blocks. For this the primary compiler was configured in Visual Studio Code. See [tasks.json](.vscode/tasks.json).

For Windows: install Code::Blocks with Mingw compiler and Visual Studio Code. In VSC type `ctrl-shift-b` to compile the code.

### Linux
For linux install the following:
```bash
sudo apt install build-essential manpages-dev
```

Make sure the `bin` directory exists:
```bash
mkdir -p bin
```

To compile, in the root of the repository execute: 
```bash
gcc -g -o bin/ntripanalyser src/*.c lib/cjson/cJSON.c -Ilib/cjson -Wall -lm
```

This command will:
- Compile all C source files in the `src/` directory using wildcard (`src/*.c`)
- Include the cJSON library from `lib/cjson/cJSON.c`
- Add the cJSON headers to include path (`-Ilib/cjson`)
- Enable debug symbols (`-g`) and all warnings (`-Wall`)
- Link the math library (`-lm`)
- Output the executable to `bin/ntripanalyser`

## GUI Application (Windows Only)

For detailed GUI compilation instructions, build methods, and troubleshooting, see the **[GUI Documentation](gui.md#building-the-gui)**.

**Quick build:**
```batch
build-gui.bat
```


