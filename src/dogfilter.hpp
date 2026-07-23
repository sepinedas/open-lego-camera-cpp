#pragma once

#include <vector>

#include <opencv2/core.hpp>

#include "mesh3d.hpp"

namespace olc {

// A WhatsApp/Snapchat-style dog filter built from smooth 3D meshes (floppy ears,
// a protruding muzzle, a black nose and a lolling tongue). Each asset is defined
// in a face-local coordinate frame; the head pose recovered from the landmarks
// via solvePnP maps them into camera space so they track head turns in 3D. The
// tongue drops out as the mouth opens.
class DogFilter {
public:
    DogFilter();

    // Composite the dog assets onto `frame` (BGR) for the given 68 landmarks.
    // Returns false if the head pose could not be estimated (assets not drawn).
    bool apply(cv::Mat& frame, const std::vector<cv::Point2f>& landmarks);

    // Render the assets at an explicit head pose (camera-space R|t, intrinsics
    // K) with the mouth `open` in [0,1]. Exposed so the offscreen mockup can
    // exercise the exact rendering path without a camera or landmark model.
    void render(cv::Mat& frame, const cv::Matx33d& R, const cv::Vec3d& t,
                const cv::Matx33d& K, double open);

    // Head pose from the six pose landmarks (nose/chin/eye-corners/mouth-corners)
    // via solvePnP. Public so the mockup can reuse it. Returns false on failure.
    static bool estimatePose(const std::vector<cv::Point2f>& lm, cv::Size img,
                             cv::Matx33d& R, cv::Vec3d& t, cv::Matx33d& K);

private:
    // One rigid asset placed in the face-local frame.
    struct Part {
        Mesh mesh;
        cv::Matx33d rot;   // local orientation
        cv::Vec3d offset;  // local position
    };

    static double mouthOpen(const std::vector<cv::Point2f>& lm);

    std::vector<Part> parts_;
    size_t tongueIdx_ = 0; // parts_ index of the tongue (animated per frame)
    cv::Vec3d tongueBase_;
    cv::Mat zbuf_;
    Light light_;
};

} // namespace olc
