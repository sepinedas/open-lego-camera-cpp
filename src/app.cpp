#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <iostream>
#include <sys/stat.h>
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
    if (thumbTex_) SDL_DestroyTexture(thumbTex_);
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

// Scaled bitmap text via SDL2_gfx's 8x8 font: render once into a small target
// texture, then blit it magnified. Keeps text crisp and avoids an SDL_ttf/font
// dependency. Falls back to unscaled text if target textures aren't supported.
void App::drawText(int x, int topY, const std::string& s, int scale,
                   SDL_Color c, bool center) {
    if (s.empty()) return;
    int w = 8 * (int)s.size(), h = 8;
    SDL_Texture* prev = SDL_GetRenderTarget(ren_);
    SDL_Texture* t = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_TARGET, w, h);
    if (!t) {
        stringRGBA(ren_, (Sint16)(center ? x - w / 2 : x), (Sint16)topY,
                   s.c_str(), c.r, c.g, c.b, c.a);
        return;
    }
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetRenderTarget(ren_, t);
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 0);
    SDL_RenderClear(ren_);
    stringRGBA(ren_, 0, 0, s.c_str(), c.r, c.g, c.b, c.a);
    SDL_SetRenderTarget(ren_, prev);
    SDL_Rect dst{center ? x - (w * scale) / 2 : x, topY, w * scale, h * scale};
    SDL_RenderCopy(ren_, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

// Newest photo/video in the output dir (timestamp names sort chronologically).
static std::string newestMedia(const std::string& dir) {
    std::string best;
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d)) {
            std::string n = e->d_name;
            if (n.find(".video.mp4") != std::string::npos) continue; // mux temp
            if (n.find(".audio.wav") != std::string::npos) continue;
            auto ends = [&](const char* x) {
                std::string s(x);
                return n.size() > s.size() &&
                       n.compare(n.size() - s.size(), s.size(), s) == 0;
            };
            if (ends(".jpg") || ends(".jpeg") || ends(".png") || ends(".mp4") ||
                ends(".avi") || ends(".mov")) {
                if (n > best) best = n; // lexicographic == chronological here
            }
        }
        ::closedir(d);
    }
    return best.empty() ? "" : dir + "/" + best;
}

// Rebuild the little square thumbnail shown on the gallery button.
void App::refreshThumbnail() {
    std::string path = newestMedia(cfg_.outputDir);
    if (path.empty()) {
        if (thumbTex_) { SDL_DestroyTexture(thumbTex_); thumbTex_ = nullptr; }
        thumbPath_.clear();
        return;
    }
    if (path == thumbPath_ && thumbTex_) return;

    cv::Mat img;
    if (Gallery::isVideo(path)) {
        cv::VideoCapture vc(path);
        if (vc.isOpened()) vc.read(img);
    } else {
        img = cv::imread(path, cv::IMREAD_COLOR);
    }
    if (img.empty()) return;

    // Centre-crop to a square, then downscale.
    int side = std::min(img.cols, img.rows);
    cv::Rect roi((img.cols - side) / 2, (img.rows - side) / 2, side, side);
    cv::Mat sq;
    cv::resize(img(roi), sq, cv::Size(128, 128), 0, 0, cv::INTER_AREA);

    if (thumbTex_) { SDL_DestroyTexture(thumbTex_); thumbTex_ = nullptr; }
    thumbTex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_BGR24,
                                  SDL_TEXTUREACCESS_STATIC, sq.cols, sq.rows);
    if (thumbTex_) {
        SDL_UpdateTexture(thumbTex_, nullptr, sq.data, (int)sq.step);
        SDL_SetTextureBlendMode(thumbTex_, SDL_BLENDMODE_BLEND);
    }
    thumbPath_ = path;
}

// Draw the gallery button as the last capture's thumbnail (rounded square),
// falling back to the framed-landscape icon when nothing has been captured.
void App::drawGalleryButton(const Button& b, Uint8 alpha) {
    if (!thumbTex_) {
        Menu::drawButton(ren_, b, alpha, false);
        return;
    }
    int s = b.r;
    Uint8 bg = (Uint8)((int)alpha * 42 / 100);
    roundedBoxRGBA(ren_, b.cx - s, b.cy - s, b.cx + s, b.cy + s, 7, 18, 18, 24,
                   std::max<Uint8>(1, bg));
    SDL_SetTextureAlphaMod(thumbTex_, alpha);
    SDL_Rect dst{b.cx - s + 3, b.cy - s + 3, 2 * s - 6, 2 * s - 6};
    SDL_RenderCopy(ren_, thumbTex_, nullptr, &dst);
    roundedRectangleRGBA(ren_, b.cx - s, b.cy - s, b.cx + s, b.cy + s, 7,
                         255, 255, 255, (Uint8)((int)alpha * 55 / 100));
}

