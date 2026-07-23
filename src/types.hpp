#pragma once

// Shared enums and small value types used across the UI and app modules.

namespace olc {

// The high-level screen the app is currently on.
enum class Mode {
    Camera,        // live preview + capture controls
    Gallery,       // browse captured photos/videos
    Playback,      // playing a video from the gallery
    ConfirmDelete, // icon-only yes/no before deleting
};

// Every tappable control maps to exactly one action.
enum class Action {
    None,
    Shutter,     // take a photo
    Record,      // start/stop video (with audio when available)
    ZoomIn,
    ZoomOut,
    OpenGallery, // camera -> gallery
    Back,        // gallery -> camera
    Prev,        // previous item in gallery
    Next,        // next item in gallery
    Play,        // play the selected video
    Delete,      // ask to delete the selected item
    ConfirmYes,  // confirm deletion
    ConfirmNo,   // cancel deletion
    ToggleFilter,// turn the dog face filter on/off
    Quit,
};

} // namespace olc
