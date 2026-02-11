#!/bin/bash
# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Native HAP Build Script for Video Deidentification
# Builds directly on target hardware with TensorRT engine generation
#
# Usage: ./build-hap-native.sh [OPTIONS]
#   --repo URL            Git repository URL (default: https://github.com/nvidia-holoscan/holohub.git)
#   --branch BRANCH       Git branch/tag (default: main)
#   --local PATH          Use local holohub directory instead of cloning
#   --tag TAG             Docker image tag (default: video-deidentification)
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
LOCAL_PATH=""
APP_PATH="applications/video_deidentification"
TAG="video-deidentification"
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
        --local) LOCAL_PATH="$2"; shift 2 ;;
        --tag) TAG="$2"; shift 2 ;;
        --version) APP_VERSION="$2"; shift 2 ;;
        --sdk-version) SDK_VERSION="$2"; shift 2 ;;
        --cuda-version) CUDA_VERSION="$2"; shift 2 ;;
        --skip-engines) SKIP_ENGINES=true; shift ;;
        --work-dir) WORK_DIR="$2"; KEEP_WORK_DIR=true; shift 2 ;;
        --help|-h) head -20 "$0" | tail -13; exit 0 ;;
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
print_info "Video Deidentification - Native HAP Build"
print_info "=============================================="
if [ -n "$LOCAL_PATH" ]; then
    print_info "Source:       ${LOCAL_PATH} (local)"
else
    print_info "Repository:   ${REPO_URL}"
    print_info "Branch:       ${BRANCH}"
fi
print_info "Platform:     ${PLATFORM}"
print_info "Base Image:   ${BASE_IMAGE}"
print_info "Tag:          ${TAG}:${APP_VERSION}"
print_info "Work Dir:     ${WORK_DIR}"
print_info "=============================================="

# =============================================================================
# Step 1: Clone repository (or use local)
# =============================================================================
print_info "Step 1: Setting up repository..."

mkdir -p "$WORK_DIR"

if [ -n "$LOCAL_PATH" ]; then
    # Use local directory
    if [ ! -d "$LOCAL_PATH" ]; then
        print_error "Local path not found: $LOCAL_PATH"
        exit 1
    fi
    GIT_ROOT="$(cd "$LOCAL_PATH" && pwd)"
    print_info "Using local directory: $GIT_ROOT"
else
    # Clone from remote
    cd "$WORK_DIR"
    if [ -d "holohub/.git" ]; then
        print_info "Repository already exists at $WORK_DIR/holohub - reusing"
        cd holohub
    else
        print_info "Cloning repository..."
        git clone --depth 1 --branch "$BRANCH" "$REPO_URL" holohub
        cd holohub
    fi
    GIT_ROOT="$(pwd)"
fi

APP_DIR="${GIT_ROOT}/${APP_PATH}"
DATA_DIR="${GIT_ROOT}/data/video_deidentification"

if [ ! -d "$APP_DIR" ]; then
    print_error "Application directory not found: $APP_DIR"
    cleanup
    exit 1
fi

print_info "Repository at: $GIT_ROOT"

# =============================================================================
# Step 2: Download data and model
# =============================================================================
print_info "Step 2: Downloading model and sample data..."

mkdir -p "$DATA_DIR"

# Download PeopleNet model from NGC
MODEL_FILE="${DATA_DIR}/resnet34_peoplenet_int8.onnx"
if [ ! -f "$MODEL_FILE" ]; then
    print_info "Downloading PeopleNet model from NGC..."
    curl -S -o "$MODEL_FILE" \
        -L "https://api.ngc.nvidia.com/v2/models/org/nvidia/team/tao/peoplenet/pruned_quantized_decrypted_v2.3.3/files?redirect=true&path=resnet34_peoplenet_int8.onnx"
else
    print_info "Model already exists: $MODEL_FILE"
fi

# Download sample video
VIDEO_FILE="${DATA_DIR}/tourist.mp4"
if [ ! -f "$VIDEO_FILE" ]; then
    print_info "Downloading sample video from Pexels..."
    curl -S -o "$VIDEO_FILE" \
        -L "https://www.pexels.com/download/video/5271997/?fps=25.0&h=540&w=960"
else
    print_info "Video already exists: $VIDEO_FILE"
fi

