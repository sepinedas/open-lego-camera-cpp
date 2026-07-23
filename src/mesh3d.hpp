#pragma once

#include <vector>

#include <opencv2/core.hpp>

namespace olc {

// A minimal smooth-shaded 3D mesh and a tiny software rasterizer. Enough to
// composite a few hundred lit triangles onto a camera frame in real time on a
// Pi -- no OpenGL context needed, so it slots straight into the SDL/OpenCV
// pipeline. Meshes carry per-vertex normals so shading is smooth (Gouraud),
// which is what makes the dog assets read as rounded 3D rather than flat
// sprites.
struct Mesh {
    std::vector<cv::Point3f> verts;   // model-space positions
    std::vector<cv::Point3f> normals; // per-vertex unit normals
    std::vector<cv::Vec3i> tris;      // vertex-index triples
    cv::Vec3b color{200, 200, 200};   // BGR base colour
};

// Directional light for Lambert shading (all in camera space).
struct Light {
    cv::Point3f dir{0.3f, -0.6f, -0.9f}; // toward the light
    double ambient = 0.45;
    double diffuse = 0.65;
};

// Procedural primitives (smooth, with correct normals).
Mesh makeEllipsoid(float rx, float ry, float rz, cv::Vec3b color,
                   int stacks = 16, int slices = 24);

// Rasterise `m` (transformed by camera-space rotation R and translation t,
// projected with intrinsics K) onto `bgr`, using and updating `zbuf` (CV_32F,
// same size, smaller = nearer). Call with the same zbuf across several meshes so
// they occlude each other correctly; reset the zbuf once per frame first.
void renderMesh(cv::Mat& bgr, cv::Mat& zbuf, const Mesh& m,
                const cv::Matx33d& R, const cv::Vec3d& t,
                const cv::Matx33d& K, const Light& light);

} // namespace olc