// Human-readable capture time. Prefers the IMG_/VID_YYYYMMDD_HHMMSS name;
// falls back to the file's mtime.
static std::string captureTime(const std::string& path) {
    std::string base = path.substr(path.find_last_of('/') + 1);
    // Find an 8-4? pattern: _YYYYMMDD_HHMMSS
    for (size_t i = 0; i + 15 < base.size() + 1; ++i) {
        if (base[i] != '_') continue;
        std::string d = base.substr(i + 1, 8), t;
        if (i + 10 <= base.size() && base[i + 9] == '_')
            t = base.substr(i + 10, 6);
        bool digits = d.size() == 8 && t.size() == 6;
        for (char ch : d + t) if (!std::isdigit((unsigned char)ch)) digits = false;
        if (digits)
            return d.substr(0, 4) + "-" + d.substr(4, 2) + "-" + d.substr(6, 2) +
                   "  " + t.substr(0, 2) + ":" + t.substr(2, 2) + ":" +
                   t.substr(4, 2);
    }
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0) {
        std::tm tm{};
        localtime_r(&st.st_mtime, &tm);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d  %H:%M:%S", &tm);
        return buf;
    }
    return "";
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
    if (!cfg_.faceCascade.empty()) faceFilter_.setCascade(cfg_.faceCascade);
    if (!faceFilter_.ready())
        std::cerr << "filters: no face cascade found; facial filters disabled "
                     "(install `opencv-data` or pass --face-cascade)\n";
    refreshThumbnail();
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
            case SDL_FINGERDOWN:
                handleFingerDown(e.tfinger);
                break;
            case SDL_FINGERMOTION:
                handleFingerMotion(e.tfinger);
                break;
            case SDL_FINGERUP:
                handleFingerUp(e.tfinger);
                break;
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

double App::fingerSpread() const {
    if (fingers_.size() < 2) return 0.0;
    auto it = fingers_.begin();
    float x1 = it->second.x, y1 = it->second.y;
    ++it;
    float x2 = it->second.x, y2 = it->second.y;
    double dx = (double)(x2 - x1) * screenW_, dy = (double)(y2 - y1) * screenH_;
    return std::sqrt(dx * dx + dy * dy);
}

void App::handleFingerDown(const SDL_TouchFingerEvent& f) {
    menu_.wake();
    fingers_[f.fingerId] = {f.x, f.y};
    if (fingers_.size() == 1) {
        // Possible tap; confirmed on finger-up if it stays put and no 2nd finger.
        tapCandidate_ = true;
        tapFinger_ = f.fingerId;
        tapStartX_ = f.x;
        tapStartY_ = f.y;
        tapStartMs_ = SDL_GetTicks();
    } else if (fingers_.size() == 2) {
        // A second finger starts a pinch and cancels the pending tap.
        tapCandidate_ = false;
        pinching_ = true;
        pinchStartDist_ = fingerSpread();
        pinchStartZoom_ = cam_->zoom();
        zoomLabelUntil_ = SDL_GetTicks() + 1200;
    }
}

void App::handleFingerMotion(const SDL_TouchFingerEvent& f) {
    auto it = fingers_.find(f.fingerId);
    if (it != fingers_.end()) it->second = {f.x, f.y};

    if (pinching_ && fingers_.size() >= 2 && pinchStartDist_ > 1.0) {
        double z = pinchStartZoom_ * (fingerSpread() / pinchStartDist_);
        cam_->setZoom(z);
        zoomLabelUntil_ = SDL_GetTicks() + 1200;
    }
    if (tapCandidate_ && f.fingerId == tapFinger_) {
        float dx = f.x - tapStartX_, dy = f.y - tapStartY_;
        if (dx * dx + dy * dy > 0.0009f) tapCandidate_ = false; // ~3% drag
    }
}

