#pragma once

#include <cinttypes>
#include "NTSCify/ScreenSettings.h"
#include "GraphicsDevice.h"
#include "resource.h"
#include "Util.h"

namespace NTSCify
{
  // This class takes RGB data (either the input or SVideo/composite filtering final output) and draws it as if it were on a CRT screen
  class RGBToCRT
  {
  public:
    RGBToCRT(
      GraphicsDevice *deviceIn, 
      uint32_t inputImageWidthIn, 
      uint32_t signalTextureWidthIn, 
      uint32_t scanlineCountIn)
    : device(deviceIn)
    , inputImageWidth(inputImageWidthIn)
    , signalTextureWidth(signalTextureWidthIn)
    , scanlineCount(scanlineCountIn)
    {
      rgbToScreenShader = device->CreatePixelShader(IDR_RGB_TO_CRT);
      generateScreenTextureShader = device->CreatePixelShader(IDR_GENERATE_SCREEN_TEXTURE);
      constantBuffer = device->CreateConstantBuffer(sizeof(RGBToScreenConstants));
      samplePatternConstantBuffer = device->CreateConstantBuffer(sizeof(k_samplingPattern16X));

      GenerateShadowMaskTexture();
    }


    void SetScreenSettings(const ScreenSettings &settings)
    { 
      if (memcmp(&settings, &screenSettings, sizeof(ScreenSettings)) != 0)
      {
        screenSettings = settings; 
        screenSettingsDirty = true;
      }
    }


    void Render(const ITexture *currentFrameRGBInput, const ITexture *previousFrameRGBInput)
    {
      uint32_t outputTargetWidth = device->BackbufferWidth();
      uint32_t outputTargetHeight = device->BackbufferHeight();

      // Next up: Set up our shader constants
      {
        RGBToScreenConstants data;

        // Figure out how to adjust our viewed texture area for overscan
        float overscanSizeX = (inputImageWidth - float(screenSettings.overscanLeft + screenSettings.overscanRight));
        float overscanSizeY = (scanlineCount - float(screenSettings.overscanTop + screenSettings.overscanBottom));

        data.overscanScaleX = overscanSizeX / inputImageWidth;
        data.overscanScaleY = overscanSizeY / scanlineCount;

        data.overscanOffsetX = float(int32_t(screenSettings.overscanLeft - screenSettings.overscanRight)) / inputImageWidth * 0.5f;
        data.overscanOffsetY = float(int32_t(screenSettings.overscanTop - screenSettings.overscanBottom)) / scanlineCount * 0.5f;

        // Figure out the aspect ratio of the output, given both our dimensions as well as the pixel aspect ratio in the screen settings.
        {
          float inputPixelRatio = screenSettings.inputPixelAspectRatio;
          const float k_aspect = inputPixelRatio * overscanSizeX / overscanSizeY;
          if (float(outputTargetWidth) > k_aspect * float(outputTargetHeight))
          {
            float desiredWidth = k_aspect * float(outputTargetHeight);
            data.viewScaleX = float(outputTargetWidth) / desiredWidth;
            data.viewScaleY = 1.0f;
          }
          else
          {
            float desiredHeight = float(outputTargetWidth) / k_aspect;
            data.viewScaleX = 1.0f;
            data.viewScaleY = float(outputTargetHeight) / desiredHeight;
          }
        }

        data.distortionX = screenSettings.horizontalDistortion;
        data.distortionY = screenSettings.verticalDistortion;
        data.maskDistortionX = screenSettings.screenEdgeRoundingX + data.distortionX;
        data.maskDistortionY = screenSettings.screenEdgeRoundingY + data.distortionY;
        data.roundedCornerSize = screenSettings.cornerRounding;

        // The values for shadowMaskScale were initially normalized against a 240-pixel-tall screen so just pretend it's ALWAYS that height for purposes
        //  of scaling the shadow mask.
        static constexpr float shadowMaskScaleNormalization = 240.0f * 0.7f;

        data.shadowMaskScaleX = float (inputImageWidth) / float(scanlineCount) 
                              * screenSettings.inputPixelAspectRatio 
                              * shadowMaskScaleNormalization 
                              * 0.45f 
                              * screenSettings.shadowMaskScale;
        data.shadowMaskScaleY = shadowMaskScaleNormalization * screenSettings.shadowMaskScale;
        data.shadowMaskStrength = screenSettings.shadowMaskStrength;
        data.phosphorDecay = screenSettings.phosphorDecay;

        data.scanlineCount = float(scanlineCount);
        data.scanlineStrength = screenSettings.scanlineStrength;

        data.signalTextureWidth = float(signalTextureWidth);

        device->DiscardAndUpdateBuffer(constantBuffer, &data);
      }

      if (screenSettingsDirty 
        || screenTexture == nullptr 
        || screenTexture->Width() != outputTargetWidth 
        || screenTexture->Height() != outputTargetHeight)
      {
        device->DiscardAndUpdateBuffer(samplePatternConstantBuffer, &k_samplingPattern16X);

        if (screenTexture == nullptr 
          || screenTexture->Width() != outputTargetWidth 
          || screenTexture->Height() != outputTargetHeight)
        {
          // Rebuild the texture at the correct resolution
          screenTexture = device->CreateTexture(outputTargetWidth, outputTargetHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFlags::RenderTarget);
        }

        device->RenderQuadWithPixelShader(
          generateScreenTextureShader,
          screenTexture.get(),
          {shadowMaskTexture.get()},
          {SamplerType::Wrap},
          {constantBuffer, samplePatternConstantBuffer});

        screenSettingsDirty = false;
      }

      device->RenderQuadWithPixelShader(
        rgbToScreenShader,
        nullptr,
        {currentFrameRGBInput, previousFrameRGBInput, screenTexture.get()},
        {SamplerType::Clamp},
        {constantBuffer});
    }

