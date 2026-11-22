/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 ParticleTroned
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "d3d11_test.h"

#include "../../../../renderdoc/api/app/renderdoc_d3d11.h"
#include "../../../../renderdoc/driver/ihv/nv/nvapi_wrapper.h"

RD_TEST(D3D11_External_Write, D3D11GraphicsTest)
{
  static constexpr const char *Description =
      "Opaque shared-texture writes retain actual pixels, dependencies and event order.";

  int main()
  {
    if(!Init())
      return 3;
    auto record = (pRENDERDOC_CaptureD3D11ExternalWrite)GetProcAddress(
        GetModuleHandleA("renderdoc.dll"), "RENDERDOC_CaptureD3D11ExternalWrite");
    if(!rdoc || !record)
      return 4;
    INVAPID3DDevice *nvDevice = NULL;
    CHECK_HR(dev->QueryInterface(__uuidof(INVAPID3DDevice), (void **)&nvDevice));
    ID3D11DevicePtr realDevice;
    CHECK_HR(nvDevice->GetReal()->QueryInterface(__uuidof(ID3D11Device), (void **)&realDevice));
    nvDevice->Release();
    ID3D11DeviceContextPtr realContext;
    realDevice->GetImmediateContext(&realContext);
    ID3D11Texture2DPtr output = MakeTexture(DXGI_FORMAT_R8G8B8A8_UNORM, 8, 8).RTV().SRV().Shared();
    ID3D11Texture2DPtr first = MakeTexture(DXGI_FORMAT_R8G8B8A8_UNORM, 8, 8).RTV().SRV();
    ID3D11Texture2DPtr second = MakeTexture(DXGI_FORMAT_R8G8B8A8_UNORM, 8, 8).RTV().SRV();
    ID3D11Texture2DPtr input = MakeTexture(DXGI_FORMAT_R8G8B8A8_UNORM, 8, 8).RTV().SRV();
    ID3D11Texture2DPtr half = MakeTexture(DXGI_FORMAT_R16G16B16A16_FLOAT, 8, 8).RTV().SRV().Shared();
    SetDebugName(half, "ExternalWrite::HalfFloat");
    ID3D11RenderTargetViewPtr outputRTV = MakeRTV(output);
    SetDebugName(output, "ExternalWrite::Output");
    SetDebugName(first, "ExternalWrite::FirstConsumer");
    SetDebugName(second, "ExternalWrite::SecondConsumer");
    SetDebugName(input, "ExternalWrite::Input");
    const auto sharedRTV = [&](ID3D11Texture2DPtr texture) {
      IDXGIResourcePtr dxgi;
      CHECK_HR(texture->QueryInterface(__uuidof(IDXGIResource), (void **)&dxgi));
      HANDLE shared = NULL;
      CHECK_HR(dxgi->GetSharedHandle(&shared));
      ID3D11Texture2DPtr alias;
      CHECK_HR(realDevice->OpenSharedResource(shared, __uuidof(ID3D11Texture2D), (void **)&alias));
      ID3D11RenderTargetViewPtr view;
      CHECK_HR(realDevice->CreateRenderTargetView(alias, NULL, &view));
      return view;
    };
    ID3D11RenderTargetViewPtr realRTV = sharedRTV(output);
    ID3D11RenderTargetViewPtr halfRTV = sharedRTV(half);
    void *dependencies[] = {(ID3D11Resource *)input};
    TEST_ASSERT(record(NULL, output, 0, "invalid context", "{}", 0, NULL) == E_INVALIDARG,
                "Reject a context that is not a wrapper");
    const auto reject = [&](ID3D11Resource *destination, UINT subresource, const char *label,
                            UINT count, void *const *resources, const char *metadata = "{}") {
      const auto captureCount = rdoc->GetNumCaptures();
      rdoc->StartFrameCapture(dev, NULL);
      const float clear[] = {0, 0, 0, 1};
      ctx->ClearRenderTargetView(outputRTV, clear);
      const HRESULT result = record(ctx, destination, subresource, label, metadata, count, resources);
      const bool rejected = result == E_INVALIDARG;
      const bool discarded =
          rdoc->EndFrameCapture(dev, NULL) == 0 && rdoc->GetNumCaptures() == captureCount;
      TEST_ASSERT(rejected && discarded, "Reject '%s': result 0x%08x, discarded %u", label, result,
                  discarded);
      return rejected && discarded;
    };
    ID3D11BufferPtr wrongType = MakeBuffer().ByteAddressed().UAV().Size(16);
    void *wrongInputs[] = {(ID3D11Buffer *)wrongType};
    const auto checkRejections = [&]() {
      return reject(output, 1, "invalid subresource", 0, NULL) &&
             reject(output, 0, "too many inputs", 17, NULL) &&
             reject(output, 0, "missing inputs", 1, NULL) &&
             reject(output, 0, std::string(128, 'x').c_str(), 0, NULL) &&
             reject(output, 0, "oversized metadata", 0, NULL, std::string(16384, 'x').c_str()) &&
             reject(wrongType, 0, "buffer output", 0, NULL) &&
             reject(output, 0, "buffer input", 1, wrongInputs);
    };
    bool checkedRejections = false;
    D3D11_QUERY_DESC queryDesc = {D3D11_QUERY_OCCLUSION_PREDICATE, 0};
    ID3D11PredicatePtr predicate;
    CHECK_HR(dev->CreatePredicate(&queryDesc, &predicate));
    ctx->Begin(predicate);
    ctx->End(predicate);
    BOOL anySamples = TRUE;
    while(ctx->GetData(predicate, &anySamples, sizeof(anySamples), 0) == S_FALSE)
    {
    }
    TEST_ASSERT(!anySamples, "An empty predicate has no passing samples");
    while(Running())
    {
      const float black[] = {0, 0, 0, 1}, red[] = {1, 0, 0, 1}, green[] = {0, 1, 0, 1};
      ctx->ClearRenderTargetView(outputRTV, black);
      realContext->ClearRenderTargetView(realRTV, red);
      const HRESULT expected = rdoc->IsFrameCapturing() ? S_OK : S_FALSE;
      ctx->SetPredication(predicate, TRUE);
      HRESULT recorded = record(ctx, output, 0, "First", "{\"evaluation\":1}", 1, dependencies);
      TEST_ASSERT(recorded == expected, "First write result 0x%08x, expected 0x%08x", recorded,
                  expected);
      if(recorded != expected)
        return 5;
      ID3D11PredicatePtr restoredPredicate;
      BOOL restoredValue = FALSE;
      ctx->GetPredication(&restoredPredicate, &restoredValue);
      TEST_ASSERT(restoredPredicate == predicate && restoredValue,
                  "Observation restores predication without changing output");
      ctx->SetPredication(NULL, FALSE);
      ctx->CopyResource(first, output);
      ctx->ClearRenderTargetView(outputRTV, black);
      realContext->ClearRenderTargetView(realRTV, green);
      recorded = record(ctx, output, 0, "Second", "{\"evaluation\":2}", 1, dependencies);
      TEST_ASSERT(recorded == expected, "Second write result 0x%08x, expected 0x%08x", recorded,
                  expected);
      if(recorded != expected)
        return 5;
      ctx->CopyResource(second, output);
      ctx->CopyResource(output, first);
      const float fraction[] = {0.25f, 0.5f, 0.75f, 1.0f};
      realContext->ClearRenderTargetView(halfRTV, fraction);
      recorded = record(ctx, half, 0, "HalfFloat", "{}", 1, dependencies);
      TEST_ASSERT(recorded == expected, "Half-float result 0x%08x, expected 0x%08x", recorded,
                  expected);
      if(recorded != expected)
        return 5;
      setMarker("First consumer restored");
      blitToSwap(first);
      Present();
      // Rejection checks own captures and must not nest inside the harness capture.
      if(!checkedRejections && !rdoc->IsFrameCapturing())
      {
        if(!checkRejections())
          return 6;
        checkedRejections = true;
        TEST_LOG("Validated seven rejected vendor boundary captures");
      }
    }
    return checkedRejections ? 0 : 6;
  }
};
REGISTER_TEST();