void App::handleFingerUp(const SDL_TouchFingerEvent& f) {
    fingers_.erase(f.fingerId);
    if (fingers_.size() < 2) pinching_ = false;

    if (tapCandidate_ && f.fingerId == tapFinger_ && fingers_.empty()) {
        if (SDL_GetTicks() - tapStartMs_ < 700) {
            int px, py, vx, vy;
            mapTouch(f.x, f.y, px, py);
            physicalToView(px, py, vx, vy);
            onTap(vx, vy);
        }
    }
    tapCandidate_ = false;
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
        case Action::OpenGallery:
            gallery_->refresh(); refreshThumbnail(); mode_ = Mode::Gallery; break;
        case Action::Back:        mode_ = Mode::Camera; break;
        case Action::Prev:        gallery_->prev(); break;
        case Action::Next:        gallery_->next(); break;
        case Action::Play:        playCurrentVideo(); break;
        case Action::Delete:
            if (gallery_ && !gallery_->empty()) mode_ = Mode::ConfirmDelete;
            break;
        case Action::ConfirmYes:
            gallery_->deleteCurrent();
            refreshThumbnail();
            mode_ = gallery_->empty() ? Mode::Camera : Mode::Gallery;
            break;
        case Action::ConfirmNo:
            mode_ = Mode::Gallery;
            break;
        case Action::CycleFilter:
            filter_ = nextFilter(filter_);
            filterLabelUntil_ = SDL_GetTicks() + 1500;
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
    flashStart_ = SDL_GetTicks(); // shutter flash animation
    refreshThumbnail();           // update the gallery-button preview
}

void App::toggleRecording() {
    if (recorder_.recording()) {
        recorder_.stop();
        std::cout << "recording stopped\n";
        refreshThumbnail();
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
    if (cam_->read(frame)) {
        // Reshape the live face before it becomes the frame we preview, capture
        // and record, so photos and videos carry the same expression.
        faceFilter_.apply(frame, filter_, filterPhase_);
        lastFrame_ = frame;
    }
    if (filter_ != Filter::None) filterPhase_ += 1.0;

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
        for (const auto& b : btns) {
            if (b.action == Action::OpenGallery)
                drawGalleryButton(b, a); // last-shot thumbnail
            else
                Menu::drawButton(ren_, b, a, recorder_.recording());
        }
    }

    // Magnification factor while/just after pinching (or whenever zoomed in).
    Uint32 now = SDL_GetTicks();
    double z = cam_->zoom();
    if (z > 1.001 || now < zoomLabelUntil_) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1fx", z);
        int scale = std::max(2, viewH_ / 160);
        int tw = 8 * (int)std::string(buf).size() * scale;
        int pad = 8 * scale / 2;
        roundedBoxRGBA(ren_, viewW_ / 2 - tw / 2 - pad, 12,
                       viewW_ / 2 + tw / 2 + pad, 12 + 8 * scale + pad,
                       6, 0, 0, 0, 110);
        drawText(viewW_ / 2, 12 + pad / 2, buf, scale, {255, 255, 255, 235}, true);
    }

    // Active filter name, shown briefly after tapping the smiley button.
    if (now < filterLabelUntil_) {
        std::string name = filterName(filter_);
        int scale = std::max(2, viewH_ / 200);
        int tw = 8 * (int)name.size() * scale;
        int pad = 8 * scale / 2;
        int y = viewH_ / 6;
        roundedBoxRGBA(ren_, viewW_ / 2 - tw / 2 - pad, y,
                       viewW_ / 2 + tw / 2 + pad, y + 8 * scale + pad,
                       6, 0, 0, 0, 120);
        drawText(viewW_ / 2, y + pad / 2, name, scale, {255, 255, 255, 240}, true);
    }

    // Shutter flash: a quick white wash that fades out after a capture.
    if (flashStart_) {
        Uint32 dt = now - flashStart_;
        const Uint32 dur = 320;
        if (dt < dur) {
            Uint8 a = (Uint8)(210.0 * (1.0 - (double)dt / dur));
            boxRGBA(ren_, 0, 0, viewW_, viewH_, 255, 255, 255, a);
        } else {
            flashStart_ = 0;
        }
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

    // Capture date/time, translucent, across the top.
    if (!gallery_->empty()) {
        std::string ts = captureTime(gallery_->current());
        if (!ts.empty()) {
            // Scale for readability but shrink so it fits the width in portrait.
            int fitW = (int)(viewW_ * 0.92 / (8 * ts.size()));
            int scale = std::max(2, std::min({viewH_ / 150, fitW, 4}));
            int barH = 8 * scale + 20;
            boxRGBA(ren_, 0, 0, viewW_, barH, 0, 0, 0, 105);
            drawText(viewW_ / 2, (barH - 8 * scale) / 2, ts, scale,
                     {255, 255, 255, 220}, true);
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
