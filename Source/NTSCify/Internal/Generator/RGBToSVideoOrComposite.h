#pragma once

#include <algorithm>

#include "NTSCify/Internal/Constants.h"
#include "NTSCify/Internal/SignalLevels.h"
#include "NTSCify/Internal/Util.h"
#include "NTSCify/ArtifactSettings.h"
#include "NTSCify/GraphicsDevice.h"
#include "NTSCify/SourceSettings.h"


namespace NTSCify::Internal::Generator
{
  // Take an RGB input texture (usually the output of the game or emulator) and convert it into either an SVideo (separate luma/chroma) or Composite
  //  (a single combined channel) output. We will also, if temporalArtifactReduction is non-zero, generate a second signal into the output texture:
  //  this represents the same /frame/ of data, but with a different starting phase, so that we can mix them together to reduce the flickering that
  //  the output of NES-style timings will give you normally.
  class RGBToSVideoOrComposite
  {
  public:
    RGBToSVideoOrComposite(IGraphicsDevice *device, uint32_t rgbTextureWidthIn, uint32_t signalTextureWidthIn, uint32_t scanlineCountIn)
    : rgbTextureWidth(rgbTextureWidthIn)
    , scanlineCount(scanlineCountIn)
    , signalTextureWidth(signalTextureWidthIn)
    {
      constantBuffer = device->CreateConstantBuffer(std::max(sizeof(RGBToSVideoConstantData), sizeof(GeneratePhaseTextureConstantData)));
      rgbToSVideoShader = device->CreateShader(ShaderID::RGBToSVideoOrComposite);
      generatePhaseTextureShader = device->CreateShader(ShaderID::GeneratePhaseTexture);
    }


    void Generate(
      IGraphicsDevice *device, 
      SignalType signalType,
      const ITexture *rgbTexture, 
      ITexture *phaseTextureOut,
      ITexture *signalTextureOut,
      SignalLevels *levelsOut,
      float initialFramePhase, 
      float prevFrameStartPhase,
      float phaseIncrementPerScanline, 
      const ArtifactSettings &artifactSettings)
    {
      // Update our scanline phases texture
      {
        GeneratePhaseTextureConstantData cd = 
        { 
          initialFramePhase,
          prevFrameStartPhase,
          phaseIncrementPerScanline,
          k_signalSamplesPerColorCycle,
          artifactSettings.instabilityScale,
          noiseSeed,
          signalTextureWidth,
          scanlineCount,
        };

        device->DiscardAndUpdateBuffer(constantBuffer.get(), &cd);

        device->RenderQuad(
          generatePhaseTextureShader.get(),
          phaseTextureOut,
          {},
          {SamplerType::LinearClamp},
          {constantBuffer.get()});
      }

      // Now run the actual shader
      {
        RGBToSVideoConstantData cd = 
        { 
          k_signalSamplesPerColorCycle, 
          rgbTextureWidth, 
          signalTextureWidth,  
          scanlineCount,
          (signalType == SignalType::Composite) ? 1.0f : 0.0f,
          artifactSettings.instabilityScale,
          noiseSeed,
        };

        device->DiscardAndUpdateBuffer(constantBuffer.get(), &cd);

        device->RenderQuad(
          rgbToSVideoShader.get(),
          signalTextureOut,
          {rgbTexture, phaseTextureOut},
          {SamplerType::LinearClamp},
          {constantBuffer.get()});
      }

      levelsOut->blackLevel = 0.0f;
      levelsOut->whiteLevel = 1.0f;
      levelsOut->saturationScale = 0.5f;

      noiseSeed = (noiseSeed + 1) & 0x000FFFFF;
    }

  private:
    struct RGBToSVideoConstantData
    {
      uint32_t outputTexelsPerColorburstCycle;        // This value should match k_signalSamplesPerColorCycle
      uint32_t inputWidth;                            // The width of the input, in texels
      uint32_t outputWidth;                           // The width of the output, in texels
      uint32_t scanlineCount;                         // How many scanlines
      float compositeBlend;                           // 0 if we're outputting to SVideo, 1 if it's composite
      float instabilityScale;
      uint32_t noiseSeed;
    };

    struct GeneratePhaseTextureConstantData
    {
      float g_initialFrameStartPhase;                 // The phase at the start of the first scanline of this frame
      float g_prevFrameStartPhase;                    // The phase at the start of the previous scanline of this frame (if relevant)
      float g_phaseIncrementPerScanline;              // The amount to increment the phase each scanline
      uint32_t g_samplesPerColorburstCycle;           // Should match k_signalSamplesPerColorCycle
      float instabilityScale;
      uint32_t noiseSeed;
      uint32_t signalTextureWidth;
      uint32_t scanlineCount;                         // How many scanlines
    };

    uint32_t rgbTextureWidth;
    uint32_t scanlineCount;
    uint32_t signalTextureWidth;
    std::unique_ptr<IShader> rgbToSVideoShader;
    std::unique_ptr<IShader> generatePhaseTextureShader;
    std::unique_ptr<IConstantBuffer> constantBuffer;

    uint32_t noiseSeed = 0;
  };
}