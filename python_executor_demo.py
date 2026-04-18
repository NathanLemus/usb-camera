"""
This code is used to read trigger signals from C code.
Specifically, this reads a boolean generated from the User_Detection_041326.c file.
The boolean whether a user is detected is saved to a circular buffer and read in Python via
    the ctypes.CDLL call to read the C file's generated .so file.
This code is meant to bridge the C and Python implementation, as the trigger signals are needed
    in Python to continue with the rest of the system's flow.
"""
import ctypes
import threading
import time
import datetime

# buffer for passing data out of C and C++ code.
BUFFER_SIZE = 8
FRAME_SIZE = 32

# using a global signal in Python to determine what the C is doing.
logic = 0


# generate local lib out of .so file
lib = ctypes.CDLL("./User_Detection.so")

# set the I/O for functions we're calling.
lib.consume.argtypes = (ctypes.POINTER(ctypes.c_uint8),)
lib.consume.restype = ctypes.c_int


# generate signals (run main in C)
def producer_loop():
    while True:
        lib.main()
        #time.sleep(0.01)


# read signals which have been set up for buffering in C
def consumer_loop():
    buf = (ctypes.c_uint8 * FRAME_SIZE)()
    # logic signal to determine what C is doing.
    global logic

    # read buffer. Can signal this based on Python logic if necessary.
    while True:
        if lib.consume(buf):
            # process data here (images / signals)
            logic = buf[0]
        else:
            time.sleep(0.001)


# run threads
t_prod = threading.Thread(target=producer_loop, daemon=True)
t_cons = threading.Thread(target=consumer_loop, daemon=True)
t_prod.start()
t_cons.start()

# perform logic here for interpreting the signals.
while True:
    if logic == 1:
        print("True", datetime.datetime.now())
    else:
        pass


# not used for this demo.
print("Done")