# Convert video to GXF format
GXF_FILE="${DATA_DIR}/tourist.gxf_entities"
if [ ! -f "$GXF_FILE" ]; then
    print_info "Converting video to GXF format..."
    ffmpeg -i "$VIDEO_FILE" -pix_fmt rgb24 -f rawvideo pipe:1 2>/dev/null | \
        python3 "${GIT_ROOT}/utilities/convert_video_to_gxf_entities.py" \
        --directory "$DATA_DIR" --basename tourist --width 960 --height 540 --framerate 24
else
    print_info "GXF data already exists: $GXF_FILE"
fi

print_info "Data preparation complete"

# =============================================================================
# Step 3: Create HAP container
# =============================================================================
print_info "Step 3: Creating HAP container..."

BUILD_CONTEXT="${WORK_DIR}/hap-context"

mkdir -p "${BUILD_CONTEXT}"/{app,models,manifests}

# Copy application source
cp "${APP_DIR}/video_deidentification.py" "${BUILD_CONTEXT}/app/"
cp "${APP_DIR}/video_deidentification.yaml" "${BUILD_CONTEXT}/app/"

# Copy WebRTC operator for headless streaming
mkdir -p "${BUILD_CONTEXT}/app/operators/webrtc_server"
cp -r "${GIT_ROOT}/operators/webrtc_server/"*.py "${BUILD_CONTEXT}/app/operators/webrtc_server/"
touch "${BUILD_CONTEXT}/app/operators/__init__.py"
print_info "Copied WebRTC operator for headless streaming"

# Models will be copied after engine generation
mkdir -p "${BUILD_CONTEXT}/models"
print_info "Models will be added after TensorRT engine generation"

# Create manifests
cat > "${BUILD_CONTEXT}/manifests/app.json" << EOF
{
    "apiVersion": "1.0.0",
    "applicationName": "video_deidentification",
    "version": "${APP_VERSION}",
    "command": "python3 /opt/holoscan/app/video_deidentification.py --source replayer --config /opt/holoscan/app/video_deidentification.yaml --data /opt/holoscan/models",
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
    "resources": { "cpu": 2, "gpu": 1, "memory": "4Gi", "gpuMemory": "2Gi" }
}
EOF

# Copy holohub scripts for setup
cp "${GIT_ROOT}/holohub" "${BUILD_CONTEXT}/"
cp -r "${GIT_ROOT}/utilities" "${BUILD_CONTEXT}/"

# Create HAP Dockerfile
cat > "${BUILD_CONTEXT}/Dockerfile" << 'DOCKERFILE'
# syntax=docker/dockerfile:1
ARG BASE_IMAGE
FROM ${BASE_IMAGE} AS deps

ARG DEBIAN_FRONTEND=noninteractive

# Install easyocr, opencv, and WebRTC dependencies
RUN pip3 install --no-cache-dir \
    easyocr \
    opencv-python \
    aiohttp \
    aiortc

