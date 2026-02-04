#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Native HAP Build Script - Builds directly on target hardware
# Includes TensorRT engine generation for optimal performance
#
# Usage: ./build-hap-native.sh [OPTIONS]
#   --repo URL            Git repository URL (default: https://github.com/nvidia-holoscan/holohub.git)
#   --branch BRANCH       Git branch/tag (default: main)
#   --app PATH            Relative path to application (default: workflows/ai_surgical_video)
#   --tag TAG             Docker image tag (default: ai-surgical-video-workflow)
#   --version VERSION     Application version (default: 1.0.0)
#   --sdk-version VER     Holoscan SDK version (default: 3.10.0)
#   --cuda-version VER    CUDA version (default: 12)
#   --work-dir DIR        Working directory (reuses existing repo if present)
#   --skip-engines        Skip TRT engine generation
#   --help                Show this help message

set -e

# Defaults
REPO_URL="https://github.com/nvidia-holoscan/holohub.git"
BRANCH="main"
APP_PATH="workflows/ai_surgical_video"
TAG="ai-surgical-video-workflow"
APP_VERSION="1.0.0"
SDK_VERSION="3.10.0"
CUDA_VERSION="12"
SKIP_ENGINES=false
WORK_DIR=""
KEEP_WORK_DIR=false

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

print_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }
print_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

cleanup() {
    if [ "$KEEP_WORK_DIR" = true ]; then
        print_info "Keeping work directory: $WORK_DIR"
    elif [ -d "$WORK_DIR" ]; then
        print_info "Cleaning up work directory..."
        rm -rf "$WORK_DIR"
    fi
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --repo) REPO_URL="$2"; shift 2 ;;
        --branch) BRANCH="$2"; shift 2 ;;
        --app) APP_PATH="$2"; shift 2 ;;
        --tag) TAG="$2"; shift 2 ;;
        --version) APP_VERSION="$2"; shift 2 ;;
        --sdk-version) SDK_VERSION="$2"; shift 2 ;;
        --cuda-version) CUDA_VERSION="$2"; shift 2 ;;
        --skip-engines) SKIP_ENGINES=true; shift ;;
        --work-dir) WORK_DIR="$2"; KEEP_WORK_DIR=true; shift 2 ;;
        --help|-h) head -22 "$0" | tail -15; exit 0 ;;
        *) print_error "Unknown option: $1"; exit 1 ;;
    esac
done

# Set default work dir if not specified
if [ -z "$WORK_DIR" ]; then
    WORK_DIR="/tmp/hap-build-$$"
fi

# Detect architecture
ARCH=$(uname -m)
case "$ARCH" in
    x86_64|amd64) PLATFORM="x86_64" ;;
    aarch64|arm64) PLATFORM="arm64" ;;
    *) print_error "Unsupported architecture: $ARCH"; exit 1 ;;
esac

BASE_IMAGE="nvcr.io/nvidia/clara-holoscan/holoscan:v${SDK_VERSION}-cuda${CUDA_VERSION}-dgpu"
OUTPUT_DIR="${HOME}/hap-output"

print_info "=============================================="
print_info "Native HAP Build Script"
print_info "=============================================="
print_info "Repository:   ${REPO_URL}"
print_info "Branch:       ${BRANCH}"
print_info "App Path:     ${APP_PATH}"
print_info "Platform:     ${PLATFORM}"
print_info "Base Image:   ${BASE_IMAGE}"
print_info "Tag:          ${TAG}:${APP_VERSION}"
print_info "Work Dir:     ${WORK_DIR}"
print_info "=============================================="

# =============================================================================
# Step 1: Clone repository (or use existing)
# =============================================================================
print_info "Step 1: Setting up repository..."

mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

if [ -d "holohub/.git" ]; then
    print_info "Repository already exists at $WORK_DIR/holohub - reusing"
    cd holohub
    # Optionally update to latest
    # git fetch origin && git checkout "$BRANCH"
else
    print_info "Cloning repository..."
    git clone --depth 1 --branch "$BRANCH" "$REPO_URL" holohub
    cd holohub
fi

GIT_ROOT="$(pwd)"
APP_DIR="${GIT_ROOT}/${APP_PATH}"

if [ ! -d "$APP_DIR" ]; then
    print_error "Application directory not found: $APP_DIR"
    cleanup
    exit 1
