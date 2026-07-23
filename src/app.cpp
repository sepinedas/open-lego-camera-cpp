#include "app.hpp"

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <vector>

#include <SDL2/SDL2_gfxPrimitives.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "icons.hpp"

namespace olc {

App::~App() {
    if (recorder_.recording()) recorder_.stop();
    if (tex_) SDL_DestroyTexture(tex_);
    if (canvas_) SDL_DestroyTexture(canvas_);
    if (ren_) SDL_DestroyRenderer(ren_);
    if (win_) SDL_DestroyWindow(win_);
    SDL_Quit();
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

bool App::initDisplay() {
    // Decide which SDL video drivers to try, in order.
    std::vector<std::string> drivers;
    if (!cfg_.driver.empty()) {
        drivers = {cfg_.driver};
    } else if (std::getenv("DISPLAY") || std::getenv("WAYLAND_DISPLAY")) {
        drivers = {""}; // a desktop session is present: let SDL auto-pick
    } else {
        drivers = {"kmsdrm", "fbcon"}; // headless Pi: draw straight to HDMI
    }

    Uint32 winFlags = cfg_.windowed ? 0u : (Uint32)SDL_WINDOW_FULLSCREEN_DESKTOP;
    int w = cfg_.windowed ? cfg_.width : 0;
    int h = cfg_.windowed ? cfg_.height : 0;

    for (const std::string& drv : drivers) {
        const char* tag = drv.empty() ? "(auto)" : drv.c_str();
        if (drv.empty()) unsetenv("SDL_VIDEODRIVER");
        else setenv("SDL_VIDEODRIVER", drv.c_str(), 1);

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
            std::cerr << "display: driver '" << tag << "' SDL_Init failed: "
                      << SDL_GetError() << "\n";
            continue;
        }

        // Report what KMS/DRM outputs SDL can see -- the key clue when a screen
        // stays black (0 displays = no connected HDMI/DPI connector with a mode).
        int nd = SDL_GetNumVideoDisplays();
        std::cerr << "display: driver '" << tag << "' up; " << nd
                  << " output(s) detected\n";
        for (int i = 0; i < nd; ++i) {
            SDL_Rect b{};
            SDL_GetDisplayBounds(i, &b);
            const char* name = SDL_GetDisplayName(i);
            std::cerr << "  [" << i << "] " << (name ? name : "?") << " "
                      << b.w << "x" << b.h << "\n";
        }

        win_ = SDL_CreateWindow("open-lego-camera", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED,
                                cfg_.windowed ? w : 0, cfg_.windowed ? h : 0,
                                winFlags);
        if (!win_) {
            std::cerr << "display: driver '" << tag << "' CreateWindow failed: "
                      << SDL_GetError() << "\n";
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            SDL_Quit();
            continue;
        }

        ren_ = SDL_CreateRenderer(win_, -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, 0); // software fallback
        if (!ren_) {
            std::cerr << "display: driver '" << tag << "' CreateRenderer failed: "
                      << SDL_GetError() << "\n";
            SDL_DestroyWindow(win_);
            win_ = nullptr;
            SDL_Quit();
            continue;
        }

        SDL_GetRendererOutputSize(ren_, &screenW_, &screenH_);
        SDL_ShowCursor(SDL_DISABLE);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);

        // Set up UI rotation. For 90/270 the logical canvas is the panel with
        // width/height swapped; everything is drawn there and blitted rotated.
        rotate_ = cfg_.rotate;
        if (rotate_ != 0 && !SDL_RenderTargetSupported(ren_)) {
            std::cerr << "display: renderer can't rotate (no target textures); "
                         "drawing unrotated\n";
            rotate_ = 0;
        }
        bool swap = (rotate_ == 90 || rotate_ == 270);
        viewW_ = swap ? screenH_ : screenW_;
        viewH_ = swap ? screenW_ : screenH_;
        if (rotate_ != 0) {
            canvas_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_TARGET, viewW_, viewH_);
            if (!canvas_) {
                std::cerr << "display: canvas alloc failed (" << SDL_GetError()
                          << "); drawing unrotated\n";
                rotate_ = 0;
                viewW_ = screenW_;
                viewH_ = screenH_;
            } else {
                SDL_SetTextureBlendMode(canvas_, SDL_BLENDMODE_NONE);
            }
        }