# Copy holohub scripts for setup (installs system dependencies)
RUN mkdir -p /tmp/scripts
COPY holohub /tmp/scripts/
RUN mkdir -p /tmp/scripts/utilities
COPY utilities /tmp/scripts/utilities/
RUN chmod +x /tmp/scripts/holohub
RUN /tmp/scripts/holohub setup && rm -rf /var/lib/apt/lists/*

# Runtime stage
FROM deps AS runtime

RUN mkdir -p /opt/holoscan/{app,models} \
    /etc/holoscan /var/holoscan/{input,output,logs}

COPY app/ /opt/holoscan/app/
COPY models/ /opt/holoscan/models/
COPY manifests/*.json /etc/holoscan/

ENV PYTHONPATH="/opt/nvidia/holoscan/python/lib:/opt/holoscan/app"
ENV HOLOSCAN_MODEL_PATH=/opt/holoscan/models/

WORKDIR /var/holoscan

COPY entrypoint.sh /opt/holoscan/
RUN chmod +x /opt/holoscan/entrypoint.sh
ENTRYPOINT ["/opt/holoscan/entrypoint.sh"]
DOCKERFILE

# Create entrypoint
cat > "${BUILD_CONTEXT}/entrypoint.sh" << 'EOF'
#!/bin/bash

# Video files should be mounted at /mnt/video (not /opt/holoscan/models)
VIDEO_MOUNT="/mnt/video"
MODELS_DIR="/opt/holoscan/models"

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
    echo "[WARN] Mount your video data with: -v /path/to/video:/mnt/video"
fi

case "${1:-}" in
    show) cat /etc/holoscan/app.json; cat /etc/holoscan/pkg.json ;;
    "") exec python3 /opt/holoscan/app/video_deidentification.py --source replayer --config /opt/holoscan/app/video_deidentification.yaml --data /opt/holoscan/models ;;
    *) exec python3 /opt/holoscan/app/video_deidentification.py --config /opt/holoscan/app/video_deidentification.yaml --data /opt/holoscan/models "$@" ;;
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
    print_info "Step 4: Generating TensorRT engine (this may take a few minutes)..."
    
    print_info "Running application to generate TensorRT engine..."
    print_info "(App may show errors in headless mode - this is expected)"
    print_info "Mounting data directory: ${DATA_DIR} -> /opt/holoscan/models"
    
    set +e
    docker run --rm --gpus all \
        --ulimit stack=33554432 \
        -e XDG_RUNTIME_DIR=/tmp \
        -v "${DATA_DIR}:/opt/holoscan/models" \
        "${TAG}:${APP_VERSION}-base" \
        --source replayer --data /opt/holoscan/models 2>&1 | head -100
    EXIT_CODE=$?
    set -e
    
    if [ $EXIT_CODE -ne 0 ]; then
        print_warn "Application exited with code $EXIT_CODE (expected in headless mode)"
    fi
    
    # Copy ONNX and ENGINE files
    print_info "Copying model and generated engine to build context..."
    
    for f in "${DATA_DIR}/"*.onnx "${DATA_DIR}/"*.engine.*; do
        [ -f "$f" ] && cp "$f" "${BUILD_CONTEXT}/models/" && print_info "  Copied: $(basename "$f")"
    done
    
    ENGINE_COUNT=$(find "${DATA_DIR}" -name "*.engine.*" 2>/dev/null | wc -l)
    print_info "Found $ENGINE_COUNT TensorRT engine(s)"
    
    # Rebuild container with model + engine
    print_info "Rebuilding container with model and TensorRT engine..."
    docker build \
        --build-arg BASE_IMAGE="${BASE_IMAGE}" \
        -t "${TAG}:${APP_VERSION}" \
        -t "${TAG}:latest" \
        .
    
    docker rmi "${TAG}:${APP_VERSION}-base" 2>/dev/null || true
else
    print_info "Step 4: Skipping TensorRT engine generation, copying ONNX model..."
    
    cp "${DATA_DIR}/"*.onnx "${BUILD_CONTEXT}/models/" 2>/dev/null || true
    
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
print_info "  - PeopleNet ONNX model (face detection)"
print_info "  - TensorRT engine (pre-generated for this GPU)"
print_info "  - EasyOCR for text detection (includes PyTorch)"
print_info "  - WebRTC streaming support (aiohttp, aiortc)"
print_info ""
print_info "NOT included (mount at runtime):"
print_info "  - Video data files (*.gxf_entities)"
print_info ""
print_info "To run with sample video:"
print_info "  docker run --rm --gpus all --runtime=nvidia \\"
print_info "    -e NVIDIA_DRIVER_CAPABILITIES=graphics,video,compute,utility,display \\"
print_info "    -e DISPLAY=\$DISPLAY \\"
print_info "    -v /tmp/.X11-unix:/tmp/.X11-unix \\"
print_info "    -v ${DATA_DIR}:/mnt/video \\"
print_info "    --ulimit stack=33554432 \\"
print_info "    ${TAG}:${APP_VERSION}"
print_info ""
print_info "To run with V4L2 camera:"
print_info "  docker run --rm --gpus all --runtime=nvidia \\"
print_info "    -e NVIDIA_DRIVER_CAPABILITIES=graphics,video,compute,utility,display \\"
print_info "    -e DISPLAY=\$DISPLAY \\"
print_info "    -v /tmp/.X11-unix:/tmp/.X11-unix \\"
print_info "    --device=/dev/video0 \\"
print_info "    --ulimit stack=33554432 \\"
print_info "    ${TAG}:${APP_VERSION} --source v4l2"
print_info ""
print_info "To run headless with WebRTC streaming:"
print_info "  docker run --rm --gpus all --runtime=nvidia \\"
print_info "    -e NVIDIA_DRIVER_CAPABILITIES=graphics,video,compute,utility \\"
print_info "    -v ${DATA_DIR}:/mnt/video \\"
print_info "    -p 8080:8080 \\"
print_info "    --ulimit stack=33554432 \\"
print_info "    ${TAG}:${APP_VERSION} --headless --source replayer"
print_info "  Then open http://localhost:8080 in a browser"
print_info ""
print_info "IMPORTANT: Mount video to /mnt/video (NOT /opt/holoscan/models)"
print_info "=============================================="

# Cleanup
cleanup
