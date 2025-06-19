# Compiling
The code is setup for compilation in Windows and Linux using *#DEFINES*.

## Windows
This code was originally developed on Windows using the Mingw compiler that comes with Code::Blocks. For this the primary compiler was configured in Visual Studio Code. See [tasks.json](.vscode/tasks.json).

For Windows: install Code::Blocks with Mingw compiler and Visual Studio Code. In VSC type `ctrl-shift-b` to compile te code.

## Linux
For linux install the following:
```bash
sudo apt install build-essential manpages-dev
```
To compile, in the root of the repository execute: 
```bash
gcc etceta
```