        const char* used = SDL_GetCurrentVideoDriver();
        std::cout << "display: " << (used ? used : "?") << " " << screenW_
                  << "x" << screenH_;
        if (rotate_) std::cout << " (UI rotated " << rotate_ << ", logical "
                               << viewW_ << "x" << viewH_ << ")";
        std::cout << "\n";
        return true;
    }

    std::cerr << "could not open a display. On a headless Pi run this on the "
                 "active HDMI console (not SSH), or pass --driver. See the "
                 "\"Debugging HDMI / no display\" section in the README.\n";
    return false;
}

void App::clear() {
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
    SDL_RenderClear(ren_);
}

// Point every subsequent draw call at the logical canvas (when rotating).
void App::beginFrame() {
    if (canvas_) SDL_SetRenderTarget(ren_, canvas_);
}

// Finish the frame: blit the logical canvas onto the panel, rotated, and flip.
void App::present() {
    if (canvas_) {
        SDL_SetRenderTarget(ren_, nullptr);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
        SDL_RenderClear(ren_);
        // A dst rect the size of the logical canvas, centred on the panel, then
        // rotated: for 90/270 its bounding box becomes the full panel.
        SDL_Rect dst{(screenW_ - viewW_) / 2, (screenH_ - viewH_) / 2,
                     viewW_, viewH_};
        SDL_RenderCopyEx(ren_, canvas_, nullptr, &dst, (double)rotate_, nullptr,
                         SDL_FLIP_NONE);
    }
    SDL_RenderPresent(ren_);
}

// Undo the display rotation so a panel tap lands on the right UI element.
void App::physicalToView(int px, int py, int& vx, int& vy) const {
    switch (rotate_) {
        case 90:  vx = py;             vy = screenW_ - px; break;
        case 180: vx = screenW_ - px;  vy = screenH_ - py; break;
        case 270: vx = screenH_ - py;  vy = px;            break;
        default:  vx = px;             vy = py;            break;
    }
    vx = std::min(viewW_ - 1, std::max(0, vx));
    vy = std::min(viewH_ - 1, std::max(0, vy));
}

// Blit a BGR cv::Mat to the screen, preserving aspect ratio (letterboxed).
void App::renderMat(const cv::Mat& src) {
    clear();
    if (src.empty()) return;

    cv::Mat bgr;
    if (src.type() == CV_8UC3) bgr = src;
    else if (src.channels() == 4) cv::cvtColor(src, bgr, cv::COLOR_BGRA2BGR);
    else if (src.channels() == 1) cv::cvtColor(src, bgr, cv::COLOR_GRAY2BGR);
    else bgr = src;

    if (!tex_ || texW_ != bgr.cols || texH_ != bgr.rows) {
        if (tex_) SDL_DestroyTexture(tex_);
        tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_BGR24,
                                 SDL_TEXTUREACCESS_STREAMING, bgr.cols, bgr.rows);
        texW_ = bgr.cols;
        texH_ = bgr.rows;
    }
    SDL_UpdateTexture(tex_, nullptr, bgr.data, static_cast<int>(bgr.step));

    // Letterbox into the logical view (which the canvas then rotates).
    double sx = (double)viewW_ / bgr.cols;
    double sy = (double)viewH_ / bgr.rows;
    double s = std::min(sx, sy);
    int dw = (int)(bgr.cols * s), dh = (int)(bgr.rows * s);
    SDL_Rect dst{(viewW_ - dw) / 2, (viewH_ - dh) / 2, dw, dh};
    SDL_RenderCopy(ren_, tex_, nullptr, &dst);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

bool App::init(const Config& cfg) {
    cfg_ = cfg;
    if (!initDisplay()) return false;

    cam_ = Camera::open(cfg_);
    if (!cam_) {
        std::cerr << "startup failed: no camera\n";
        return false;
    }
    std::cout << "camera: " << cam_->description() << " " << cam_->width()
              << "x" << cam_->height() << " @ " << cam_->fps() << "fps\n";

    gallery_ = std::make_unique<Gallery>(cfg_.outputDir);
    menu_.wake();
    return true;
}

