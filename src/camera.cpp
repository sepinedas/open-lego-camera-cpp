#include "camera.hpp"

#include <algorithm>
#include <iostream>
#include <thread>

#include <opencv2/imgproc.hpp>

namespace olc {

namespace {
constexpr double kMaxZoom = 4.0;
constexpr double kZoomStep = 1.25; // multiplicative per tap

// Number of CPU threads to hand the software colour converter in the fallback
// pipeline. The Pi Zero 2 W is quad-core, so a single-threaded videoconvert
// leaves three cores idle while it becomes the frame-rate bottleneck.
int convertThreads() {
    unsigned n = std::thread::hardware_concurrency();
    return static_cast<int>(n == 0 ? 1 : n);
}

// Fast path: pull NV12 straight from the ISP with *no* colour conversion in the
// pipeline. Combined with CAP_PROP_CONVERT_RGB=0 this hands OpenCV the raw
// planar buffer, which the display then uploads to an NV12 texture so the GPU
// does the YUV->RGB conversion. A short leaky queue decouples capture from the
// consumer and always keeps the freshest frame.
std::string picamPipelineNV12(int w, int h, const std::string& name) {
    std::string src = "libcamerasrc";
    if (!name.empty()) src += " camera-name=\"" + name + "\"";
    return src + " ! video/x-raw,format=NV12" +
           ",width=" + std::to_string(w) + ",height=" + std::to_string(h) +
           " ! queue leaky=downstream max-size-buffers=2"
           " ! appsink drop=true max-buffers=2";
}

// Fallback: ISP-processed format -> BGR via the software converter. Used only
// when the GPU can't take NV12 textures or NV12 capture won't start. The
// converter is spread across all cores (n-threads) and a leaky queue lets
// capture and conversion run on separate threads instead of in lock-step.
//
// The source caps MUST pin a *processed* pixel format (NV12/YUV420/RGBx). If
// they don't, libcamerasrc may negotiate the sensor's native Bayer stream
// (e.g. the IMX500's 2028x1520-SRGGB16/RAW), which videoconvert can't consume,
// and the pipeline fails to start. `format` forces the ISP-processed output.
// `name` optionally selects one camera by its libcamera id when several exist.
std::string picamPipelineBGR(const std::string& format, int w, int h,
                             const std::string& name) {
    std::string src = "libcamerasrc";
    if (!name.empty()) src += " camera-name=\"" + name + "\"";
    return src + " ! video/x-raw,format=" + format +
           ",width=" + std::to_string(w) + ",height=" + std::to_string(h) +
           " ! queue leaky=downstream max-size-buffers=2"
           " ! videoconvert n-threads=" + std::to_string(convertThreads()) +
           " ! video/x-raw,format=BGR"
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

    // Preferred Pi path: raw NV12 delivered to OpenCV untouched, so the GPU can
    // convert it at display time. Ask OpenCV not to auto-convert to BGR; a
    // successful raw grab comes back as a single-channel planar buffer
    // (height*3/2 rows). If the backend ignores that and still hands us BGR
    // (3 channels), NV12 capture isn't available -- fall through to the BGR
    // pipeline below.
    auto openPiNV12 = [&]() -> bool {
        std::string pipe = picamPipelineNV12(cfg.width, cfg.height, cfg.picamName);
        if (!cam->cap_.open(pipe, cv::CAP_GSTREAMER)) return false;
        cam->cap_.set(cv::CAP_PROP_CONVERT_RGB, 0);
        cv::Mat probe;
        if (cam->cap_.read(probe) && !probe.empty() && probe.channels() == 1 &&
            probe.rows % 3 == 0) {
            cam->format_ = PixelFormat::NV12;
            cam->width_ = probe.cols;
            cam->height_ = probe.rows * 2 / 3; // strip the interleaved UV plane
            cam->desc_ = "Pi camera (libcamera, NV12 -> GPU convert)";
            return true;
        }
        cam->cap_.release();
        std::cerr << "picam: raw NV12 capture unavailable; using CPU convert\n";
        return false;
    };

    auto openPiBGR = [&]() -> bool {
        // Different sensors/ISPs expose different processed formats; try the
        // common ones in turn and keep the first that actually delivers a frame.
        static const char* kFormats[] = {"NV12", "YUV420", "RGBx", "BGRx", "RGB"};
        for (const char* fmt : kFormats) {
            std::string pipe = picamPipelineBGR(fmt, cfg.width, cfg.height, cfg.picamName);
            if (!cam->cap_.open(pipe, cv::CAP_GSTREAMER)) continue;
            cv::Mat probe;
            if (cam->cap_.read(probe) && !probe.empty()) {
                cam->format_ = PixelFormat::BGR;
                cam->desc_ = std::string("Pi camera (libcamera, ") + fmt +
                             " -> CPU convert)";
                return true;
            }
            cam->cap_.release();
            std::cerr << "picam: format " << fmt << " did not start; trying next\n";
        }
        std::cerr << "picam: no libcamera format worked. Check `rpicam-hello` "
                     "and `gst-inspect-1.0 libcamerasrc`.\n";
        return false;
    };

    auto openPi = [&]() -> bool { return openPiNV12() || openPiBGR(); };

    auto openWebcam = [&]() -> bool {
        cam->format_ = PixelFormat::BGR;
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

    // For the BGR paths, record the actual negotiated geometry (the NV12 path
    // already set width_/height_ from the raw buffer above).
    if (cam->format_ != PixelFormat::NV12) {
        cam->width_ = static_cast<int>(cam->cap_.get(cv::CAP_PROP_FRAME_WIDTH));
        cam->height_ = static_cast<int>(cam->cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
    }
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
void Camera::setZoom(double z) { zoom_ = std::min(kMaxZoom, std::max(1.0, z)); }
double Camera::maxZoom() { return kMaxZoom; }

// Centred crop for the current zoom, in display pixels. Kept even on all sides
// so it stays valid for NV12's 2x2-subsampled chroma plane.
cv::Rect Camera::zoomSrcRect(int w, int h) const {
    if (zoom_ <= 1.001 || w <= 0 || h <= 0) return cv::Rect(0, 0, w, h);
    int cw = std::max(2, static_cast<int>(w / zoom_)) & ~1;
    int ch = std::max(2, static_cast<int>(h / zoom_)) & ~1;
    int x = ((w - cw) / 2) & ~1;
    int y = ((h - ch) / 2) & ~1;
    if (x + cw > w) x = w - cw;
    if (y + ch > h) y = h - ch;
    return cv::Rect(x, y, cw, ch);
}

// Digital zoom for capture/record/filters: convert to BGR (if needed), crop the
// centred window and scale it back to the full frame. Uniform across backends
// so a saved photo matches the zoomed preview exactly.
cv::Mat Camera::toDisplayBGR(const cv::Mat& native) const {
    cv::Mat bgr;
    if (native.empty()) return bgr;
    if (format_ == PixelFormat::NV12)
        cv::cvtColor(native, bgr, cv::COLOR_YUV2BGR_NV12);
    else
        bgr = native.clone(); // own the pixels: callers (filters) mutate them
    if (zoom_ > 1.001) {
        cv::Rect roi = zoomSrcRect(bgr.cols, bgr.rows);
        cv::Mat zoomed;
        cv::resize(bgr(roi), zoomed, bgr.size(), 0, 0, cv::INTER_LINEAR);
        bgr = zoomed;
    }
    return bgr;
}

bool Camera::read(cv::Mat& frame) {
    return cap_.read(frame) && !frame.empty();
}

} // namespace olc
