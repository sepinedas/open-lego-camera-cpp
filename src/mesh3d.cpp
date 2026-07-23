#include "mesh3d.hpp"

#include <algorithm>
#include <cmath>

namespace olc {

namespace {
float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
} // namespace

// UV-parameterised ellipsoid. The analytic normal of an ellipsoid at (x,y,z) is
// (x/rx^2, y/ry^2, z/rz^2) normalised, which gives smooth shading.
Mesh makeEllipsoid(float rx, float ry, float rz, cv::Vec3b color, int stacks,
                   int slices) {
    Mesh m;
    m.color = color;
    for (int i = 0; i <= stacks; ++i) {
        float v = (float)i / stacks;             // 0..1
        float phi = v * (float)CV_PI;            // 0..pi (top to bottom)
        float sp = std::sin(phi), cp = std::cos(phi);
        for (int j = 0; j <= slices; ++j) {
            float u = (float)j / slices;         // 0..1
            float th = u * 2.0f * (float)CV_PI;  // around
            float st = std::sin(th), ct = std::cos(th);
            cv::Point3f p(rx * sp * ct, ry * cp, rz * sp * st);
            cv::Point3f n(p.x / (rx * rx), p.y / (ry * ry), p.z / (rz * rz));
            float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (len > 1e-6f) n *= (1.0f / len);
            m.verts.push_back(p);
            m.normals.push_back(n);
        }
    }
    int cols = slices + 1;
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            int a = i * cols + j;
            int b = a + 1;
            int c = a + cols;
            int d = c + 1;
            m.tris.emplace_back(a, c, b);
            m.tris.emplace_back(b, c, d);
        }
    }
    return m;
}

void renderMesh(cv::Mat& bgr, cv::Mat& zbuf, const Mesh& m,
                const cv::Matx33d& R, const cv::Vec3d& t,
                const cv::Matx33d& K, const Light& light) {
    const int W = bgr.cols, H = bgr.rows;
    const double fx = K(0, 0), fy = K(1, 1), cx = K(0, 2), cy = K(1, 2);

    // Transform vertices to camera space, project, and pre-shade each vertex.
    const size_t n = m.verts.size();
    std::vector<cv::Point3d> cam(n);   // camera-space position
    std::vector<cv::Point2d> scr(n);   // projected pixel
    std::vector<double> shade(n);      // Lambert intensity
    std::vector<bool> ok(n, false);

    cv::Point3f L = light.dir;
    double Ll = std::sqrt(L.x * L.x + L.y * L.y + L.z * L.z);
    if (Ll > 1e-6) L *= (float)(1.0 / Ll);

    for (size_t i = 0; i < n; ++i) {
        cv::Vec3d v(m.verts[i].x, m.verts[i].y, m.verts[i].z);
        cv::Vec3d p = R * v + t;
        if (p[2] < 1e-3) continue; // behind camera
        cam[i] = cv::Point3d(p[0], p[1], p[2]);
        scr[i] = cv::Point2d(fx * p[0] / p[2] + cx, fy * p[1] / p[2] + cy);

        cv::Vec3d nn(m.normals[i].x, m.normals[i].y, m.normals[i].z);
        cv::Vec3d nc = R * nn; // rotate normal into camera space
        double nl = std::sqrt(nc.dot(nc));
        if (nl > 1e-6) nc *= (1.0 / nl);
        double nd = nc[0] * L.x + nc[1] * L.y + nc[2] * L.z;
        shade[i] = light.ambient + light.diffuse * std::max(0.0, nd);
        ok[i] = true;
    }

    for (const auto& tri : m.tris) {
        int i0 = tri[0], i1 = tri[1], i2 = tri[2];
        if (!ok[i0] || !ok[i1] || !ok[i2]) continue;
        const cv::Point2d &A = scr[i0], &B = scr[i1], &C = scr[i2];

        // Screen-space bounding box, clipped to the frame.
        int minX = (int)std::floor(std::min({A.x, B.x, C.x}));
        int maxX = (int)std::ceil(std::max({A.x, B.x, C.x}));
        int minY = (int)std::floor(std::min({A.y, B.y, C.y}));
        int maxY = (int)std::ceil(std::max({A.y, B.y, C.y}));
        minX = std::max(0, minX); minY = std::max(0, minY);
        maxX = std::min(W - 1, maxX); maxY = std::min(H - 1, maxY);
        if (minX > maxX || minY > maxY) continue;

        double area = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
        if (std::fabs(area) < 1e-9) continue;
        double invArea = 1.0 / area;

        double z0 = cam[i0].z, z1 = cam[i1].z, z2 = cam[i2].z;
        for (int y = minY; y <= maxY; ++y) {
            float* zrow = zbuf.ptr<float>(y);
            cv::Vec3b* crow = bgr.ptr<cv::Vec3b>(y);
            for (int x = minX; x <= maxX; ++x) {
                double px = x + 0.5, py = y + 0.5;
                // Barycentric weights via edge functions.
                double w0 = ((B.x - px) * (C.y - py) - (B.y - py) * (C.x - px)) * invArea;
                double w1 = ((C.x - px) * (A.y - py) - (C.y - py) * (A.x - px)) * invArea;
                double w2 = 1.0 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;

                float z = (float)(w0 * z0 + w1 * z1 + w2 * z2);
                if (z >= zrow[x]) continue; // z-test (nearer wins)
                zrow[x] = z;

                double s = w0 * shade[i0] + w1 * shade[i1] + w2 * shade[i2];
                s = clampf((float)s, 0.f, 1.4f);
                crow[x] = cv::Vec3b(
                    (uchar)clampf(m.color[0] * (float)s, 0, 255),
                    (uchar)clampf(m.color[1] * (float)s, 0, 255),
                    (uchar)clampf(m.color[2] * (float)s, 0, 255));
            }
        }
    }
}

} // namespace olc
