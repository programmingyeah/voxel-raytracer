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
- Visual Studio 2022 or Build Tools with C++ support
- CMake
- Vulkan SDK
- GLFW with a CMake package config available
- GLM

Recommended dependency setup:
- use `vcpkg` for `glfw3` and `glm`
- point CMake at the vcpkg toolchain when configuring

Example:

```powershell README.md
git clone <your-repo-url> voxel_tracer
cd voxel_tracer
./scripts/build_windows.ps1
```

If you use vcpkg, configure CMake with its toolchain file before building, for example by editing the script invocation or running CMake manually.

The build automatically compiles:
- `src/assets/shaders/shader.comp`
- output SPIR-V: `build/shaders/comp.spv`

## Run

```bash README.md
./build/voxel_tracer
```

On Windows, the executable will typically be under:

```powershell README.md
.\build-windows\Release\voxel_tracer.exe
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
