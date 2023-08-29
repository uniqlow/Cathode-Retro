#pragma once

#include "NTSCify/SourceSettings.h"


namespace NTSCify::Internal
{
  struct SignalProperties
  {
    SignalType type;
    uint32_t scanlineWidth;
    uint32_t scanlineCount;
    float colorCyclesPerInputPixel;
    float inputPixelAspectRatio; // $TODO: Does this really belong here? Need a better aspect ratio wiring but this works for now
  };
}