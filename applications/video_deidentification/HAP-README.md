# Video Deidentification - HAP Build and Deployment Guide

This guide explains how to build and deploy the Video Deidentification application as a Holoscan Application Package (HAP) with pre-generated TensorRT engines.

## Overview

The HAP container includes:
- PeopleNet ONNX model (face detection)
- Pre-generated TensorRT engine (optimized for your GPU)
- EasyOCR for text detection (includes PyTorch)
- WebRTC streaming support (aiohttp, aiortc)

**NOT included** (mount at runtime):
- Video data files (`*.gxf_entities`, `*.gxf_index`)

## Prerequisites

- Docker with NVIDIA Container Toolkit
- NVIDIA GPU with appropriate drivers
- ~25GB disk space for the container
- Network access for downloading models (first build only)

## Building the HAP

### Option 1: Build from Local Source (Recommended for Development)

Use your local holohub directory with any custom changes:

```bash
./applications/video_deidentification/build-hap-native.sh \
    --local /path/to/your/holohub \
    --work-dir ~/hap-video-deid
```

### Option 2: Build from GitHub

Clone and build from the official repository:

```bash
./applications/video_deidentification/build-hap-native.sh \
    --work-dir ~/hap-video-deid
```

### Option 3: Build from Custom Fork/Branch

```bash
./applications/video_deidentification/build-hap-native.sh \
    --repo https://github.com/YOUR_USERNAME/holohub.git \
    --branch your-branch-name \
    --work-dir ~/hap-video-deid
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `--local PATH` | (none) | Use local holohub directory instead of cloning |
| `--repo URL` | nvidia-holoscan/holohub | Git repository URL |
| `--branch BRANCH` | main | Git branch/tag |
| `--tag TAG` | video-deidentification | Docker image tag |
| `--version VERSION` | 1.0.0 | Application version |
| `--sdk-version VER` | 3.10.0 | Holoscan SDK version |
| `--cuda-version VER` | 12 | CUDA version |
| `--work-dir DIR` | /tmp/hap-build-$$ | Working directory (reused if exists) |
| `--skip-engines` | (off) | Skip TensorRT engine generation |

### Build Output

After building:
- Docker image: `video-deidentification:1.0.0`
- Saved tar: `~/hap-output/video-deidentification-<arch>-1.0.0.tar`

## Running the HAP

### Display Mode (with Visualization Window)

Shows the deidentified video in a GUI window:

```bash
docker run --rm --gpus all --runtime=nvidia \
    -e NVIDIA_DRIVER_CAPABILITIES=graphics,video,compute,utility,display \
    -e DISPLAY=$DISPLAY \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -v /path/to/video/data:/mnt/video \
    --ulimit stack=33554432 \
    video-deidentification:1.0.0 --source replayer
```

### Headless Mode (WebRTC Streaming)

Streams the deidentified video to a web browser via WebRTC:

```bash
docker run --rm --gpus all --runtime=nvidia \
    -e NVIDIA_DRIVER_CAPABILITIES=graphics,video,compute,utility \
    -v /path/to/video/data:/mnt/video \
    -p 8080:8080 \
    --ulimit stack=33554432 \
    video-deidentification:1.0.0 --headless --source replayer
```

Then open `http://localhost:8080` in a web browser and click **Start**.

### With V4L2 Camera

**Display mode:**
```bash
docker run --rm --gpus all --runtime=nvidia \
    -e NVIDIA_DRIVER_CAPABILITIES=graphics,video,compute,utility,display \
    -e DISPLAY=$DISPLAY \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    --device=/dev/video0 \
    --ulimit stack=33554432 \
    video-deidentification:1.0.0 --source v4l2
```

**Headless mode:**
```bash
docker run --rm --gpus all --runtime=nvidia \
    -e NVIDIA_DRIVER_CAPABILITIES=graphics,video,compute,utility \
    --device=/dev/video0 \
    -p 8080:8080 \
    --ulimit stack=33554432 \
    video-deidentification:1.0.0 --headless --source v4l2
```

## Video Data

### Using Your Own Video

Convert your MP4 video to GXF format:

```bash
docker run --rm --entrypoint bash \
    -v /path/to/your/video:/input \
    -v /path/to/output:/output \
    video-deidentification:1.0.0 \
    -c "ffmpeg -i /input/video.mp4 -an -pix_fmt rgb24 -f rawvideo pipe:1 | \
        python3 /workspace/utilities/convert_video_to_gxf_entities.py \
        --directory /output --basename video --width 1280 --height 720 --framerate 25"
```

Then mount the output directory at runtime.

### Important: Mount Location

Always mount video data to `/mnt/video`, NOT `/opt/holoscan/models`:

```bash
# Correct
-v /path/to/video:/mnt/video

# Wrong (would overwrite models)
-v /path/to/video:/opt/holoscan/models
```

## Transferring to Another Machine

```bash
# On build machine
ls ~/hap-output/
# video-deidentification-x86_64-1.0.0.tar

# Transfer
scp ~/hap-output/video-deidentification-*.tar target-machine:~/

# On target machine
docker load -i ~/video-deidentification-x86_64-1.0.0.tar
```

## Show Container Information

```bash
docker run --rm video-deidentification:1.0.0 show
```

SPDX-License-Identifier: Apache-2.0

Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
