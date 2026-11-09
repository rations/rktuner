// Settings — what the rktuner app remembers between runs, kept as a flattened
// BMessage in ~/config/settings/rktuner/settings:
//   "backend"         string  "mediakit" or "jack"
//   "input:mediakit"  string  the source last chosen on the Media Kit
//   "input:jack"      string  the source last chosen on JACK
//   "hear"            bool    whether the input is passed through to the outputs
//   "param:<name>"    float   the tuner's parameters (the A4 reference)
//   "frame"           BRect   the window's frame
// The file is read once at start and written when the window closes.

#pragma once

#include <Message.h>

namespace rktuner
{

// The settings file's contents; an empty message if it is missing or unreadable.
BMessage LoadSettings();
// Writes the settings file (directories created as needed).
status_t SaveSettings(const BMessage &settings);

} // namespace rktuner
