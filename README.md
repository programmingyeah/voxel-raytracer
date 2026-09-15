# voxel_tracer

Vulkan-based voxel raytracer

## Dependencies

Install the required packages:

```bash README.md
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    pkg-config \
    libvulkan-dev \
    vulkan-validationlayers-dev \
    vulkan-tools \
    mesa-vulkan-drivers \
    libglfw3-dev \
    libglm-dev \
    glslc
```

## Build

### Linux

```bash README.md
git clone https://github.com/programmingyeah/voxel-raytracer voxel_tracer
cd voxel_tracer
./scripts/build_linux.sh
```

Or manually:

```bash README.md
cmake -S . -B build
cmake --build build
```

### Windows

Install first:
- a working C++ toolchain usable from Command Prompt, such as MinGW-w64
- CMake
- Vulkan SDK
- GLFW with a CMake package config available
- GLM

Recommended dependency setup:
- use `vcpkg` for `glfw3` and `glm`
- point CMake at the vcpkg toolchain when configuring

Example:

```bat
git clone <your-repo-url> voxel_tracer
cd voxel_tracer
scripts\build_windows.bat
```

If you use vcpkg, pass its toolchain file as the first argument:

```bat
scripts\build_windows.bat C:\vcpkg\scripts\buildsystems\vcpkg.cmake
```

The build automatically compiles:
- `src/assets/shaders/shader.comp`
- output SPIR-V: `build/shaders/comp.spv`

If `build-windows` only contains `CMakeCache.txt` and `CMakeFiles`, configuration likely succeeded but the actual compile step failed. The batch script now pauses so you can read the error.

## Run

```bash README.md
./build/voxel_tracer
```

On Windows, the executable will typically be under:

```bat
.\build-windows\voxel_tracer.exe
```

## Controls

- `W / S` forward / backward
- `A / D` strafe left / right
- `Space` move up
- `Left Shift` move down
- mouse look

## Troubleshooting

If the app fails to start:

- Check Vulkan is available:

```bash README.md
vulkaninfo | head
```

- If shader compilation fails, confirm `glslc` exists:

```bash README.md
which glslc
```

- If CMake cannot find GLFW or GLM, make sure the packages above installed successfully.
