#include "cv.h"
#include "matrix.h"

CvEngine::CvEngine(vr::PVRCompositor *pvrcompositor, vr::IVRSystem *vrsystem) :
  pvrcompositor(pvrcompositor),
  vrsystem(vrsystem)
{
  getOut() << "cv cons 1" << std::endl;
  vr::PVRCompositor::CreateDevice(&cvDevice, &qrContext, &qrSwapChain);
  getOut() << "cv cons 2" << std::endl;

  HRESULT hr = cvDevice->QueryInterface(__uuidof(ID3D11InfoQueue), (void **)&qrInfoQueue);
  if (SUCCEEDED(hr)) {
    qrInfoQueue->PushEmptyStorageFilter();
  } else {
    getOut() << "info queue query failed" << std::endl;
    // abort();
  }

  std::thread([this]() -> void {
    cv::Ptr<cv::ORB> orb = cv::ORB::create();

    std::vector<cv::KeyPoint> trainKeypoints;
    cv::Mat trainDescriptors;
    bool first = true;

    for (;;) {      
      sem.lock();

      Mat inputImage(colorBufferDesc.Height, colorBufferDesc.Width, CV_8UC4);

      cvContext->Wait(fence, fenceValue);
      
      cvContext->CopyResource(
        colorReadTex,
        colorMirrorServerTex
      );
      
      D3D11_MAPPED_SUBRESOURCE resource;
      HRESULT hr = cvContext->Map(
        colorReadTex,
        0,
        D3D11_MAP_READ,
        0,
        &resource
      );
      if (FAILED(hr)) {
        getOut() << "failed to map read texture: " << (void *)hr << std::endl;
        InfoQueueLog();
        abort();
      }

      getOut() << "thread 6 " << colorBufferDesc.Width << " " << (void *)resource.pData << " " << resource.RowPitch << " " << resource.DepthPitch << " " << (inputImage.total() * inputImage.elemSize()) << " " << (colorBufferDesc.Width * colorBufferDesc.Height * 4) << std::endl;

      UINT lBmpRowPitch = colorBufferDesc.Width * 4;
      BYTE *sptr = reinterpret_cast<BYTE *>(resource.pData);
      BYTE *dptr = (BYTE *)inputImage.ptr(); // + (lBmpRowPitch * colorBufferDesc.Height) - lBmpRowPitch;
      for (size_t h = 0; h < colorBufferDesc.Height; ++h) {
        memcpy(dptr, sptr, lBmpRowPitch);
        sptr += resource.RowPitch;
        // dptr -= lBmpRowPitch;
        dptr += lBmpRowPitch;
      }
      // memcpy(inputImage.ptr(), subresource.pData, colorBufferDesc.Width * colorBufferDesc.Height * 4);

      // getOut() << "thread 7" << std::endl;

      cvContext->Unmap(colorReadTex, 0);

      std::vector<cv::KeyPoint> queryKeypoints;
      cv::Mat queryDescriptors;
      orb->detectAndCompute(inputImage, cv::noArray(), queryKeypoints, queryDescriptors);

      if (first) {
        trainKeypoints = queryKeypoints;
        trainDescriptors = queryDescriptors;

        first = false;
      }
      
      std::vector<cv::DMatch> matches;
      cv::BFMatcher bf(cv::NORM_HAMMING, true);
      bf.match(queryDescriptors, trainDescriptors, matches);

      points.resize(matches.size() * 2);
      for (size_t i = 0; i < matches.size(); i++) {
        cv::DMatch &match = matches[i];
        int queryIdx = match.queryIdx;
        const cv::KeyPoint &keypoint = queryKeypoints[queryIdx];

        points[i*2] = keypoint.pt.x;
        points[i*2 + 1] = keypoint.pt.y;
        // getOut() << "  " << keypoint.pt.x << " " << keypoint.pt.y << "\n";
      }

      running = false;
    }
  }).detach();
}
void CvEngine::setEnabled(bool enabled) {
  pvrcompositor->submitCallbacks.push_back([this](ID3D11Device5 *device, ID3D11DeviceContext4 *context, ID3D11Texture2D *colorTex) -> void {
    getOut() << "cb 1" << std::endl;

    if (!running) {
      running = true;

      getOut() << "cb 2" << std::endl;

      D3D11_TEXTURE2D_DESC desc;
      colorTex->GetDesc(&desc);
      
      getOut() << "cb 3" << std::endl;

      HRESULT hr;
      if (!colorMirrorClientTex || desc.Width != colorBufferDesc.Width || desc.Height != colorBufferDesc.Height) {
        getOut() << "cb 4" << std::endl;
        
        colorBufferDesc = desc;

        D3D11_TEXTURE2D_DESC mirrorDesc = desc;
        mirrorDesc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
        hr = device->CreateTexture2D(
          &colorBufferDesc,
          NULL,
          &colorMirrorClientTex
        );
        if (FAILED(hr)) {
          getOut() << "failed to create color mirror texture: " << (void *)hr << std::endl;
          this->pvrcompositor->InfoQueueLog();
          InfoQueueLog();
          abort();
        }

        D3D11_TEXTURE2D_DESC readDesc = desc;
        readDesc.Usage = D3D11_USAGE_STAGING;
        readDesc.BindFlags = 0;
        readDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        readDesc.MiscFlags = 0;
        hr = qrDevice->CreateTexture2D(
          &readDesc,
          NULL,
          &colorReadTex
        );
        if (FAILED(hr)) {
          getOut() << "failed to create color read texture: " << (void *)hr << std::endl;
          this->pvrcompositor->InfoQueueLog();
          InfoQueueLog();
          abort();
        }

        IDXGIResource *colorMirrorClientRes;
        hr = colorMirrorClientTex->QueryInterface(__uuidof(IDXGIResource), (void**)&colorMirrorClientRes); 
        if (FAILED(hr)) {
          getOut() << "failed to query color buffer shared resource: " << (void *)hr << std::endl;
          abort();
        }

        HANDLE colorMirrorHandle = NULL;
        hr = colorMirrorClientRes->GetSharedHandle(&colorMirrorHandle);
        if (FAILED(hr)) {
          getOut() << "failed to get color buffer share handle: " << (void *)hr << " " << (void *)colorMirrorClientRes << std::endl;
          this->pvrcompositor->InfoQueueLog();
          InfoQueueLog();
          abort();
        }
        
        IDXGIResource *colorMirrorServerRes;
        HRESULT hr = qrDevice->OpenSharedResource(colorMirrorHandle, __uuidof(IDXGIResource), (void**)&colorMirrorServerRes);
        if (FAILED(hr)) {
          getOut() << "failed to unpack color server shared texture handle: " << (void *)hr << " " << (void *)colorMirrorHandle << std::endl;
          abort();
        }

        hr = colorMirrorServerRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&colorMirrorServerTex); 
        if (FAILED(hr)) {
          getOut() << "failed to unpack color server shared texture: " << (void *)hr << " " << (void *)colorMirrorServerRes << std::endl;
          abort();
        }
        
        hr = device->CreateFence(
          0, // value
          D3D11_FENCE_FLAG_SHARED, // flags
          __uuidof(ID3D11Fence), // interface
          (void **)&fence // out
        );
        if (FAILED(hr)) {
          getOut() << "failed to create color buffer fence" << std::endl;
          abort();
        }

        colorMirrorClientRes->Release();
        colorMirrorServerRes->Release();
        
        getOut() << "cb 5" << std::endl;
      }
      
      getOut() << "cb 6" << std::endl;

      context->CopyResource(
        colorMirrorClientTex,
        colorTex
      );
      
      getOut() << "cb 7" << std::endl;
      
      ++fenceValue;
      context->Signal(fence, fenceValue);

      eyeWidth = pvrcompositor->width;
      eyeHeight = pvrcompositor->height;
      vr::HmdMatrix34_t viewMatrixHmd = pvrcompositor->GetViewMatrix();
      // float viewMatrix[16];
      setPoseMatrix(viewMatrixInverse, viewMatrixHmd);
      // getMatrixInverse(viewMatrix, viewMatrixInverse);

      vr::HmdMatrix34_t stageMatrixHmd = pvrcompositor->GetStageMatrix();
      // float stageMatrix[16];
      setPoseMatrix(stageMatrixInverse, stageMatrixHmd);
      // getMatrixInverse(stageMatrix, stageMatrixInverse);

      vr::HmdMatrix44_t projectionMatrixHmd = pvrcompositor->GetProjectionMatrix();
      float projectionMatrix[16];
      setPoseMatrix(projectionMatrix, projectionMatrixHmd);
      getMatrixInverse(projectionMatrix, projectionMatrixInverse);

      getOut() << "cb 8" << std::endl;

      sem.unlock();
      
      getOut() << "cb 9" << std::endl;
    }
  });
}
const std::vector<float> &CvEngine::getFeatures() const {
  return points;
}