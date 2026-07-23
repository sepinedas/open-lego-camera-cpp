#include "dogfilter.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <opencv2/calib3d.hpp>

namespace olc {

namespace {

// Rotation from Euler angles (degrees), applied X then Y then Z.
cv::Matx33d euler(double xd, double yd, double zd) {
    double x = xd * CV_PI / 180, y = yd * CV_PI / 180, z = zd * CV_PI / 180;
    cv::Matx33d Rx(1, 0, 0, 0, std::cos(x), -std::sin(x), 0, std::sin(x), std::cos(x));
    cv::Matx33d Ry(std::cos(y), 0, std::sin(y), 0, 1, 0, -std::sin(y), 0, std::cos(y));
    cv::Matx33d Rz(std::cos(z), -std::sin(z), 0, std::sin(z), std::cos(z), 0, 0, 0, 1);
    return Rz * Ry * Rx;
}

// BGR colours.
const cv::Vec3b kEar{55, 80, 120};      // brown
const cv::Vec3b kMuzzle{175, 205, 230}; // light tan
const cv::Vec3b kNose{35, 32, 38};      // near-black
const cv::Vec3b kTongue{110, 110, 235}; // pink-red

} // namespace

DogFilter::DogFilter() {
    // Face-local frame (same units/orientation as the solvePnP model below):
    // +X right, +Y up, +Z out of the face toward the camera. Eyes sit near
    // y=+170, x=+/-225; the face is ~450 wide.

    // Muzzle: a rounded snout protruding forward over the nose/mouth.
    parts_.push_back({makeEllipsoid(105, 90, 118, kMuzzle),
                      euler(0, 0, 0), cv::Vec3d(0, -75, 100)});

    // Nose: dark blob on the front tip of the muzzle.
    parts_.push_back({makeEllipsoid(56, 47, 47, kNose),
                      euler(0, 0, 0), cv::Vec3d(0, -45, 205)});

    // Ears: floppy, elongated, hanging down the sides of the head, splayed out.
    // Anchored high (near the top of the head) and tilted outward.
    parts_.push_back({makeEllipsoid(66, 175, 44, kEar),
                      euler(14, 0, 24), cv::Vec3d(-250, 300, -20)});
    parts_.push_back({makeEllipsoid(66, 175, 44, kEar),
                      euler(14, 0, -24), cv::Vec3d(250, 300, -20)});

    // Tongue: pink, flattened, hanging below the muzzle; extends when mouth opens.
    tongueBase_ = cv::Vec3d(0, -205, 150);
    parts_.push_back({makeEllipsoid(46, 108, 20, kTongue),
                      euler(18, 0, 0), tongueBase_});
    tongueIdx_ = parts_.size() - 1;
}

double DogFilter::mouthOpen(const std::vector<cv::Point2f>& lm) {
    if (lm.size() < 68) return 0.0;
    double gap = cv::norm(lm[66] - lm[62]);       // inner top<->bottom lip
    double eye = cv::norm(lm[45] - lm[36]);        // outer eye span (scale ref)
    if (eye < 1e-3) return 0.0;
    double r = gap / eye;                          // ~0 closed, ~0.4 wide open
    return std::max(0.0, std::min(1.0, (r - 0.06) / 0.30));
}

bool DogFilter::estimatePose(const std::vector<cv::Point2f>& lm, cv::Size img,
                             cv::Matx33d& R, cv::Vec3d& t, cv::Matx33d& K) {
    if (lm.size() < 68) return false;

    // Canonical 3D face model (arbitrary but self-consistent units), matching
    // the six landmarks below. +Y up, +Z toward the front of the face.
    std::vector<cv::Point3f> obj = {
        {0.0f, 0.0f, 0.0f},        // 30 nose tip
        {0.0f, -330.0f, -65.0f},   // 8  chin
        {-225.0f, 170.0f, -135.0f},// 36 left eye, left corner
        {225.0f, 170.0f, -135.0f}, // 45 right eye, right corner
        {-150.0f, -150.0f, -125.0f},// 48 left mouth corner
        {150.0f, -150.0f, -125.0f} // 54 right mouth corner
    };
    std::vector<cv::Point2f> imgPts = {lm[30], lm[8],  lm[36],
                                       lm[45], lm[48], lm[54]};

    double f = std::max(img.width, img.height);
    K = cv::Matx33d(f, 0, img.width / 2.0, 0, f, img.height / 2.0, 0, 0, 1);
    cv::Mat dist = cv::Mat::zeros(4, 1, CV_64F);

    cv::Mat rvec, tvec;
    if (!cv::solvePnP(obj, imgPts, cv::Mat(K), dist, rvec, tvec, false,
                      cv::SOLVEPNP_ITERATIVE))
        return false;

    cv::Mat Rm;
    cv::Rodrigues(rvec, Rm);
    R = cv::Matx33d((double*)Rm.ptr<double>());
    t = cv::Vec3d(tvec.ptr<double>()[0], tvec.ptr<double>()[1],
                  tvec.ptr<double>()[2]);
    return true;
}

void DogFilter::render(cv::Mat& frame, const cv::Matx33d& R, const cv::Vec3d& t,
                       const cv::Matx33d& K, double open) {
    // Animate the tongue: drop and lengthen it as the mouth opens.
    open = std::max(0.0, std::min(1.0, open));
    parts_[tongueIdx_].offset =
        tongueBase_ + cv::Vec3d(0, -95.0 * open, 25.0 * open);

    // One shared depth buffer so the assets occlude each other correctly.
    if (zbuf_.size() != frame.size())
        zbuf_.create(frame.size(), CV_32F);
    zbuf_.setTo(FLT_MAX);

    for (const auto& p : parts_) {
        cv::Matx33d Rtot = R * p.rot;
        cv::Vec3d ttot = R * p.offset + t;
        renderMesh(frame, zbuf_, p.mesh, Rtot, ttot, K, light_);
    }
}

bool DogFilter::apply(cv::Mat& frame, const std::vector<cv::Point2f>& landmarks) {
    cv::Matx33d R, K;
    cv::Vec3d t;
    if (!estimatePose(landmarks, frame.size(), R, t, K)) return false;
    render(frame, R, t, K, mouthOpen(landmarks));
    return true;
}

} // namespace olc