fi

print_info "Repository at: $GIT_ROOT"

# =============================================================================
# Step 2: Build the application with HoloHub
# =============================================================================
print_info "Step 2: Building application with HoloHub..."

# Extract app name from path (last component)
APP_NAME=$(basename "$APP_PATH")

./holohub build "$APP_NAME"

print_info "Application built successfully"

# =============================================================================
# Step 3: Create HAP container
# =============================================================================
print_info "Step 3: Creating HAP container..."

BUILD_CONTEXT="${WORK_DIR}/hap-context"
DATA_DIR="${GIT_ROOT}/data"

mkdir -p "${BUILD_CONTEXT}"/{app,models,manifests,holohub_src/operators,holohub_src/cmake/pybind11,holohub_src/cmake/pydoc}

# Copy application source
if [ -d "${APP_DIR}/python" ]; then
    cp "${APP_DIR}/python/"*.py "${BUILD_CONTEXT}/app/" 2>/dev/null || true
    cp "${APP_DIR}/python/"*.yaml "${BUILD_CONTEXT}/app/" 2>/dev/null || true
else
    cp "${APP_DIR}/"*.py "${BUILD_CONTEXT}/app/" 2>/dev/null || true
    cp "${APP_DIR}/"*.yaml "${BUILD_CONTEXT}/app/" 2>/dev/null || true
fi

# Copy HoloHub operators source
cp -r "${GIT_ROOT}/operators/orsi" "${BUILD_CONTEXT}/holohub_src/operators/"
cp -r "${GIT_ROOT}/operators/deidentification" "${BUILD_CONTEXT}/holohub_src/operators/"
cp "${GIT_ROOT}/operators/operator_util.hpp" "${BUILD_CONTEXT}/holohub_src/operators/"

# Copy cmake infrastructure
cp "${GIT_ROOT}/cmake/pybind11_add_holohub_module.cmake" "${BUILD_CONTEXT}/holohub_src/cmake/"
cp "${GIT_ROOT}/cmake/pybind11/__init__.py" "${BUILD_CONTEXT}/holohub_src/cmake/pybind11/"
cp "${GIT_ROOT}/cmake/pydoc/macros.hpp" "${BUILD_CONTEXT}/holohub_src/cmake/pydoc/"

# Create CMakeLists.txt for operators
cat > "${BUILD_CONTEXT}/holohub_src/CMakeLists.txt" << 'CMAKE_EOF'
cmake_minimum_required(VERSION 3.20)
project(holohub_operators CXX CUDA)

find_package(holoscan REQUIRED CONFIG PATHS "/opt/nvidia/holoscan")
find_package(CUDAToolkit REQUIRED)

set(HOLOHUB_BUILD_PYTHON ON)
set(CMAKE_INSTALL_LIBDIR "lib")
set(HOLOSCAN_INSTALL_LIB_DIR "lib")
set(HOLOHUB_PYTHON_MODULE_OUT_DIR ${CMAKE_BINARY_DIR}/python/lib/holohub)
file(MAKE_DIRECTORY ${HOLOHUB_PYTHON_MODULE_OUT_DIR})

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(pybind11_add_holohub_module)

add_subdirectory(operators/orsi/orsi_format_converter)
add_subdirectory(operators/orsi/orsi_segmentation_preprocessor)
CMAKE_EOF

# Models will be copied AFTER engine generation (so engines are included)
# For base build, create empty models directory
mkdir -p "${BUILD_CONTEXT}/models"
print_info "Models will be added after TensorRT engine generation"

# Create manifests
cat > "${BUILD_CONTEXT}/manifests/app.json" << EOF
{
    "apiVersion": "1.0.0",
    "applicationName": "${APP_NAME}",
    "version": "${APP_VERSION}",
    "command": "python3 /opt/holoscan/app/ai_surgical_video.py --source replayer --config /opt/holoscan/app/config.yaml --data /opt/holoscan/models",
    "workingDirectory": "/var/holoscan/",
    "input": { "formats": ["file"] },
    "output": { "format": { "type": "screen" } }
}
EOF

