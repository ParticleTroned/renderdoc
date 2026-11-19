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
    const auto reject = [&](UINT subresource, const char *label, UINT count, void *const *resources) {
      const auto captureCount = rdoc->GetNumCaptures();
      rdoc->StartFrameCapture(dev, NULL);
      const float clear[] = {0, 0, 0, 1};
      ctx->ClearRenderTargetView(outputRTV, clear);
      TEST_ASSERT(record(ctx, output, subresource, label, "{}", count, resources) == E_INVALIDARG,
                  "Reject invalid vendor boundary arguments");
      TEST_ASSERT(rdoc->EndFrameCapture(dev, NULL) == 0 && rdoc->GetNumCaptures() == captureCount,
                  "Missing vendor output must not produce a successful capture");
    };
    reject(1, "invalid subresource", 0, NULL);
    reject(0, "too many inputs", 17, NULL);
    reject(0, "missing inputs", 1, NULL);
    reject(0, std::string(128, 'x').c_str(), 0, NULL);
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
      TEST_ASSERT(record(ctx, output, 0, "First", "{\"evaluation\":1}", 1, dependencies) == expected,
                  "Record the first real external write");
      ID3D11PredicatePtr restoredPredicate;
      BOOL restoredValue = FALSE;
      ctx->GetPredication(&restoredPredicate, &restoredValue);
      TEST_ASSERT(restoredPredicate == predicate && restoredValue,
                  "Observation restores predication without changing output");
      ctx->SetPredication(NULL, FALSE);
      ctx->CopyResource(first, output);
      ctx->ClearRenderTargetView(outputRTV, black);
      realContext->ClearRenderTargetView(realRTV, green);
      TEST_ASSERT(record(ctx, output, 0, "Second", "{\"evaluation\":2}", 1, dependencies) == expected,
                  "Record the second real external write");
      ctx->CopyResource(second, output);
      ctx->CopyResource(output, first);
      const float fraction[] = {0.25f, 0.5f, 0.75f, 1.0f};
      realContext->ClearRenderTargetView(halfRTV, fraction);
      TEST_ASSERT(record(ctx, half, 0, "HalfFloat", "{}", 1, dependencies) == expected,
                  "Record half-float inference output");
      setMarker("First consumer restored");
      blitToSwap(first);
      Present();
    }
    return 0;
  }
};
REGISTER_TEST();
