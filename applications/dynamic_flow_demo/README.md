# Dynamic Flow Demo

An interactive Holoscan application demonstrating dynamic workflow reconfiguration with a graphical user interface.

## Overview

This application showcases Holoscan's dynamic flow control capabilities, allowing users to visually edit and reconfigure operator execution order in real-time through an ImGui-based interface. It demonstrates how to build interactive Holoscan applications with live workflow modification.

**Key Features:**
- 🎨 **Interactive GUI** - ImGui-based visual workflow editor
- 🔄 **Drag-and-Drop** - Reorder operators by dragging
- ✅ **Enable/Disable** - Toggle operators on/off with checkboxes
- ⚡ **Live Updates** - See data flowing through the pipeline in real-time
- 🔧 **Dynamic Reconfiguration** - Changes take effect when you click Apply

This application is built using Holoscan SDK and supports the following platforms:
- x86_64
- arm64 (including Jetson)

## Prerequisites

### For Running in Container (Recommended)
- Docker
- NVIDIA Container Toolkit (for GPU support)
- X11 display server access (for GUI)

### For Local Development
- Holoscan SDK 3.6.0 or later
- CMake 3.20+
- C++17 compatible compiler
- CUDA Toolkit (for GPU features)
- X11 development libraries

## Installation

The application is part of HoloHub and doesn't require separate installation. Simply clone the HoloHub repository:

```bash
git clone https://github.com/nvidia-holoscan/holohub.git
cd holohub
```

## Usage

### Running the Application

```bash
./holohub run dynamic_flow_demo
```

By default, the `./holohub build` and `./holohub run` commands will build and run the application in a containerized environment using the `standard` mode.

For local development without containers, use the `--local` flag:

```bash
./holohub run dynamic_flow_demo --local
```

Note that for the `--local` flag, the relevant custom dependencies (e.g. `requirements.txt` for Python) will be ignored and need to be installed manually.

### Using the Interactive Interface

When the application starts, a GUI window opens showing the current workflow:

#### **Workflow Display**
- **Source** (fixed) - Generates incrementing integer values every second
- **Operator One, Two, Three** - Pass-through operators that can be reordered
- **Sink** (fixed) - Consumes the final output
- **Values** - Live data values are displayed next to each operator in green

#### **Modifying the Workflow**

1. **Reorder Operators**
   - Click and hold on any operator name
   - Drag to a new position
   - Release to drop and swap positions
   - The Apply button will become enabled

2. **Enable/Disable Operators**
   - Check/uncheck the checkbox next to each operator
   - Disabled operators turn gray and show "(disabled)"
   - Disabled operators are skipped in the workflow
   - The Apply button will become enabled

3. **Apply Changes**
   - Click the **Apply** button to activate your changes
   - The workflow will reconfigure dynamically
   - The console output will show the new execution order
   - The button becomes disabled again until you make more changes

4. **Visual Feedback**
   - **Yellow asterisk (*)** with "Unsaved changes" appears when modifications are pending
   - **"Active workflow"** at the bottom shows the currently running configuration
   - **Green values** indicate operators are actively processing data
   - **Gray "Waiting..."** means no data has reached that operator yet

#### **Example Workflows**

**Default:** `source → one → two → three → sink`

**After reordering:** `source → three → one → two → sink`

**With operator disabled:** `source → one → three → sink` (two disabled)

**All disabled:** `source → sink` (data bypasses all operators)

### Containerized Deployment

The application includes a Dockerfile for containerized deployment:

```bash
# Build and run in one step
./holohub run dynamic_flow_demo

# Or build and run separately
./holohub build dynamic_flow_demo
./holohub run dynamic_flow_demo
```

**Note:** The GUI requires X11 display access. The HoloHub run script automatically handles X11 forwarding, but if running manually ensure you:
- Set `DISPLAY` environment variable
- Mount X11 socket: `-v /tmp/.X11-unix:/tmp/.X11-unix`
- Allow X11 connections: `xhost +local:docker` (or use more secure methods)

### Exiting the Application

- Click the **X button** on the window to close
- Or press **Ctrl+C** in the terminal

## How It Works

### Architecture

The application uses **dynamic flow control** with router operators to achieve runtime workflow reconfiguration:

1. **Router Operator** - A single router operator manages the flow
2. **Stage Tracking** - Tracks which operator should execute next based on configuration
3. **Circular Flow** - All operators feed back to the router for the next stage
4. **Dynamic Callbacks** - `set_dynamic_flows()` evaluates configuration and routes data accordingly

### Workflow Flow

```
source → router (stage 0) → operator1 → router (stage 1) → operator2 → router (stage 2) → operator3 → router (stage 3) → sink
```

The router reads the shared `WorkflowConfig` state to determine which operator to route to at each stage.

### Key Components

**Operators:**
- `SourceOperator` - Periodic data generator (1 Hz)
- `PassThroughOperator` - Generic operator with input/output
- `SinkOperator` - Final data consumer
- `MainRouterOperator` - Dynamic routing controller
- `ImGuiVisualizer` - GUI operator (30 Hz)

**Shared State:**
- `operator_order` - Current active execution order
- `pending_order` - Staging area for UI changes
- `operator_enabled` - Current enable/disable state
- `pending_enabled` - Staging area for enable/disable changes
- `has_pending_changes` - Tracks if Apply button should be enabled
- `current_stage` - Tracks routing state

## Development

### Project Structure

```
dynamic_flow_demo/
├── CMakeLists.txt           # Build configuration
├── Dockerfile               # Container definition
├── README.md               # This file
├── metadata.json           # Application metadata
├── LICENSE                 # Apache 2.0 license
└── src/
    └── main.cpp           # Complete application source
```

### Extending the Application

**Adding More Operators:**
1. Create operator instances in `compose()`
2. Connect them to the router with `add_flow()`
3. Add routing logic in the `set_dynamic_flows()` callback
4. Update the UI to display the new operators

**Modifying the UI:**
- Edit the `ImGuiVisualizer::compute()` method
- ImGui documentation: https://github.com/ocornut/imgui
- Holoviz documentation: https://docs.nvidia.com/holoscan/sdk-user-guide/visualization.html

## License

This project is licensed under the Apache-2.0 License - see the [LICENSE](LICENSE) file for details.