std::string App::timestampName(const char* prefix, const char* ext) const {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof(buf), "_%Y%m%d_%H%M%S", &tm);
    return cfg_.outputDir + "/" + prefix + buf + ext;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void App::pumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                running_ = false;
                break;
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) {
                    if (mode_ == Mode::Camera) running_ = false;
                    else mode_ = Mode::Camera; // step back to preview
                } else {
                    menu_.wake();
                }
                break;
            case SDL_MOUSEBUTTONDOWN: {
                // Ignore mouse events SDL synthesises from touch (which ==
                // SDL_TOUCH_MOUSEID); the SDL_FINGERDOWN below handles those,
                // otherwise every tap would fire twice.
                if (e.button.which != SDL_TOUCH_MOUSEID) {
                    int vx, vy;
                    physicalToView(e.button.x, e.button.y, vx, vy);
                    onTap(vx, vy);
                }
                break;
            }
            case SDL_FINGERDOWN: {
                // Touch coords are normalised 0..1 on KMSDRM: correct for any
                // touch-panel misalignment, then undo the display rotation.
                int px, py, vx, vy;
                mapTouch(e.tfinger.x, e.tfinger.y, px, py);
                physicalToView(px, py, vx, vy);
                onTap(vx, vy);
                break;
            }
            default:
                break;
        }
    }
}

void App::mapTouch(float nx, float ny, int& px, int& py) const {
    float x = nx, y = ny;
    switch (cfg_.touchRotate) {          // clockwise, on the unit square
        case 90:  { float t = x; x = 1.f - y; y = t; break; }
        case 180: x = 1.f - x; y = 1.f - y; break;
        case 270: { float t = x; x = y; y = 1.f - t; break; }
        default:  break;
    }
    if (cfg_.touchFlipX) x = 1.f - x;
    if (cfg_.touchFlipY) y = 1.f - y;
    px = std::min(screenW_ - 1, std::max(0, (int)(x * screenW_)));
    py = std::min(screenH_ - 1, std::max(0, (int)(y * screenH_)));
}

void App::onTap(int x, int y) {
    if (mode_ == Mode::ConfirmDelete) {
        auto btns = menu_.layout(mode_, viewW_, viewH_, false);
        Action a = Menu::hitTest(btns, x, y);
        dispatch(a == Action::ConfirmYes ? Action::ConfirmYes : Action::ConfirmNo);
        return;
    }

    // In Camera/Gallery, a tap while the menu is hidden only wakes it.
    bool wasAwake = menu_.awake();
    menu_.wake();
    if (!wasAwake) return;

    bool hasVideo = gallery_ && !gallery_->empty() && gallery_->currentIsVideo();
    auto btns = menu_.layout(mode_, viewW_, viewH_, hasVideo);
    dispatch(Menu::hitTest(btns, x, y));
}

void App::dispatch(Action a) {
    switch (a) {
        case Action::Shutter:     capturePhoto(); break;
        case Action::Record:      toggleRecording(); break;
        case Action::ZoomIn:      cam_->zoomIn(); break;
        case Action::ZoomOut:     cam_->zoomOut(); break;
        case Action::OpenGallery: gallery_->refresh(); mode_ = Mode::Gallery; break;
        case Action::Back:        mode_ = Mode::Camera; break;
        case Action::Prev:        gallery_->prev(); break;
        case Action::Next:        gallery_->next(); break;
        case Action::Play:        playCurrentVideo(); break;
        case Action::Delete:
            if (gallery_ && !gallery_->empty()) mode_ = Mode::ConfirmDelete;
            break;
        case Action::ConfirmYes:
            gallery_->deleteCurrent();
            mode_ = gallery_->empty() ? Mode::Camera : Mode::Gallery;
            break;
        case Action::ConfirmNo:
            mode_ = Mode::Gallery;
            break;
        case Action::Quit:
            running_ = false;
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void App::capturePhoto() {
    if (lastFrame_.empty()) return;
    std::string path = timestampName("IMG", ".jpg");
    cv::imwrite(path, lastFrame_);
    std::cout << "saved " << path << "\n";
}

void App::toggleRecording() {
    if (recorder_.recording()) {
        recorder_.stop();
        std::cout << "recording stopped\n";
    } else if (!lastFrame_.empty()) {
        std::string path = timestampName("VID", ".mp4");
        cv::Size sz(lastFrame_.cols, lastFrame_.rows);
        if (recorder_.start(path, sz, cam_->fps(), cfg_.audio))
            std::cout << "recording -> " << path << "\n";
    }
}

// Blocking playback of the selected video: renders frames at the source fps and
// returns to the gallery on end, tap or key.
void App::playCurrentVideo() {
    if (!gallery_ || gallery_->empty() || !gallery_->currentIsVideo()) return;
    cv::VideoCapture vc(gallery_->current());
    if (!vc.isOpened()) return;

    mode_ = Mode::Playback;
    double fps = vc.get(cv::CAP_PROP_FPS);
    Uint32 frameMs = (Uint32)(fps > 1.0 ? 1000.0 / fps : 33.0);

    cv::Mat frame;
    bool stop = false;
    while (running_ && !stop && vc.read(frame) && !frame.empty()) {
        Uint32 t0 = SDL_GetTicks();
        beginFrame();
        renderMat(frame);
        present();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running_ = false; stop = true; }
            else if (e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN ||
                     e.type == SDL_FINGERDOWN) stop = true;
        }
        Uint32 dt = SDL_GetTicks() - t0;
        if (dt < frameMs) SDL_Delay(frameMs - dt);
    }
    mode_ = Mode::Gallery;
    menu_.wake();
}

