#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/face.hpp>

namespace olc {

// Detects a face and fits 68 facial landmarks (the "face mesh" the 3D assets
// are anchored to). Face detection runs on a downscaled grey image for speed;
// landmarks are fit at full resolution. If the face is briefly lost the last
// landmarks are reused for a few frames to reduce flicker.
class FaceTracker {
public:
    // Loads the Haar cascade and the LBF landmark model. Returns false (and
    // available() stays false) if either can't be loaded -- the app then simply
    // runs without the filter.
    bool init(const std::string& cascadePath, const std::string& lbfModelPath);
    bool available() const { return available_; }

    // Fit landmarks on a BGR frame. Returns true and fills `landmarks` (68 pts)
    // when a face is tracked.
    bool track(const cv::Mat& bgr, std::vector<cv::Point2f>& landmarks);

    // Default search locations for the models, in priority order.
    static std::vector<std::string> defaultCascadePaths();
    static std::vector<std::string> defaultModelPaths();
    static std::string firstExisting(const std::vector<std::string>& paths);

private:
    bool available_ = false;
    cv::CascadeClassifier cascade_;
    cv::Ptr<cv::face::Facemark> facemark_;

    std::vector<cv::Point2f> last_;
    int missed_ = 0;
    static constexpr int kKeepFrames = 6; // reuse stale landmarks up to this long
};

} // namespace olc
