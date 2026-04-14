import ctypes
import threading
import time

print("Start")

lib = ctypes.CDLL("./User_Detection.so")

c_thread = threading.Thread(target=lib.main)
c_thread.start()

# for demo purposes.
# this is the other logic to run in Python
time.sleep(10)

lib.stop()

c_thread.join()

print("Done")
