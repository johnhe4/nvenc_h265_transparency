# nvenc_h265_transparency
Encodes a sequence of YUV frames into HEVC (h.265) with transparency using NVIDIA's [Video Codec SDK](https://developer.nvidia.com/nvidia-video-codec-SDK)

## Dependencies
*REQUIRED:*
- C++ compiler. I test on g++ but try to keep things cross-platform.
- [cmake](https://cmake.org) 3.14 or later
- [CLI11](https://github.com/CLIUtils/CLI11)
- NVIDIA graphics card with [encode capability](https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new)
- NVIDIA graphics driver installed, functioning and recent
- NVIDIA [CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit), functioning and recent

*HELPFUL:*
 - [FFmpeg](https://ffmpeg.org) for preparing input data and packaging output data. My code has no dependencies on FFmpeg.
 - An image editor for creating the transparency mask. I used [gimp](https://www.gimp.org).

## Getting started
This is a command line program that uses cmake to build

```
git clone https://github.com/johnhe4/nvenc_h265_transparency.git
cd nvenc_h265_transparency
mkdir build
cd build
cmake ..
make
```

## Prepare input data
Start with any video, say a file named `video.mp4`. We also need a grayscale image of the same dimensions as the video, say `image.jpg`.

### Get video metadata
Get the video dimensions (width and height).
There are many ways to determine this, ffprobe is one way:

`ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=s=x:p=0 video.mp4`

Get the frame rate (fps) as a ratio (numerator and denominator):

`ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=s=x:p=0 video.mp4`

### Convert video to raw bytes
Now convert the video to a raw sequence of YUV 4:2:0 frames, all packed into one monolithic file.
This file will be uncompressed so a large number of frames will take a lot of space.
FFmpeg is the usual choice for this:

`ffmpeg -i video.mp4 -c:v rawvideo -pix_fmt nv12 video.yuv`

### Convert mask to raw bytes
We'll use our `image.jpg` for a transparency mask. This mask must be a grayscale image where a bright value represents more opacity, and dark value represents more transparency.
For this test you can use any grayscale image, but it MUST have the same dimensions as the video:

`ffmpeg -i image.jpg -pix_fmt nv12 image.yuv`

## Create an h.265 video with transparency
Now it's time to run the code you built earlier:

`./nvenc_h265_transparency --yuvFrames video.yuv --width <width> --height <height> --fpsn <fps numerator> --fpsd <fps denominator> --mask image.yuv`

It doesn't have anything fancy for output, so unless you see errors you can wait for it to complete.

## Finalize output data
This will generate a single `.h265` file in the current directory. 
This file will not play in standard players; it must first be packed into a container such as mp4.
Again, FFmpeg to the rescue (replace `fpsn` and `fpsd` with appropriate values):

`ffmpeg -i video.h265 -c copy -time_base <fpsn>/<fpsd> <framerate finalVideo.mp4`

Enjoy!

-John
