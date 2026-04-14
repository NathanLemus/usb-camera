# usb-camera
USB camera tools

## setup
Install dependencies
``` bash
sudo apt-get install aubio-tools libaubio-dev libaubio-doc libportaudio19-dev
sudo apt-get install ffmpeg x264 libx264-dev
```

Make the file
``` bash
# Use this to compile a .so file for using with Python (via ctypes.CDLL)
gcc -Wall -Wextra -I User_Detection_041326.h User_Detection_041326.c -shared -o User_Detection.so -fPIC -lportaudio -laubio -lm

# Use this for running just the c code.
gcc -Wall -Wextra -I User_Detection_041326.h User_Detection_041326.c -o User_Detection -lportaudio -laubio -lm

# The old version:
gcc -o test_audio PortAudio_and_AubioMFCC_260407.c -lportaudio -laubio
```

For running v4l2_camera.cpp
```
cmake .
make
./test_v4l2_cam
ffplay output.mjpg
```