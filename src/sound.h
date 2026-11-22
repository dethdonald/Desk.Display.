#pragma once
#include <Arduino.h>

// Short UI sounds through the ES8311 codec + onboard amplifier.
// Playback runs in its own task, so callers never block.
enum SoundKind : uint8_t { SND_PAGE, SND_ROTATE, SND_TEST };

void soundInit();
void soundApplySettings();      // main loop: volume / enabled flags
void soundPlay(SoundKind kind);
const char *soundStatus();      // "ready" or what failed // honours settings (enabled, per-event switches, quiet at night)