  protected:
    // These are float2 sampling patterns, but they need to be float4 aligned for the constant buffers, so there's 2 padding floats per
    //  coordinate.
    // These are standard 8x and 16x sampling patterns (found in the D3D docs here:
    //  https://docs.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_standard_multisample_quality_levels )
    static constexpr float k_samplingPattern8X[] =
    {
       1.0f / 8.0, -3.0f / 8.0,   0.0f, 0.0f,
      -1.0f / 8.0,  3.0f / 8.0,   0.0f, 0.0f,
       5.0f / 8.0,  1.0f / 8.0,   0.0f, 0.0f,
      -3.0f / 8.0, -5.0f / 8.0,   0.0f, 0.0f,
      -5.0f / 8.0,  5.0f / 8.0,   0.0f, 0.0f,
      -7.0f / 8.0, -1.0f / 8.0,   0.0f, 0.0f,
       3.0f / 8.0,  7.0f / 8.0,   0.0f, 0.0f,
       7.0f / 8.0, -7.0f / 8.0,   0.0f, 0.0f,
    };


    static constexpr float k_samplingPattern16X[] =
    {
       1.0f / 8.0,  1.0f / 8.0,   0.0f, 0.0f,
      -1.0f / 8.0, -3.0f / 8.0,   0.0f, 0.0f,
      -3.0f / 8.0,  2.0f / 8.0,   0.0f, 0.0f,
       4.0f / 8.0, -1.0f / 8.0,   0.0f, 0.0f,
      -5.0f / 8.0, -2.0f / 8.0,   0.0f, 0.0f,
       2.0f / 8.0,  5.0f / 8.0,   0.0f, 0.0f,
       5.0f / 8.0,  3.0f / 8.0,   0.0f, 0.0f,
       3.0f / 8.0, -5.0f / 8.0,   0.0f, 0.0f,
      -2.0f / 8.0,  6.0f / 8.0,   0.0f, 0.0f,
       0.0f / 8.0, -7.0f / 8.0,   0.0f, 0.0f,
      -4.0f / 8.0, -6.0f / 8.0,   0.0f, 0.0f,
      -6.0f / 8.0,  4.0f / 8.0,   0.0f, 0.0f,
      -8.0f / 8.0,  0.0f / 8.0,   0.0f, 0.0f,
       7.0f / 8.0, -4.0f / 8.0,   0.0f, 0.0f,
       6.0f / 8.0,  7.0f / 8.0,   0.0f, 0.0f,
      -7.0f / 8.0, -8.0f / 8.0,   0.0f, 0.0f,
    };

