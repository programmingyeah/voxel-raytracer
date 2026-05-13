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

```bash README.md
git clone <your-repo-url> voxel_tracer
cd voxel_tracer
cmake -S . -B build
cmake --build build
```

The build automatically compiles:
- `src/assets/shaders/shader.comp`
- output SPIR-V: `build/shaders/comp.spv`

## Run

```bash README.md
./build/voxel_tracer
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

## Project notes

Build assumptions:
- Linux / Ubuntu
- C++17
- Vulkan
- GLFW via `pkg-config`
- GLM installed system-wide
- compute shader compiled by CMake during build
