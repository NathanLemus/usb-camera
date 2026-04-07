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

#define CLEAR(x) memset(&(x), 0, sizeof(x))

struct Buffer {
    void* start;
    size_t length;
};

// https://www.kernel.org/doc/html/v4.9/media/uapi/v4l/capture.c.html
int xioctl(int fd, int request, void* arg) {
    int ret;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}


int main() {
    // openCapture enabled will capture indefinitely. 
    bool openCapture = 1;
    int closedCaptureNumFrames = 300;
    int width = 1920; //1280
    int height = 1080; //720
    std::string outFile = "output.mjpg";

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


    std::ofstream outfile(outFile, std::ios::binary);
    pollfd pfd = {fd, POLLIN, 0};

    for (int frame_count = 0; frame_count * !openCapture < closedCaptureNumFrames; ++frame_count) {
        // Wait for frame
        // 1000 ms / 30 fps = 33.33 ms / frame
        int r = poll(&pfd, 1, 35);
        if (r <= 0) {
            std::cerr << "poll timeout\n";
            continue;
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
        outfile.write((char*)buffers[buf.index].start, buf.bytesused);

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
    outfile.close();
    std::cout << "Saved MJPEG stream to: " << outFile << "\n";
    return 0;
}