cat > "${BUILD_CONTEXT}/manifests/pkg.json" << EOF
{
    "apiVersion": "1.0.0",
    "applicationRoot": "/opt/holoscan/app/",
    "modelRoot": "/opt/holoscan/models/",
    "resources": { "cpu": 4, "gpu": 1, "memory": "8Gi", "gpuMemory": "4Gi" }
}
EOF

# Create Dockerfile
cat > "${BUILD_CONTEXT}/Dockerfile" << DOCKERFILE
# Native build for ${PLATFORM}
ARG BASE_IMAGE
FROM \${BASE_IMAGE} AS builder

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \\
    build-essential cmake ninja-build git \\
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY holohub_src/ /build/src/

RUN cmake -S /build/src -B /build/out -G Ninja \\
    -DCMAKE_BUILD_TYPE=Release \\
    -DCMAKE_PREFIX_PATH=/opt/nvidia/holoscan \\
    -DHOLOHUB_BUILD_PYTHON=ON \\
    && cmake --build /build/out -j\$(nproc)

# Runtime stage
FROM \${BASE_IMAGE} AS runtime

ARG DEBIAN_FRONTEND=noninteractive

RUN pip3 install --no-cache-dir cupy-cuda12x 2>/dev/null || pip3 install --no-cache-dir cupy || true

RUN mkdir -p /opt/holoscan/{app,models,holohub,holohub_lib,operators} \\
    /etc/holoscan /var/holoscan/{input,output,logs}

COPY --from=builder /build/out/python/lib/holohub/ /opt/holoscan/holohub/
COPY --from=builder /build/out/operators/orsi/ /opt/holoscan/holohub_lib/orsi/

COPY holohub_src/operators/deidentification/ /opt/holoscan/operators/deidentification/
RUN touch /opt/holoscan/operators/__init__.py /opt/holoscan/operators/deidentification/__init__.py

RUN echo "from .orsi_format_converter import OrsiFormatConverterOp" > /opt/holoscan/holohub/__init__.py && \\
    echo "from .orsi_segmentation_preprocessor import OrsiSegmentationPreprocessorOp" >> /opt/holoscan/holohub/__init__.py

ENV LD_LIBRARY_PATH="/opt/holoscan/holohub_lib/orsi/orsi_format_converter:/opt/holoscan/holohub_lib/orsi/orsi_segmentation_preprocessor:/opt/nvidia/holoscan/lib:\${LD_LIBRARY_PATH}"

