// Visual check for the dog filter's 3D assets and shading, without a camera or
// landmark model. It hand-places six frontal pose-landmarks, recovers the head
// pose with the real solvePnP path (DogFilter::estimatePose), then renders the
// assets at that pose and at yaw-rotated variants to confirm they behave as 3D.
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include "../src/dogfilter.hpp"
using namespace olc;

static cv::Matx33d euler(double xd, double yd, double zd) {
    double x = xd * CV_PI / 180, y = yd * CV_PI / 180, z = zd * CV_PI / 180;
    cv::Matx33d Rx(1, 0, 0, 0, cos(x), -sin(x), 0, sin(x), cos(x));
    cv::Matx33d Ry(cos(y), 0, sin(y), 0, 1, 0, -sin(y), 0, cos(y));
    cv::Matx33d Rz(cos(z), -sin(z), 0, sin(z), cos(z), 0, 0, 0, 1);
    return Rz * Ry * Rx;
}

// A simple frontal face sketch plus the six landmarks used for pose.
static cv::Mat faceSketch(const std::vector<cv::Point2f>& lm, int W, int H) {
    cv::Mat img(H, W, CV_8UC3, cv::Scalar(60, 70, 80));
    cv::ellipse(img, cv::Point(W / 2, H / 2 + 10), cv::Size(150, 195), 0, 0, 360,
                cv::Scalar(150, 175, 205), -1); // skin
    cv::circle(img, lm[36], 10, cv::Scalar(240, 240, 240), -1);
    cv::circle(img, lm[45], 10, cv::Scalar(240, 240, 240), -1);
    cv::circle(img, lm[36], 4, cv::Scalar(40, 40, 40), -1);
    cv::circle(img, lm[45], 4, cv::Scalar(40, 40, 40), -1);
    cv::line(img, lm[48], lm[54], cv::Scalar(90, 90, 170), 3); // mouth
    return img;
}

int main() {
    const int W = 480, H = 480;
    // Frontal pose landmarks (only the six pose indices need to be valid).
    std::vector<cv::Point2f> lm(68, cv::Point2f(0, 0));
    lm[30] = {240, 250}; // nose tip
    lm[8] = {240, 400};  // chin
    lm[36] = {180, 200}; // left eye, left corner
    lm[45] = {300, 200}; // right eye, right corner
    lm[48] = {200, 320}; // left mouth corner
    lm[54] = {280, 320}; // right mouth corner

    cv::Matx33d R0, K;
    cv::Vec3d t0;
    if (!DogFilter::estimatePose(lm, cv::Size(W, H), R0, t0, K)) {
        fprintf(stderr, "estimatePose failed\n");
        return 1;
    }

    DogFilter dog;
    struct Shot { const char* label; double yaw; double open; };
    Shot shots[] = {{"frontal", 0, 0.0},
                    {"mouth open", 0, 1.0},
                    {"yaw -30", -30, 0.3},
                    {"yaw +30", 30, 0.3}};

    std::vector<cv::Mat> tiles;
    for (const auto& s : shots) {
        cv::Mat img = faceSketch(lm, W, H);
        cv::Matx33d R = R0 * euler(0, s.yaw, 0); // rotate about the head's Y axis
        dog.render(img, R, t0, K, s.open);
        cv::putText(img, s.label, {12, 30}, cv::FONT_HERSHEY_SIMPLEX, 0.7,
                    {255, 255, 255}, 2);
        tiles.push_back(img);
    }

    cv::Mat top, bot, sheet;
    cv::hconcat(tiles[0], tiles[1], top);
    cv::hconcat(tiles[2], tiles[3], bot);
    cv::vconcat(top, bot, sheet);
    cv::imwrite("/home/user/open-lego-camera-cpp/build/filter-mockup.png", sheet);
    return 0;
}
