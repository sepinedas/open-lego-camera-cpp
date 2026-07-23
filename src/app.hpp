#pragma once

#include <map>
#include <memory>
#include <string>

#include <SDL2/SDL.h>
#include <opencv2/core.hpp>

#include "camera.hpp"
#include "config.hpp"
#include "filters.hpp"
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
    void handleFingerDown(const SDL_TouchFingerEvent& f);
    void handleFingerMotion(const SDL_TouchFingerEvent& f);
    void handleFingerUp(const SDL_TouchFingerEvent& f);
    double fingerSpread() const; // pixel distance between the two active fingers

    // --- overlays ---
    // Scaled bitmap text (SDL2_gfx 8x8 font blown up); centre horizontally on x
    // when `center`. Used for the zoom factor and the gallery timestamp.
    void drawText(int x, int topY, const std::string& s, int scale,
                  SDL_Color c, bool center);
    void drawGalleryButton(const Button& b, Uint8 alpha); // last-shot thumbnail
    void refreshThumbnail();                              // rebuild after a capture
    void dispatch(Action a);

    // --- actions ---
    void capturePhoto();
    void toggleRecording();
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
    FaceFilter faceFilter_;

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

    // Live facial-expression filter (cycled by the smiley button).
    Filter filter_ = Filter::None;
    double filterPhase_ = 0.0;      // free-running counter for tear animation
    Uint32 filterLabelUntil_ = 0;   // show the filter name briefly after a change

    cv::Mat lastFrame_;        // most recent live frame (source for photos)
    cv::Mat galleryMat_;       // decoded image/thumbnail currently shown
    std::string galleryShown_; // path backing galleryMat_

    // Capture flash animation.
    Uint32 flashStart_ = 0;

    // Last-capture thumbnail rendered on the gallery button.
    SDL_Texture* thumbTex_ = nullptr;
    std::string thumbPath_;

    // Pinch-to-zoom and single-finger tap detection.
    struct Finger { float x, y; };
    std::map<SDL_FingerID, Finger> fingers_;
    bool pinching_ = false;
    double pinchStartDist_ = 0.0;
    double pinchStartZoom_ = 1.0;
    Uint32 zoomLabelUntil_ = 0;      // keep the factor visible briefly after a change
    bool tapCandidate_ = false;      // one finger down, not yet a drag/pinch
    SDL_FingerID tapFinger_ = 0;
    float tapStartX_ = 0, tapStartY_ = 0;
    Uint32 tapStartMs_ = 0;
};

} // namespace olc
