# usb-camera
USB camera tools

## setup
Install dependencies
``` bash
sudo apt-get install aubio-tools libaubio-dev libaubio-doc libportaudio19-dev
```

Make the file
``` bash
gcc -o test_audio PortAudio_and_AubioMFCC_260330.c -lportaudio -laubio
```
