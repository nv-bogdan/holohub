# Real-Time Face and Text Deidentification

<center> <img src="./docs/video_deid.gif" ></center>

This sample application demonstrates the use of face and text detection models to do real-time video deidentification.
Regions identified to be face or text are blurred out from the final image.

> **_NOTE:_** This application is a demonstration of real-time face and text deidentification and is not meant to be used in critical applications
that has zero error tolerance.  The models used in this sample application have limitations, e.g., in detecting faces and text that are
partially occluded, in low lighting situations, when there is motion blur, etc.

## Models

This application uses TAO PeopleNet model from [NGC](https://catalog.ngc.nvidia.com/orgs/nvidia/teams/tao/models/peoplenet) for detecting faces.
The model is downloaded when building the application.

For text detection, this application uses [EasyOCR](https://github.com/JaidedAI/EasyOCR) python library which uses Character Region Awareness for Text Detection [(CRAFT)](https://github.com/clovaai/CRAFT-pytorch).

## Data

This application downloads a pre-recorded video from [Pexels](https://www.pexels.com/video/young-traveler-walking-in-the-streets-of-milan-5271997/) when the application is built for use with this application.  Please review the [license terms](https://www.pexels.com/license/) from Pexels.

> **_NOTE:_** The user is responsible for checking if the dataset license is fit for the intended purpose.

## Input

This app currently supports the following input options:

1. V4L2 compatible input device (default, see V4L2 Support below)
2. Pre-recorded video (see Video Replayer Support below)

## Output

This app supports two output modes:

1. **Display mode** (default) - Renders output using HolovizOp to a window
2. **Headless WebRTC mode** - Streams output to a web browser via WebRTC

## Run Instructions

### V4L2 Support

This application supports v4l2 compatible devices as input.  To run this application with your v4l2 compatible device,
please plug in your input device and run:
```sh
./holohub run video_deidentification
```

By default, this application expects the input device to be mounted at `/dev/video0`.  If this is not the case, please update
`applications/video_deidentification/video_deidentification.yaml` and set it to use the corresponding input device before
running the application.  You can also override the default input device on the command line by running:
```sh
./holohub run video_deidentification --run-args="--video_device /dev/video0"
```

### Video Replayer Support

If you don't have a v4l2 compatible device plugged in, you may also run this application on a pre-recorded video.
To launch the application using the Video Stream Replayer as the input source, run:

```sh
./holohub run video_deidentification --run-args="--source replayer"
```

### Headless WebRTC Streaming

This application supports headless operation with WebRTC streaming, allowing you to view the deidentified video in a web browser without requiring a display on the server.

#### Prerequisites

The WebRTC feature requires additional Python packages which are included in the application's Dockerfile:
- `aiohttp` - Web server
- `aiortc` - WebRTC implementation

#### Running in Headless Mode

To run the application in headless mode with WebRTC streaming:

```sh
# With video replayer
./holohub run video_deidentification --docker-file applications/video_deidentification/Dockerfile \
    --extra-docker-run-args="-p 8080:8080" \
    --run-args="--headless --source replayer"

# With V4L2 camera
./holohub run video_deidentification --docker-file applications/video_deidentification/Dockerfile \
    --extra-docker-run-args="-p 8080:8080 --device=/dev/video0" \
    --run-args="--headless --source v4l2"
```

Then open a web browser and navigate to `http://localhost:8080` (or `http://<server-ip>:8080` from another machine). Click the **Start** button to begin viewing the stream.

#### WebRTC Command-Line Options

| Option | Default | Description |
|--------|---------|-------------|
| `--headless` | (off) | Enable headless mode with WebRTC streaming |
| `--webrtc-host HOST` | 0.0.0.0 | Web server host address |
| `--webrtc-port PORT` | 8080 | Web server port |
| `--ice-server URL` | (none) | ICE/TURN server config (can be repeated) |
| `--verbose` | (off) | Enable verbose logging |

#### Using a TURN Server

For containerized or NAT environments, you may need a TURN server:

```sh
# Start TURN server (on host machine)
docker run -d --rm --network=host instrumentisto/coturn \
    -n --log-file=stdout \
    --external-ip=$HOST_IP \
    --listening-ip=$HOST_IP \
    --lt-cred-mech --fingerprint \
    --user=admin:admin \
    --realm=default.realm.org

# Run app with TURN server
./holohub run video_deidentification --docker-file applications/video_deidentification/Dockerfile \
    --extra-docker-run-args="-p 8080:8080" \
    --run-args="--headless --ice-server turn:$HOST_IP:3478[admin:admin]"
```

## Known Issues

There is a known issue running this application on IGX w/ iGPU and on Jetson AGX (see [#500](https://github.com/nvidia-holoscan/holohub/issues/500)).
The workaround is to update the device to avoid picking up the libnvv4l2.so library.

```bash
cd /usr/lib/aarch64-linux-gnu/
ls -l libv4l2.so.0.0.999999
sudo rm libv4l2.so.0.0.999999
sudo ln -s libv4l2.so.0.0.0.0  libv4l2.so.0.0.999999
```
