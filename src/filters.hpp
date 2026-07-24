#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>

#include "types.hpp"

namespace olc {

// Real-time, WhatsApp-style facial-expression filters.
//
// The face itself is *reshaped* (its pixels are pushed around with cv::remap)
// rather than having cartoon graphics pasted over it -- a "big smile" is your
// own mouth stretched into a grin, a "crying" face is your own mouth and brows
// pulled into a frown. The only thing actually drawn on top of the frame are
// the falling tears of the crying filter.
//
// Faces are found with a stock OpenCV Haar cascade (objdetect); no landmark
// model or contrib module is needed, which keeps it light enough for a Pi Zero.
class FaceFilter {
public:
    // Loads the frontal-face cascade from the usual system locations.
    FaceFilter();

    // Point the detector at an explicit cascade XML (from --face-cascade).
    // Empty is a no-op; a bad path leaves any already-loaded cascade in place.
    void setCascade(const std::string& path);

    // True once a cascade is loaded and filtering can actually do something.
    bool ready() const { return loaded_; }

    // Apply `filter` to `frame` (BGR, 8-bit, 3-channel) in place. `phase` is a
    // free-running per-frame counter that drives the tear animation. A no-op
    // when the filter is None, no cascade loaded, or no face is found.
    void apply(cv::Mat& frame, Filter filter, double phase);

    // --- region-limited API (keeps the NV12 preview off the CPU convert) ---
    //
    // Refresh the detected faces from a luma/grayscale image (the NV12 Y plane
    // is exactly that, so no colour conversion is needed). Honours the
    // detect-every-N-frames cadence internally; call once per frame.
    void updateDetection(const cv::Mat& luma);

    // The single frame-space rectangle covering everything `filter` will modify
    // for the currently-detected faces (face boxes + margin for the warp and
    // tears, clamped to WxH and made even for chroma-subsampled buffers). An
    // empty rect means there is nothing to reshape this frame.
    cv::Rect dirtyRegion(Filter filter, int w, int h) const;

    // Apply `filter` to `roi`, a BGR sub-image whose top-left sits at `origin`
    // in frame space. Only the parts of each face falling inside `roi` are
    // touched, so callers can convert and re-encode just the dirty region.
    void applyRegion(cv::Mat& roi, cv::Point origin, Filter filter, double phase);

private:
    void detectLuma(const cv::Mat& luma);        // refresh faces_ (full-res coords)
    void applySmile(cv::Mat& frame, const cv::Rect& face);
    void applyCry(cv::Mat& frame, const cv::Rect& face, double phase);

    // Rough 0..1 estimate of how open the mouth is, from the contrast of the
    // central mouth patch (an open mouth = dark cavity next to bright teeth).
    float mouthOpenness(const cv::Mat& frame, const cv::Rect& face) const;
    // Brighten the teeth band toward white; stronger the wider the mouth opens.
    void whitenTeeth(cv::Mat& frame, const cv::Rect& face, float open) const;
    // Draw the falling tears of the crying filter.
    void drawTears(cv::Mat& frame, const cv::Rect& face, double phase) const;

    cv::CascadeClassifier face_;
    bool loaded_ = false;
    bool warned_ = false;                // "no cascade" logged only once
    int frameCount_ = 0;                 // detection runs every few frames
    std::vector<cv::Rect> faces_;        // last detection result, full-res
};

// Cycle order for the on-screen filter button: None -> BigSmile -> Crying ->.
Filter nextFilter(Filter f);
// Short human label for the brief on-screen filter name.
const char* filterName(Filter f);

} // namespace olc