// ---------------------------------------------------------------------------
// Per-mode rendering
// ---------------------------------------------------------------------------

void App::renderCamera() {
    cv::Mat frame;
    if (cam_->read(frame)) lastFrame_ = frame;

    if (recorder_.recording()) recorder_.writeFrame(lastFrame_);

    beginFrame();
    renderMat(lastFrame_);

    // Persistent recording indicator (independent of the menu fade).
    if (recorder_.recording()) {
        int r = std::max(8, viewH_ / 60);
        filledCircleRGBA(ren_, 24 + r, 24 + r, r, 235, 60, 60, 235);
    }

    if (menu_.awake()) {
        Uint8 a = menu_.alpha();
        auto btns = menu_.layout(Mode::Camera, viewW_, viewH_, false);
        for (const auto& b : btns)
            Menu::drawButton(ren_, b, a, recorder_.recording());
    }
    present();
}

void App::ensureGalleryImage() {
    if (gallery_->empty()) { galleryMat_.release(); galleryShown_.clear(); return; }
    const std::string& path = gallery_->current();
    if (path == galleryShown_ && !galleryMat_.empty()) return;

    if (Gallery::isVideo(path)) {
        cv::VideoCapture vc(path);
        cv::Mat first;
        if (vc.isOpened()) vc.read(first);
        galleryMat_ = first;
    } else {
        galleryMat_ = cv::imread(path, cv::IMREAD_COLOR);
    }
    galleryShown_ = path;
}

void App::renderGallery() {
    ensureGalleryImage();

    beginFrame();
    if (galleryMat_.empty()) {
        clear(); // nothing captured yet: black with just the Back control
    } else {
        renderMat(galleryMat_);
        // A centred play glyph hints that the current item is a video.
        if (gallery_->currentIsVideo()) {
            int r = std::max(30, viewH_ / 10);
            filledCircleRGBA(ren_, viewW_ / 2, viewH_ / 2, r, 0, 0, 0, 90);
            drawIcon(ren_, Action::Play, viewW_ / 2, viewH_ / 2,
                     (int)(r * 0.62), 220, false);
        }
    }

    Uint8 a = menu_.awake() ? menu_.alpha() : (Uint8)0;
    if (a > 0) {
        bool hasVideo = gallery_->currentIsVideo();
        auto btns = menu_.layout(Mode::Gallery, viewW_, viewH_, hasVideo);
        for (const auto& b : btns) Menu::drawButton(ren_, b, a, false);
    }
    present();
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

int App::run() {
    while (running_) {
        pumpEvents();
        if (!running_) break;

        switch (mode_) {
            case Mode::Camera:
                renderCamera();
                break;
            case Mode::Gallery:
                renderGallery();
                break;
            case Mode::ConfirmDelete: {
                // Dim the shown item, then two always-on confirm buttons.
                ensureGalleryImage();
                beginFrame();
                if (!galleryMat_.empty()) renderMat(galleryMat_);
                else clear();
                boxRGBA(ren_, 0, 0, viewW_, viewH_, 0, 0, 0, 120);
                auto btns = menu_.layout(Mode::ConfirmDelete, viewW_, viewH_, false);
                for (const auto& b : btns) Menu::drawButton(ren_, b, 255, false);
                present();
                break;
            }
            case Mode::Playback:
                // Playback runs its own loop in playCurrentVideo(); nothing here.
                break;
        }
    }
    return 0;
}

} // namespace olc