    // Generate the shadow mask texture we use for the CRT emulation
    void GenerateShadowMaskTexture()
    {
      // $TODO this texture could be pre-made and the one being generated here is WAY overkill for how tiny it shows up on-screen, but it does look nice!
      static constexpr uint32_t k_size = 512;
      static constexpr uint32_t k_mipCount = 8;

      ComPtr<ID3D11RenderTargetView> tempRTVLevel0;
      shadowMaskTexture = device->CreateTexture(k_size, k_size / 2, k_mipCount, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlags::RenderTarget);

      // First step is the generate the texture at the largest mip level
      {
        ComPtr<ID3D11PixelShader> generateShadowMaskShader = device->CreatePixelShader(IDR_GENERATE_SHADOW_MASK);

        struct GenerateShadowMaskConstants
        {
          float blackLevel;
          float coordinateScale;
          float texWidth;
          float texHeight;
        };

        GenerateShadowMaskConstants consts;
        consts.blackLevel = 0.0;
        consts.coordinateScale = 1.0f / float(k_size);
        consts.texWidth = float(k_size);
        consts.texHeight = float(k_size / 2);

        ComPtr<ID3D11Buffer> constBuf = device->CreateConstantBuffer(sizeof(GenerateShadowMaskConstants));

        device->DiscardAndUpdateBuffer(constBuf, &consts, sizeof(consts));

        device->RenderQuadWithPixelShader(
          generateShadowMaskShader,
          shadowMaskTexture.get(),
          {},
          {SamplerType::Clamp},
          {constBuf});
      }

      // Now it's generated so we need to generate the mips by using our lanczos downsample
      ComPtr<ID3D11PixelShader> downsampleShader = device->CreatePixelShader(IDR_DOWNSAMPLE_2X);
      for (uint32_t destMip = 1; destMip < k_mipCount; destMip++)
      {
        std::unique_ptr<IMipLevelSource> levelSource = device->CreateMipLevelSource(shadowMaskTexture.get(), destMip - 1);
        std::unique_ptr<IMipLevelTarget> levelTarget = device->CreateMipLevelTarget(shadowMaskTexture.get(), destMip);

        device->RenderQuadWithPixelShader(
          downsampleShader,
          levelTarget.get(),
          {levelSource.get()},
          {SamplerType::Wrap},
          {});
      }
    }


    struct RGBToScreenConstants
    {
      float viewScaleX;             // Scale to get the correct aspect ratio of the image
      float viewScaleY;             // ...
      float overscanScaleX;         // overscanSize / standardSize;
      float overscanScaleY;         // ...
      float overscanOffsetX;        // How much texture space to offset the center of our coordinate system due to overscan
      float overscanOffsetY;        // ...
      float distortionX;            // How much to distort (from 0 .. 1)
      float distortionY;            // ...
      float maskDistortionX;        // Where to put the mask sides
      float maskDistortionY;        // ...

      float shadowMaskScaleX;       // Scale of the shadow mask texture lookup
      float shadowMaskScaleY;       // Scale of the shadow mask texture lookup
      float shadowMaskStrength;     // 
      float roundedCornerSize;      // 0 == no corner, 1 == screen is an oval
      float phosphorDecay;          // 
      float scanlineCount;          // How many scanlines there are
      float scanlineStrength;       // How strong the scanlines are (0 == none, 1 == whoa)

      float signalTextureWidth;     // The width (in texels) of the signal texture
    };

    GraphicsDevice *device;

    uint32_t inputImageWidth;
    uint32_t signalTextureWidth;
    uint32_t scanlineCount;

    ComPtr<ID3D11Buffer> constantBuffer;
    ComPtr<ID3D11Buffer> samplePatternConstantBuffer;
    ComPtr<ID3D11PixelShader> rgbToScreenShader;
    ComPtr<ID3D11PixelShader> generateScreenTextureShader;
  
    std::unique_ptr<ITexture> shadowMaskTexture;
    std::unique_ptr<ITexture> screenTexture;

    ScreenSettings screenSettings;
    bool screenSettingsDirty = false;
  };
}