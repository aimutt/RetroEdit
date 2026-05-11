Open a PowerShell terminal (in VS Code: Ctrl+ `) and run these two commands from the project root:


# Step 1 — Configure (only need to do this once, or after changing CMakeLists.txt)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Step 2 — Debug Build
cmake --build build --config Debug

or do a rebuild like so
cmake --build build --config Debug --clean-first

The output executable ends up at:
build\Debug\RetroEdit.exe


# Release build
cmake --build build --config Release
cmake --build build --config Release --clean-first

SDL3.dll is automatically copied next to it, so you can run it directly:


.\build\Debug\RetroEdit.exe