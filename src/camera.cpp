#include "camera.hpp"

#include <algorithm>
#include <iostream>

#include <opencv2/imgproc.hpp>

namespace olc {

namespace {
constexpr double kMaxZoom = 4.0;
constexpr double kZoomStep = 1.25; // multiplicative per tap

// GStreamer pipeline that pulls frames from a Pi camera through libcamera and
// hands OpenCV plain BGR.
//
// The source caps MUST pin a *processed* pixel format (NV12/YUV420/RGBx). If
// they don't, libcamerasrc may negotiate the sensor's native Bayer stream
// (e.g. the IMX500's 2028x1520-SRGGB16/RAW), which videoconvert can't consume,
// and the pipeline fails to start. `format` forces the ISP-processed output.
// `name` optionally selects one camera by its libcamera id when several exist.
std::string picamPipeline(const std::string& format, int w, int h,
                          const std::string& name) {
    std::string src = "libcamerasrc";
    if (!name.empty()) src += " camera-name=\"" + name + "\"";
    return src + " ! video/x-raw,format=" + format +
           ",width=" + std::to_string(w) + ",height=" + std::to_string(h) +
           " ! videoconvert ! video/x-raw,format=BGR"
           " ! appsink drop=true max-buffers=2";
}

// Try one V4L2 index; returns true and leaves `cap` open on success.
bool tryWebcam(cv::VideoCapture& cap, int index, int w, int h) {
    if (!cap.open(index, cv::CAP_V4L2)) return false;
    // Prefer MJPG so the modest Pi Zero USB bus can sustain higher resolutions.
    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, w);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, h);
    cv::Mat probe;
    if (!cap.read(probe) || probe.empty()) {
        cap.release();
        return false;
    }
    return true;
}
} // namespace

std::unique_ptr<Camera> Camera::open(const Config& cfg) {
    std::unique_ptr<Camera> cam(new Camera());

    auto openPi = [&]() -> bool {
        // Different sensors/ISPs expose different processed formats; try the
        // common ones in turn and keep the first that actually delivers a frame.
        static const char* kFormats[] = {"NV12", "YUV420", "RGBx", "BGRx", "RGB"};
        for (const char* fmt : kFormats) {
            std::string pipe = picamPipeline(fmt, cfg.width, cfg.height, cfg.picamName);
            if (!cam->cap_.open(pipe, cv::CAP_GSTREAMER)) continue;
            cv::Mat probe;
            if (cam->cap_.read(probe) && !probe.empty()) {
                cam->desc_ = std::string("Pi camera (libcamera, ") + fmt + ")";
                return true;
            }
            cam->cap_.release();
            std::cerr << "picam: format " << fmt << " did not start; trying next\n";
        }
        std::cerr << "picam: no libcamera format worked. Check `rpicam-hello` "
                     "and `gst-inspect-1.0 libcamerasrc`.\n";
        return false;
    };

    auto openWebcam = [&]() -> bool {
        if (cfg.webcamIndex >= 0) {
            if (!tryWebcam(cam->cap_, cfg.webcamIndex, cfg.width, cfg.height)) return false;
            cam->desc_ = "USB webcam /dev/video" + std::to_string(cfg.webcamIndex);
            return true;
        }
        for (int i = 0; i < 10; ++i) {
            if (tryWebcam(cam->cap_, i, cfg.width, cfg.height)) {
                cam->desc_ = "USB webcam /dev/video" + std::to_string(i);
                return true;
            }
        }
        return false;
    };

    bool ok = false;
    switch (cfg.camera) {
        case CameraKind::PiCam:  ok = openPi(); break;
        case CameraKind::Webcam: ok = openWebcam(); break;
        case CameraKind::Auto:   ok = openPi() || openWebcam(); break;
    }
    if (!ok) {
        std::cerr << "no camera could be opened\n";
        return nullptr;
    }

    // Record the actual negotiated geometry (may differ from the request).
    cam->width_ = static_cast<int>(cam->cap_.get(cv::CAP_PROP_FRAME_WIDTH));
    cam->height_ = static_cast<int>(cam->cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cam->cap_.get(cv::CAP_PROP_FPS);
    cam->fps_ = (fps > 1.0 && fps < 121.0) ? fps : 30.0;
    if (cam->width_ <= 0 || cam->height_ <= 0) {
        cam->width_ = cfg.width;
        cam->height_ = cfg.height;
    }
    return cam;
}

void Camera::zoomIn() { zoom_ = std::min(kMaxZoom, zoom_ * kZoomStep); }
void Camera::zoomOut() { zoom_ = std::max(1.0, zoom_ / kZoomStep); }

// Digital zoom: crop a centred window of size (1/zoom) and scale it back up to
// the full frame. Uniform across both backends so behaviour is identical.
void Camera::applyZoom(cv::Mat& frame) const {
    if (zoom_ <= 1.001 || frame.empty()) return;
    int cw = std::max(2, static_cast<int>(frame.cols / zoom_));
    int ch = std::max(2, static_cast<int>(frame.rows / zoom_));
    int x = (frame.cols - cw) / 2;
    int y = (frame.rows - ch) / 2;
    cv::Mat roi = frame(cv::Rect(x, y, cw, ch));
    cv::Mat zoomed;
    cv::resize(roi, zoomed, frame.size(), 0, 0, cv::INTER_LINEAR);
    frame = zoomed;
}

bool Camera::read(cv::Mat& frame) {
    if (!cap_.read(frame) || frame.empty()) return false;
    applyZoom(frame);
    return true;
}

} // namespace olc
