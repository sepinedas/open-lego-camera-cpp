#include "facetracker.hpp"

#include <cstdlib>
#include <iostream>
#include <sys/stat.h>

#include <opencv2/imgproc.hpp>

namespace olc {

namespace {
constexpr int kDetectWidth = 320; // downscale width for detection
bool fileExists(const std::string& p) {
    struct stat st{};
    return !p.empty() && ::stat(p.c_str(), &st) == 0;
}
} // namespace

std::vector<std::string> FaceTracker::defaultCascadePaths() {
    return {
        "/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml",
        "/usr/local/share/opencv4/haarcascades/haarcascade_frontalface_default.xml",
        "models/haarcascade_frontalface_default.xml",
    };
}

std::vector<std::string> FaceTracker::defaultModelPaths() {
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : ".";
    return {
        "models/lbfmodel.yaml",
        home + "/.local/share/open-lego-camera/lbfmodel.yaml",
        "/usr/share/open-lego-camera/lbfmodel.yaml",
    };
}

std::string FaceTracker::firstExisting(const std::vector<std::string>& paths) {
    for (const auto& p : paths)
        if (fileExists(p)) return p;
    return "";
}

bool FaceTracker::init(const std::string& cascadePath,
                       const std::string& lbfModelPath) {
    std::string cp = fileExists(cascadePath) ? cascadePath
                                             : firstExisting(defaultCascadePaths());
    std::string mp = fileExists(lbfModelPath) ? lbfModelPath
                                              : firstExisting(defaultModelPaths());
    if (cp.empty()) {
        std::cerr << "filter: no Haar cascade found (install libopencv-dev data "
                     "or pass --cascade)\n";
        return false;
    }
    if (mp.empty()) {
        std::cerr << "filter: no landmark model found. Fetch it with "
                     "scripts/get-models.sh or pass --face-model.\n";
        return false;
    }
    if (!cascade_.load(cp)) {
        std::cerr << "filter: failed to load cascade " << cp << "\n";
        return false;
    }
    try {
        facemark_ = cv::face::FacemarkLBF::create();
        facemark_->loadModel(mp);
    } catch (const cv::Exception& e) {
        std::cerr << "filter: failed to load landmark model " << mp << ": "
                  << e.what() << "\n";
        return false;
    }
    std::cout << "filter: face model ready (" << mp << ")\n";
    available_ = true;
    return true;
}

bool FaceTracker::track(const cv::Mat& bgr, std::vector<cv::Point2f>& landmarks) {
    if (!available_ || bgr.empty()) return false;

    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

    // Detect on a downscaled copy, then map boxes back to full resolution.
    double scale = (double)kDetectWidth / std::max(1, gray.cols);
    if (scale > 1.0) scale = 1.0;
    cv::Mat small;
    cv::resize(gray, small, cv::Size(), scale, scale, cv::INTER_AREA);
    cv::equalizeHist(small, small);

    std::vector<cv::Rect> faces;
    cascade_.detectMultiScale(small, faces, 1.2, 3, 0, cv::Size(40, 40));

    if (!faces.empty()) {
        // Keep the largest face and scale its box back to full resolution.
        cv::Rect best = faces[0];
        for (const auto& f : faces)
            if (f.area() > best.area()) best = f;
        double inv = 1.0 / scale;
        std::vector<cv::Rect> full{cv::Rect((int)(best.x * inv), (int)(best.y * inv),
                                            (int)(best.width * inv),
                                            (int)(best.height * inv))};
        std::vector<std::vector<cv::Point2f>> shapes;
        if (facemark_->fit(gray, full, shapes) && !shapes.empty()) {
            last_ = shapes[0];
            missed_ = 0;
            landmarks = last_;
            return true;
        }
    }

    // No detection this frame: reuse the last landmarks briefly to avoid flicker.
    if (!last_.empty() && missed_ < kKeepFrames) {
        ++missed_;
        landmarks = last_;
        return true;
    }
    return false;
}

} // namespace olc
