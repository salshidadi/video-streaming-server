# UDP Video and Audio Streaming Application

This is a C-based server and client application for capturing and streaming live video and audio over UDP.

## Features

* **Video Capture and Encoding:** The server captures video from a V4L2 device (`/dev/video0`) at a 640x480 resolution. It converts the raw YUYV frames to YUV420P and encodes them into a low-latency H.264 format using FFmpeg (`libavcodec` and `libswscale`).
* **Audio Capture:** The server forks a separate process to capture audio using ALSA (16-bit, mono, 44100Hz).
* **Dual-Stream Transmission:** Encoded video packets are chunked (up to 60,000 bytes) and transmitted over the main UDP connection. Audio is sent simultaneously to the client on port `1235`.
* **Client Playback:** The client leverages `ffplay` to display the streams. It spawns a background process to catch the UDP audio stream and uses a pipe to feed the incoming H.264 video packets directly to a second `ffplay` instance.
* **Graceful Exit:** The client is equipped with a signal handler for `SIGINT` to safely terminate the spawned `ffplay` processes when shutting down.

## Prerequisites

* **ALSA** (Advanced Linux Sound Architecture) for capturing microphone input.
* **FFmpeg Libraries** (`libavcodec`, `libswscale`, `libavutil`) for handling video encoding and pixel format conversion.
* **V4L2** (Video4Linux2) supported webcam.
* `ffplay` (part of the FFmpeg suite) installed on the client machine for media playback.

## Compilation

Open your terminal and compile the two source files. Ensure you link the necessary multimedia libraries when compiling the server:

```bash
# Compile the server
gcc server.c -o server -lasound -lavcodec -lswscale -lavutil

# Compile the client
gcc client.c -o client
```

## Usage

1. **Start the Server:**
   Run the server executable first. It will bind to UDP port `8080` and wait for an incoming client connection.
   ```bash
   ./server
   ```

2. **Start the Client:**
   Run the client executable. It will connect to the server at `127.0.0.1` on port `8080` and send a "hello" message to trigger the media stream.
   ```bash
   ./client
   ```

3. **Stopping the Stream:**
   Press `Ctrl+C` on the client terminal. The client will catch the interrupt signal, clean up the running `ffplay` processes using `killall`, and shut down.
