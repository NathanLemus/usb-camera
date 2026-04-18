#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <stdio.h>
#include <stdint.h>
#include <chrono>
#include <iomanip>
#include <sstream>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define WIDTH 1920
#define HEIGHT 1080
#define CHANNELS 32

// For buffer to Python
#define BUFFER_SIZE 10
#define FRAME_SIZE (WIDTH * HEIGHT * CHANNELS)

// for buffer to Python
// (not to be confused with buffer used for Audio processing locally)
static uint8_t share_buffer[BUFFER_SIZE][FRAME_SIZE];
static volatile int write_idx = 0;
static volatile int read_idx = 0;

// for buffer to Python
static inline int next(int idx) {
    return (idx + 1) % BUFFER_SIZE;
}

struct Buffer {
    void* start;
    size_t length;
};

// This needs an EXTERN "C" block, as Python's CTYPES
//  has issues finding these methods if this is not 
//  explicitly used.
extern "C" {
    // For buffer to Python
    void write_to_buff(const uint8_t *buffer_frame){
    	int next_write = next(write_idx);

        // drop oldest in the queue
        if (next_write == read_idx) {
            read_idx = next(read_idx);
        }

        memcpy(share_buffer[write_idx], buffer_frame, FRAME_SIZE);

        write_idx = next_write;
    }

    // For buffer to Python
    // This is part of the demo where fake frames are generated.
    // Helpful for passthrough testing of the system without worrying about camera faults.
    int consume(uint8_t *out) {
        if (read_idx == write_idx) {
            return 0; // empty
        }

        memcpy(out, share_buffer[read_idx], FRAME_SIZE);

        read_idx = next(read_idx);
        return 1;
    }
}


// https://www.kernel.org/doc/html/v4.9/media/uapi/v4l/capture.c.html
int xioctl(int fd, int request, void* arg) {
    int ret;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

// get filename with time information for saving frames.
std::string make_filename(const std::string& prefix, const std::string& ext) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
    localtime_r(&time, &tm);

    std::ostringstream oss;
    oss << prefix << "_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << "." << ext;

    return oss.str();
}


int main() {
    // openCapture enabled will capture indefinitely. 
    bool openCapture = 1;
    // fall back on capturing a fixed number of frames.
    int closedCaptureNumFrames = 300;
    // do you want to save frames?
    bool saveFrames = 0;
    int width = WIDTH;
    int height = HEIGHT;
    std::string outFile;
    std::tm tm{};
    std::ostringstream oss;

    // should be this device, but check on system if there's no connection.
    const char* dev = "/dev/video0";
    int fd = open(dev, O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    v4l2_format fmt;
    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    // Check available options:
    // https://stackoverflow.com/questions/22563827/list-available-capture-formats
    // $ v4l2-ctl -d0 --list-formats-ext
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    // try to set the format for the stream
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        return 1;
    }


    // https://www.kernel.org/doc/html/v4.9/media/uapi/v4l/mmap.html#mmap
    // Request buffers
    v4l2_requestbuffers req;
    CLEAR(req);
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return 1;
    }

    std::vector<Buffer> buffers(req.count);

    // Map buffers
    for (size_t i = 0; i < buffers.size(); ++i) {
        v4l2_buffer buf;
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            return 1;
        }

        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length,
                                PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, buf.m.offset);

        if (buffers[i].start == MAP_FAILED) {
            perror("mmap");
            return 1;
        }
    }

    // Queue buffers
    for (size_t i = 0; i < buffers.size(); ++i) {
        v4l2_buffer buf;
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return 1;
        }
    }

    // Stream
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return 1;
    }

    // empty unless we want to save frames, then change stream params
    std::ofstream outfile;
    if (saveFrames){
        outFile = make_filename("output_","mjpg");
    }
    pollfd pfd = {fd, POLLIN, 0};

    for (int frame_count = 0; frame_count * !openCapture < closedCaptureNumFrames; ++frame_count) {
        // Wait for frame
        // 1000 ms / 30 fps = 33.33 ms / frame -> 35 ms
        int r = poll(&pfd, 1, 35);
        if (r <= 0) {
            std::cerr << "poll timeout\n";
            continue;
        }

        if (saveFrames){
            outFile = make_filename("output_","mjpg");
            outfile = std::ofstream(outFile, std::ios::binary);
        }

        // Dequeue buffer
        v4l2_buffer buf;
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            perror("VIDIOC_DQBUF");
            return 1;
        }

        // Write MJPEG frame
        if (saveFrames){
            outfile.write((char*)buffers[buf.index].start, buf.bytesused);
            outfile.close();
            std::cout << "Saved MJPEG stream to: " << outFile << "\n";
            outFile.clear();
        }

        write_to_buff((uint8_t*)buffers[buf.index].start);

        // Requeue buffer
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return 1;
        }
    }

    // Stop streaming
    if (xioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("VIDIOC_STREAMOFF");
    }

    // Cleanup buffers
    for (auto& b : buffers) {
        munmap(b.start, b.length);
    }

    close(fd);

    return 0;
}
