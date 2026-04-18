"""
This code is used to read trigger signals from C++ code.
Specifically, this reads an image frame generated from the v4l2_camera.cpp file.
The image frame is saved to a circular buffer and read in Python via
    the ctypes.CDLL call to read the C++ file's generated .so file.
This code is meant to bridge the C and Python implementation, as the image frames are needed
    in Python to continue with the rest of the system's flow.
"""
import ctypes
import threading
import time
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.image as mpimg

lib = ctypes.CDLL("./libv4l2_camera.so")

WIDTH = 1920
HEIGHT = 1080
CHANNELS = 32
FRAME_SIZE = WIDTH * HEIGHT * CHANNELS

lib.consume.argtypes = (ctypes.POINTER(ctypes.c_uint8),)
lib.consume.restype = ctypes.c_int

def producer_loop():
    lib.main()
    ## simulate frames
    #frame = np.zeros((HEIGHT, WIDTH, CHANNELS), dtype=np.uint8)
    #while True:
    #    frame[:] = (frame[0,0,0] + 1) % 255  # change content
    #    lib.write_to_buff(frame.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8)))
    #    time.sleep(0.01)


def consumer_loop():
    buf = (ctypes.c_uint8 * FRAME_SIZE)()

    while True:
        if lib.consume(buf):
            frame = np.frombuffer(buf, dtype=np.uint8)
            frame = frame.reshape((HEIGHT, WIDTH, CHANNELS))
            img = mpimg.imread(frame)
            imgplot = plt.imshow(img)
            plt.show()

            print("Frame mean:", frame.mean())
        else:
            time.sleep(0.005)


t1 = threading.Thread(target=producer_loop, daemon=True)
t2 = threading.Thread(target=consumer_loop, daemon=True)
t1.start()
t2.start()

# This is meant to be a placeholder for other logic that the Python systems use.
# Alternatively, image processing could occur here.
while True:
    pass

# this is not used for this demo
time.sleep(2)
