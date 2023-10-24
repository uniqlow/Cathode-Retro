#pragma once

#include <Windows.h>
#include <functional>

#include "CathodeRetro/Settings.h"


bool RunSettingsDialog(
  HWND parentWindow,
  CathodeRetro::SignalType *signalTypeInOut,
  CathodeRetro::SourceSettings *sourceSettingsInOut,
  CathodeRetro::ArtifactSettings *artifactSettingsInOut,
  CathodeRetro::TVKnobSettings *knobSettingsInOut,
  CathodeRetro::OverscanSettings *overscanSettingsInOut,
  CathodeRetro::ScreenSettings *screenSettingsInOut);