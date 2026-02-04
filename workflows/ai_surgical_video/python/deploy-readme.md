# Deploying AI Surgical Video Workflow as a HAP Container

This guide explains how to build and deploy the AI Surgical Video Workflow as a Holoscan Application Package (HAP) with pre-generated TensorRT engines for optimal performance.

## Overview

The `build-hap-native.sh` script builds the application directly on the target hardware (e.g., IGX Orin, Jetson, or x86 workstation with NVIDIA GPU). This approach:

- Generates TensorRT engines optimized for the specific GPU
- Bakes ONNX models and engines into the container
- Excludes large video files (mounted at runtime)
- Produces a portable, self-contained container

## Prerequisites

- **Docker** with NVIDIA Container Toolkit
- **NVIDIA GPU** with appropriate drivers
- **Git** for cloning the repository
- **~20GB disk space** for build artifacts
- **Network access** to pull base images from NGC

## Quick Start

```bash
# Download the script
curl -O https://raw.githubusercontent.com/nvidia-holoscan/holohub/main/workflows/ai_surgical_video/python/build-hap-native.sh
chmod +x build-hap-native.sh

# Build the HAP (this will take 10-20 minutes on first run)
./build-hap-native.sh

# Run the container
docker run --rm --gpus all --runtime=nvidia \
    -e NVIDIA_DRIVER_CAPABILITIES=graphics,video,compute,utility,display \
    -e DISPLAY=$DISPLAY \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -v ~/holohub/data/orsi:/mnt/video/orsi \
    --ulimit stack=33554432 \
    ai-surgical-video-workflow:1.0.0
```

## Build Options

```bash
./build-hap-native.sh [OPTIONS]

Options:
  --repo URL            Git repository URL (default: https://github.com/nvidia-holoscan/holohub.git)
  --branch BRANCH       Git branch/tag (default: main)
  --app PATH            Relative path to application (default: workflows/ai_surgical_video)
  --tag TAG             Docker image tag (default: ai-surgical-video-workflow)
  --version VERSION     Application version (default: 1.0.0)
  --sdk-version VER     Holoscan SDK version (default: 3.10.0)
  --cuda-version VER    CUDA version (default: 12)
  --work-dir DIR        Working directory (reuses existing repo if present)
  --skip-engines        Skip TRT engine generation (use ONNX models only)
  --help                Show help message
```

### Examples

```bash
# Build with a specific branch
./build-hap-native.sh --branch v3.0.0

# Reuse existing clone (faster rebuilds)
./build-hap-native.sh --work-dir ~/my-holohub-build

# Skip engine generation (engines will be created on first run)
./build-hap-native.sh --skip-engines

# Custom tag and version
./build-hap-native.sh --tag my-surgical-app --version 2.0.0
```

## Build Process

The script performs these steps:

1. **Clone Repository** - Downloads HoloHub source code
2. **Build Application** - Compiles C++ operators with Python bindings using `./holohub build`
3. **Create Base Container** - Builds Docker image with app and operators
4. **Generate TensorRT Engines** - Runs the app once to create optimized engines for the GPU
5. **Rebuild Final Container** - Bakes ONNX models and engines into the final image
6. **Save Container** - Exports to `~/hap-output/` as a tar file

## What's Included in the Container

| Included | Not Included |
|----------|--------------|
| Python application code | Video data files (*.gxf_entities) |
| C++ operators (compiled) | |
| ONNX models (3 models) | |
| TensorRT engines (pre-generated) | |
| Configuration files | |

## Running the Container

### Required Docker Flags

| Flag | Purpose |
|------|---------|
| `--gpus all` | Enable GPU access |
| `--runtime=nvidia` | Use NVIDIA container runtime |
| `-e NVIDIA_DRIVER_CAPABILITIES=...` | Enable graphics/display capabilities |
| `-e DISPLAY=$DISPLAY` | Pass display environment |
| `-v /tmp/.X11-unix:/tmp/.X11-unix` | Share X11 socket |
| `-v /path/to/orsi:/mnt/video/orsi` | Mount video data |
| `--ulimit stack=33554432` | Increase stack size |

### Video Data Mount

**Important:** Mount video data to `/mnt/video/orsi`, NOT `/opt/holoscan/models/orsi`.

The entrypoint automatically symlinks video files while preserving the container's built-in models and engines.

```bash
# Correct - symlinks video, preserves models
-v ~/holohub/data/orsi:/mnt/video/orsi

# Wrong - would overwrite models and engines!
-v ~/holohub/data/orsi:/opt/holoscan/models/orsi
```

### Full Run Command

```bash
docker run --rm --gpus all --runtime=nvidia \
    -e NVIDIA_DRIVER_CAPABILITIES=graphics,video,compute,utility,display \
    -e DISPLAY=$DISPLAY \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -v ~/holohub/data/orsi:/mnt/video/orsi \
    --ulimit stack=33554432 \
    ai-surgical-video-workflow:1.0.0
```

### Headless Mode

```bash
docker run --rm --gpus all --runtime=nvidia \
    -v ~/holohub/data/orsi:/mnt/video/orsi \
    --ulimit stack=33554432 \
    ai-surgical-video-workflow:1.0.0 \
    --source replayer --data /opt/holoscan/models --headless
```

### Show Container Manifests

```bash
docker run --rm ai-surgical-video-workflow:1.0.0 show
```

## Transferring to Another Machine

```bash
# On build machine
ls ~/hap-output/
# ai-surgical-video-workflow-arm64-1.0.0.tar

# Transfer to target
scp ~/hap-output/ai-surgical-video-workflow-arm64-1.0.0.tar target-machine:~/

# On target machine
docker load -i ~/ai-surgical-video-workflow-arm64-1.0.0.tar
```

## Troubleshooting

### "Failed to initialize glfw" or Display Errors

Ensure you have all display-related flags:
```bash
--runtime=nvidia \
-e NVIDIA_DRIVER_CAPABILITIES=graphics,video,compute,utility,display \
-e DISPLAY=$DISPLAY \
-v /tmp/.X11-unix:/tmp/.X11-unix
```

Also run `xhost +local:docker` on the host before running.

### "Could not open index file" / Video Not Found

Check that:
1. Video files exist in your mount source directory
2. You're mounting to `/mnt/video/orsi` (not `/opt/holoscan/models/orsi`)
3. The directory contains `*.gxf_entities` and `*.gxf_index` files

### Engine Generation Takes Too Long

First-time engine generation can take 5-15 minutes depending on GPU. Subsequent runs use cached engines.

### Stack Size Warning

If you see stack-related warnings, ensure `--ulimit stack=33554432` is set.

### Permission Denied on X11

```bash
xhost +local:docker
```

## Architecture Support

The script auto-detects the host architecture:
- **x86_64/amd64** - For workstations and servers
- **aarch64/arm64** - For IGX Orin, Jetson, and ARM servers

TensorRT engines are architecture and GPU-specific. Build on the target hardware for best results.

## License

SPDX-License-Identifier: Apache-2.0

Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
