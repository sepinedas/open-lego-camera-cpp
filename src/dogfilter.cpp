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
const cv::Vec3b kEar{55, 80, 120};      // brown (outer ear)
const cv::Vec3b kEarFold{42, 62, 98};   // darker brown (folded-over tip)
const cv::Vec3b kMuzzle{175, 205, 230}; // light tan
const cv::Vec3b kNose{35, 32, 38};      // near-black
const cv::Vec3b kTongue{110, 110, 235}; // pink-red
const cv::Vec3b kWhisker{232, 236, 240};// off-white

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

    // Folded ears: an upright base segment near the top of the head, plus a tip
    // segment that folds forward and down over it (the classic folded-ear look).
    // Right (+X) then left (-X).
    parts_.push_back({makeEllipsoid(58, 92, 40, kEar),
                      euler(6, 0, 26), cv::Vec3d(248, 300, -10)});     // base
    parts_.push_back({makeEllipsoid(54, 86, 34, kEarFold),
                      euler(64, 0, 18), cv::Vec3d(250, 250, 78)});     // folded tip
    parts_.push_back({makeEllipsoid(58, 92, 40, kEar),
                      euler(6, 0, -26), cv::Vec3d(-248, 300, -10)});
    parts_.push_back({makeEllipsoid(54, 86, 34, kEarFold),
                      euler(64, 0, -18), cv::Vec3d(-250, 250, 78)});

    // Whiskers: thin off-white spindles fanning out from each side of the muzzle.
    Mesh whisker = makeEllipsoid(98, 4.5f, 4.5f, kWhisker, 6, 8);
    for (int side : {-1, 1}) {
        for (int k = -1; k <= 1; ++k) {
            double fan = k * 15.0;                          // up/down spread
            cv::Matx33d rot = euler(0, -side * 24.0, side * fan); // tip forward
            cv::Vec3d off(side * 96.0, -66.0 - k * 4.0, 150.0);
            parts_.push_back({whisker, rot, off});
        }
    }

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

// Blend the pose toward the previous frame's, except on a large jump (which
// means we just re-acquired the face somewhere new).
void DogFilter::smoothPose(cv::Vec3d& rvec, cv::Vec3d& tvec) {
    if (!hasPose_) {
        prevRvec_ = rvec;
        prevTvec_ = tvec;
        hasPose_ = true;
        return;
    }
    double tref = std::max(1.0, cv::norm(prevTvec_));
    bool jump = cv::norm(tvec - prevTvec_) > 0.4 * tref ||
                cv::norm(rvec - prevRvec_) > 0.6; // ~34 degrees
    if (!jump) {
        const double aR = 0.35, aT = 0.4; // new-sample weight (lower = steadier)
        rvec = aR * rvec + (1.0 - aR) * prevRvec_;
        tvec = aT * tvec + (1.0 - aT) * prevTvec_;
    }
    prevRvec_ = rvec;
    prevTvec_ = tvec;
}

bool DogFilter::apply(cv::Mat& frame, const std::vector<cv::Point2f>& landmarks) {
    cv::Matx33d R, K;
    cv::Vec3d t;
    if (!estimatePose(landmarks, frame.size(), R, t, K)) return false;

    // Smooth the pose as well as the landmarks, for a steadier filter.
    cv::Vec3d rvec;
    cv::Mat rm;
    cv::Rodrigues(cv::Mat(R), rm);
    rvec = cv::Vec3d(rm.ptr<double>()[0], rm.ptr<double>()[1], rm.ptr<double>()[2]);
    smoothPose(rvec, t);
    cv::Rodrigues(rvec, rm);
    R = cv::Matx33d((double*)rm.ptr<double>());

    render(frame, R, t, K, mouthOpen(landmarks));
    return true;
}

} // namespace olc
