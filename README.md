# nvenc_h265_transparency
Encodes a sequence of YUV frames into HEVC (h.265) with transparency, using Nvidia's nvEncodeApi

## Dependencies
- Nvidia's [Video Codec SDK](https://developer.nvidia.com/nvidia-video-codec-SDK). This requires a Nvidia developer account and manual install of headers and libs
- [cmake](https://cmake.org) 3.14 or later
- [CLI11](https://github.com/CLIUtils/CLI11)
- Nvidia graphics card

## Getting started
This is a command line program that uses cmake to build

```
git clone https://github.com/johnhe4/nvenc_h265_transparency.git
cd nvenc_h265_transparency
mkdir build
cd build
cmake ..
```

## Prepare input data
Start with any video, say a file named `video.mp4`.

Get the video dimensions (width and height).
There are many ways to determine this, ffprobe is one way:

`ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=s=x:p=0 video.mp4`

Get the number of frames. You can process less than this number but not more:

`ffprobe -v error -select_streams v:0 -show_entries stream=nb_frames -of csv=s=x:p=0 video.mp4`

Get the frame rate (fps) as a ratio (numerator and denominator):

`ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=s=x:p=0 video.mp4`

Now convert the video to a raw sequence of YUV 4:2:0 frames, all packed into one monolithic file.
This file will be uncompressed so a large number of frames will take a lot of space.
FFmpeg is the usual choice for this:

`ffmpeg -i video.mp4 -c:v rawvideo -pix_fmt yuv420p video.yuv`

Finally, we need a transparency mask. This mask must be a grayscale image where a bright value represents more opacity, and dark value represents more transparency.
For this test you can use any grayscale image, but it MUST have the same dimensions as the video.

## Create an h.265 video with transparency
Run the app:

`./nvenc_h265_transparency --yuvFrames <yuv file you created> --height <height> --width <width> --numFrames <number of frames> --fpsn <fps numerator> --fpsd <fps denominator> --mask <mask img>`

## Finalize output data
This will generate a single `.h265` file in the current directory (`video.h265` in this example). 
This file will not play in standard players; it must first be packed into a container such as mp4.
Again, FFmpeg to the rescue:

TODO, incomplete
`ffmpeg -f video.h265 -pix_fmt yuv420p -s:v <width>x<height> -r 25 -i input.yuv -c:v libx264 output.mp4`

Enjoy!
-John
