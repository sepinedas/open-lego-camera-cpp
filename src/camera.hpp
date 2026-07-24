#pragma once

#include <memory>
#include <string>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include "config.hpp"

namespace olc {

// Pixel layout of the frames Camera::read() hands back. NV12 is the format the
// Pi camera ISP produces natively; keeping frames in it lets the GPU do the
// YUV->RGB conversion at display time instead of burning a CPU core on it.
enum class PixelFormat { BGR, NV12 };

// A single camera source (Pi camera module or USB webcam).
//
// Frames come back in the source's *native* layout (see format()): the Pi
// camera delivers NV12 straight from the ISP with no CPU colour conversion,
// while a USB webcam is decoded to BGR by OpenCV. Digital zoom is no longer
// baked into the pixels here -- the preview applies it on the GPU (a source
// rect, see zoomSrcRect()), and capture/record materialise a zoomed BGR frame
// on demand via toDisplayBGR(). This keeps the hot preview path free of any
// per-frame CPU colour-convert or resize.
class Camera {
public:
    // Opens the requested source. On CameraKind::Auto it tries the Pi camera
    // (libcamera via GStreamer) first, then falls back to a V4L2 webcam.
    // Returns nullptr if nothing could be opened.
    static std::unique_ptr<Camera> open(const Config& cfg);

    // Grab the next frame in the source's native format (see format()). No zoom
    // is applied. False if the stream ended.
    bool read(cv::Mat& frame);

    // Pixel layout of frames from read().
    PixelFormat format() const { return format_; }

    // The centred crop rectangle for the current zoom, in display-pixel coords.
    // Used as a GPU source rect for the preview, and to crop captures. Returns
    // the full frame when not zoomed. Coordinates are kept even so the rect is
    // valid for chroma-subsampled (NV12) buffers too.
    cv::Rect zoomSrcRect(int w, int h) const;

    // Materialise a full-size BGR frame from a native frame with the current
    // zoom applied -- for photo capture, recording and the facial filters, all
    // of which need BGR. Not on the preview hot path.
    cv::Mat toDisplayBGR(const cv::Mat& native) const;

    void zoomIn();
    void zoomOut();
    void setZoom(double z);       // absolute zoom, clamped to [1, maxZoom()]
    double zoom() const { return zoom_; }
    bool zoomed() const { return zoom_ > 1.001; }
    static double maxZoom();

    int width() const { return width_; }   // display (luma) width
    int height() const { return height_; }  // display (luma) height
    double fps() const { return fps_; }
    const std::string& description() const { return desc_; }

private:
    Camera() = default;

    cv::VideoCapture cap_;
    PixelFormat format_ = PixelFormat::BGR;
    int width_ = 0;
    int height_ = 0;
    double fps_ = 30.0;
    double zoom_ = 1.0;      // 1.0 .. kMaxZoom
    std::string desc_;
};

} // namespace olc