COPY app/ /opt/holoscan/app/
COPY models/ /opt/holoscan/models/
COPY manifests/*.json /etc/holoscan/

ENV PYTHONPATH="/opt/nvidia/holoscan/python/lib:/opt/holoscan/app:/opt/holoscan"
ENV HOLOSCAN_MODEL_PATH=/opt/holoscan/models/

WORKDIR /var/holoscan

COPY entrypoint.sh /opt/holoscan/
RUN chmod +x /opt/holoscan/entrypoint.sh
ENTRYPOINT ["/opt/holoscan/entrypoint.sh"]
DOCKERFILE

# Create entrypoint that symlinks video files from mount point
cat > "${BUILD_CONTEXT}/entrypoint.sh" << 'EOF'
#!/bin/bash

# Video files should be mounted at /mnt/video/orsi (not /opt/holoscan/models/orsi)
# This preserves the container's built-in ONNX models and TensorRT engines
# We symlink just the video files (.gxf_entities, .gxf_index) into the models directory

VIDEO_MOUNT="/mnt/video/orsi"
MODELS_DIR="/opt/holoscan/models/orsi"

if [ -d "$VIDEO_MOUNT" ]; then
    echo "[INFO] Linking video files from $VIDEO_MOUNT..."
    for f in "$VIDEO_MOUNT"/*.gxf_entities "$VIDEO_MOUNT"/*.gxf_index; do
        if [ -f "$f" ]; then
            ln -sf "$f" "$MODELS_DIR/$(basename "$f")" 2>/dev/null || true
            echo "[INFO]   Linked: $(basename "$f")"
        fi
    done
else
    echo "[WARN] No video mount found at $VIDEO_MOUNT"
    echo "[WARN] Mount your video data with: -v /path/to/orsi:/mnt/video/orsi"
fi

case "${1:-}" in
    show) cat /etc/holoscan/app.json; cat /etc/holoscan/pkg.json ;;
    "") exec python3 /opt/holoscan/app/ai_surgical_video.py --source replayer --config /opt/holoscan/app/config.yaml --data /opt/holoscan/models ;;
    *) exec python3 /opt/holoscan/app/ai_surgical_video.py "$@" ;;
esac
EOF

# Build initial container
print_info "Building Docker image..."
cd "${BUILD_CONTEXT}"

docker build \
    --build-arg BASE_IMAGE="${BASE_IMAGE}" \
    -t "${TAG}:${APP_VERSION}-base" \
    .

print_info "Initial container built: ${TAG}:${APP_VERSION}-base"

# =============================================================================
# Step 4: Generate TensorRT engines
# =============================================================================
if [ "$SKIP_ENGINES" = false ]; then
    print_info "Step 4: Generating TensorRT engines (this may take several minutes)..."
    
    CONTAINER_NAME="hap-engine-gen-$$"
    
    print_info "Running application to generate TensorRT engines..."
    print_info "(App may show errors in headless mode - this is expected)"
    print_info "Mounting entire data directory: ${DATA_DIR} -> /opt/holoscan/models"
    
    # Mount the WHOLE data directory (read-write so TensorRT can save engines)
    # This gives the app access to all models AND video files
    # Engines will be written directly to the host's data directory
    set +e
    docker run --rm --gpus all \
        --ulimit stack=33554432 \
        -e XDG_RUNTIME_DIR=/tmp \
        -v "${DATA_DIR}:/opt/holoscan/models" \
        "${TAG}:${APP_VERSION}-base" \
        --source replayer --data /opt/holoscan/models --frame-limit 10 --headless 2>&1
    EXIT_CODE=$?
    set -e
    
    # App likely failed (headless mode + Holoviz) but engines should be created
    if [ $EXIT_CODE -ne 0 ]; then
        print_warn "Application exited with code $EXIT_CODE (expected in headless mode)"
    fi
    
    # Now copy ONNX and ENGINE files from host data directory to build context
    # (video files are NOT copied - they will be mounted at runtime)
    print_info "Copying ONNX models and generated engines to build context..."
    
    ENGINE_COUNT=0
    
    # Engine files have pattern: *.engine.fp16 or *.engine.fp32 (not just *.engine)
    # Copy orsi models (ONNX + engines, but NOT video)
    mkdir -p "${BUILD_CONTEXT}/models/orsi/models"
    for f in "${DATA_DIR}/orsi/models/"*.onnx "${DATA_DIR}/orsi/models/"*.engine.*; do
        [ -f "$f" ] && cp "$f" "${BUILD_CONTEXT}/models/orsi/models/" && print_info "  Copied: orsi/models/$(basename "$f")"
    done
    ENGINE_COUNT=$((ENGINE_COUNT + $(find "${DATA_DIR}/orsi/models" -name "*.engine.*" 2>/dev/null | wc -l)))
    
    # Copy ssd_model (ONNX + engines)
    mkdir -p "${BUILD_CONTEXT}/models/ssd_model"
    for f in "${DATA_DIR}/ssd_model/"*.onnx "${DATA_DIR}/ssd_model/"*.engine.*; do
        [ -f "$f" ] && cp "$f" "${BUILD_CONTEXT}/models/ssd_model/" && print_info "  Copied: ssd_model/$(basename "$f")"
    done
    ENGINE_COUNT=$((ENGINE_COUNT + $(find "${DATA_DIR}/ssd_model" -name "*.engine.*" 2>/dev/null | wc -l)))
    
    # Copy monai_tool_seg_model (ONNX + engines)
    mkdir -p "${BUILD_CONTEXT}/models/monai_tool_seg_model"
    for f in "${DATA_DIR}/monai_tool_seg_model/"*.onnx "${DATA_DIR}/monai_tool_seg_model/"*.engine.*; do
        [ -f "$f" ] && cp "$f" "${BUILD_CONTEXT}/models/monai_tool_seg_model/" && print_info "  Copied: monai_tool_seg_model/$(basename "$f")"
    done
    ENGINE_COUNT=$((ENGINE_COUNT + $(find "${DATA_DIR}/monai_tool_seg_model" -name "*.engine.*" 2>/dev/null | wc -l)))
    
    print_info "Total: $ENGINE_COUNT TensorRT engine(s) found"
    
    if [ "$ENGINE_COUNT" -gt 0 ]; then
        # Rebuild container with ONNX + engines
        print_info "Rebuilding container with models and TensorRT engines..."
        docker build \
            --build-arg BASE_IMAGE="${BASE_IMAGE}" \
            -t "${TAG}:${APP_VERSION}" \
            -t "${TAG}:latest" \
            .
        
        # Remove base image
        docker rmi "${TAG}:${APP_VERSION}-base" 2>/dev/null || true
    else
        print_warn "No TensorRT engines generated. Rebuilding with ONNX models only..."
        docker build \
            --build-arg BASE_IMAGE="${BASE_IMAGE}" \
            -t "${TAG}:${APP_VERSION}" \
            -t "${TAG}:latest" \
            .
        docker rmi "${TAG}:${APP_VERSION}-base" 2>/dev/null || true
    fi
else
    # Skip engine generation - just copy ONNX models
    print_info "Step 4: Skipping TensorRT engine generation, copying ONNX models..."
    
    mkdir -p "${BUILD_CONTEXT}/models/orsi/models"
    cp "${DATA_DIR}/orsi/models/"*.onnx "${BUILD_CONTEXT}/models/orsi/models/" 2>/dev/null || true
    
    mkdir -p "${BUILD_CONTEXT}/models/ssd_model"
    cp "${DATA_DIR}/ssd_model/"*.onnx "${BUILD_CONTEXT}/models/ssd_model/" 2>/dev/null || true
    
    mkdir -p "${BUILD_CONTEXT}/models/monai_tool_seg_model"
    cp "${DATA_DIR}/monai_tool_seg_model/"*.onnx "${BUILD_CONTEXT}/models/monai_tool_seg_model/" 2>/dev/null || true
    
    # Rebuild with ONNX models
    docker build \
        --build-arg BASE_IMAGE="${BASE_IMAGE}" \
        -t "${TAG}:${APP_VERSION}" \
        -t "${TAG}:latest" \
        .
    docker rmi "${TAG}:${APP_VERSION}-base" 2>/dev/null || true
fi

# =============================================================================
# Step 5: Save container
# =============================================================================
print_info "Step 5: Saving container..."

mkdir -p "$OUTPUT_DIR"
TAR_FILE="${OUTPUT_DIR}/${TAG}-${PLATFORM}-${APP_VERSION}.tar"

docker save "${TAG}:${APP_VERSION}" -o "${TAR_FILE}"

print_info "=============================================="
print_info "Build complete!"
print_info "=============================================="
print_info "Image:    ${TAG}:${APP_VERSION}"
print_info "Saved to: ${TAR_FILE}"
print_info ""
print_info "Container includes:"
print_info "  - ONNX models (all 3)"
print_info "  - TensorRT engines (pre-generated for this GPU)"
print_info ""
print_info "NOT included (mount at runtime):"
print_info "  - Video data files (*.gxf_entities)"
print_info ""
print_info "To run:"
print_info "  docker run --rm --gpus all \\"
print_info "    --runtime=nvidia \\"
print_info "    -e NVIDIA_DRIVER_CAPABILITIES=graphics,video,compute,utility,display \\"
print_info "    -e DISPLAY=\$DISPLAY \\"
print_info "    -v /tmp/.X11-unix:/tmp/.X11-unix \\"
print_info "    -v /path/to/orsi:/mnt/video/orsi \\"
print_info "    --ulimit stack=33554432 \\"
print_info "    ${TAG}:${APP_VERSION}"
print_info ""
print_info "Example with HoloHub data:"
print_info "  docker run --rm --gpus all --runtime=nvidia \\"
print_info "    -e NVIDIA_DRIVER_CAPABILITIES=graphics,video,compute,utility,display \\"
print_info "    -e DISPLAY=\$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix \\"
print_info "    -v \${HOME}/holohub/data/orsi:/mnt/video/orsi \\"
print_info "    --ulimit stack=33554432 ${TAG}:${APP_VERSION}"
print_info ""
print_info "IMPORTANT: Mount to /mnt/video/orsi (NOT /opt/holoscan/models/orsi)"
print_info "           The entrypoint symlinks video files, preserving built-in models."
print_info "=============================================="

# Cleanup
cleanup
