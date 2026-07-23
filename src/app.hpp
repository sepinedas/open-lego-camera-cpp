#pragma once

#include <memory>
#include <string>

#include <SDL2/SDL.h>
#include <opencv2/core.hpp>

#include <memory>
#include <vector>

#include "camera.hpp"
#include "config.hpp"
#include "dogfilter.hpp"
#include "facetracker.hpp"
#include "gallery.hpp"
#include "recorder.hpp"
#include "types.hpp"
#include "ui.hpp"

namespace olc {

// Owns the SDL window/renderer and drives the whole application: live preview,
// capture, gallery browsing, video playback and delete-confirm, all through the
// translucent auto-hiding icon menu.
class App {
public:
    App() = default;
    ~App();

    // One-time setup: open the display (KMSDRM on a headless Pi) and camera.
    bool init(const Config& cfg);

    // Blocks in the main loop until the user quits. Returns process exit code.
    int run();

private:
    // --- display helpers ---
    bool initDisplay();
    void renderMat(const cv::Mat& mat); // letterboxed blit of a BGR frame
    void beginFrame();                  // target the offscreen (logical) canvas
    void present();                     // blit the canvas to the panel, rotated
    void clear();
    // Map a panel-space pixel to logical (pre-rotation) UI coords.
    void physicalToView(int px, int py, int& vx, int& vy) const;

    // --- input ---
    void pumpEvents();
    void onTap(int x, int y);
    // Map a normalised (0..1) touch point to screen pixels, applying the
    // configured rotate/flip so a rotated panel (e.g. HyperPixel) lines up.
    void mapTouch(float nx, float ny, int& px, int& py) const;
    void dispatch(Action a);

    // --- actions ---
    void capturePhoto();
    void toggleRecording();
    void toggleFilter();
    void ensureFilter();   // lazily load the face model on first use
    void playCurrentVideo();

    // --- per-mode rendering ---
    void renderCamera();
    void renderGallery();
    void ensureGalleryImage();
    std::string timestampName(const char* prefix, const char* ext) const;

    Config cfg_;
    std::unique_ptr<Camera> cam_;
    std::unique_ptr<Gallery> gallery_;
    Recorder recorder_;
    Menu menu_;

    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    SDL_Texture* tex_ = nullptr;        // streaming texture for the camera frame
    SDL_Texture* canvas_ = nullptr;     // offscreen UI target, blitted rotated
    int texW_ = 0, texH_ = 0;
    int screenW_ = 0, screenH_ = 0;     // physical panel size
    int viewW_ = 0, viewH_ = 0;         // logical UI size (swapped for 90/270)
    int rotate_ = 0;                    // UI rotation in degrees clockwise

    Mode mode_ = Mode::Camera;
    bool running_ = true;

    cv::Mat lastFrame_;        // most recent live frame (source for photos)
    cv::Mat galleryMat_;       // decoded image/thumbnail currently shown
    std::string galleryShown_; // path backing galleryMat_

    // Dog face filter (lazily initialised on first enable).
    FaceTracker faceTracker_;
    std::unique_ptr<DogFilter> dogFilter_;
    std::vector<cv::Point2f> landmarks_;
    bool filterOn_ = false;
    bool filterInit_ = false;
    bool haveFace_ = false;
    unsigned frames_ = 0;
    static constexpr unsigned kTrackEvery = 2; // re-track every N frames
};

} // namespace olc
