#include "qr.h"
#include "matrix.h"

// int ssId = 0;
QrEngine::QrEngine(vr::PVRCompositor *pvrcompositor, vr::IVRSystem *vrsystem) :
  pvrcompositor(pvrcompositor),
  vrsystem(vrsystem)
{
  getOut() << "qr cons 1" << std::endl;
  vr::PVRCompositor::CreateDevice(&qrDevice, &qrContext, &qrSwapChain);
  getOut() << "qr cons 2" << std::endl;
  
  HRESULT hr = qrDevice->QueryInterface(__uuidof(ID3D11InfoQueue), (void **)&qrInfoQueue);
  if (SUCCEEDED(hr)) {
    qrInfoQueue->PushEmptyStorageFilter();
  } else {
    getOut() << "info queue query failed" << std::endl;
    // abort();
  }

  std::thread([this]() -> void {
    cv::QRCodeDetector qrDecoder(cv::QRCodeDetector::QRCodeDetector());
    
    for (;;) {
      // getOut() << "thread 1" << std::endl;
      
      sem.lock();
      
      // getOut() << "thread 2" << std::endl;

      cv::Mat inputImage(colorBufferDesc.Height, colorBufferDesc.Width, CV_8UC4);
      
      // getOut() << "thread 3 " << (void *)fence << " " << inputImage.isContinuous() << std::endl;

      qrContext->Wait(fence, fenceValue);
      
      // getOut() << "thread 4 " << (void *)colorReadTex << " " << (void *)colorMirrorServerTex << std::endl;
      
      qrContext->CopyResource(
        colorReadTex,
        colorMirrorServerTex
      );
      
      // getOut() << "thread 5" << std::endl;
      
      D3D11_MAPPED_SUBRESOURCE resource;
      HRESULT hr = qrContext->Map(
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

      // getOut() << "thread 6 " << colorBufferDesc.Width << " " << (void *)resource.pData << " " << resource.RowPitch << " " << resource.DepthPitch << " " << (inputImage.total() * inputImage.elemSize()) << " " << (colorBufferDesc.Width * colorBufferDesc.Height * 4) << std::endl;

      /* char cwdBuf[MAX_PATH];
      if (!GetCurrentDirectory(sizeof(cwdBuf), cwdBuf)) {
        getOut() << "failed to get current directory" << std::endl;
        abort();
      }
      std::string qrPngPath = cwdBuf;
      qrPngPath += R"EOF(\..\..\..\..\..\qr.png)EOF";
      getOut() << "read qr code image 1 " << qrPngPath << std::endl;
      Mat inputImage = imread(qrPngPath, IMREAD_COLOR);
      getOut() << "read qr code image 2" << std::endl; */

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

      getOut() << "thread 7" << std::endl;

      qrContext->Unmap(colorReadTex, 0);

      // imwrite((std::string(R"EOF(C:\Users\avaer\Documents\GitHub\overlay\tmp\)EOF") + std::to_string(++ssId) + std::string(".png")).c_str(), inputImage);

      // Mat inputImage2;
      // cvtColor(inputImage, inputImage2, COLOR_BGRA2GRAY);

      // getOut() << "thread 8 " << (int)inputImage2.ptr()[0] << " " << (int)inputImage2.ptr()[1] << " " << (int)inputImage2.ptr()[2] << " " << (int)inputImage2.ptr()[3] << std::endl;

      cv::Mat bbox, rectifiedImage;

      std::string data = qrDecoder.detectAndDecode(inputImage, bbox, rectifiedImage);
      getOut() << "thread 9 " << data.length() << std::endl;

      {
        std::lock_guard<Mutex> lock(mut);

        if (data.length() > 0 && bbox.type() == CV_32FC2 && bbox.rows == 4 && bbox.cols == 1) {
          getOut() << "Decoded QR code: " << data << " " <<
            bbox.at<cv::Point2f>(0).x << " " << bbox.at<cv::Point2f>(0).y << " " <<
            bbox.at<cv::Point2f>(1).x << " " << bbox.at<cv::Point2f>(1).y << " " <<
            bbox.at<cv::Point2f>(2).x << " " << bbox.at<cv::Point2f>(2).y << " " <<
            bbox.at<cv::Point2f>(3).x << " " << bbox.at<cv::Point2f>(3).y << " " <<
            std::endl;

          qrCodes.resize(1);
          QrCode &qrCode = qrCodes[0];
          qrCode.data = std::move(data);
          
          for (int i = 0; i < 4; i++) {
            const cv::Point2f &p = bbox.at<cv::Point2f>(i);
            float worldPoint[4] = {
              (p.x/(float)eyeWidth) * 2.0f - 1.0f,
              (1.0f-(p.y/(float)eyeHeight)) * 2.0f - 1.0f,
              0.0f,
              1.0f,
            };
            applyVector4Matrix(worldPoint, projectionMatrixInverse);
            perspectiveDivideVector(worldPoint);
            applyVector4Matrix(worldPoint, viewMatrixInverse);
            applyVector4Matrix(worldPoint, stageMatrixInverse);

            qrCode.points[i*3] = worldPoint[0];
            qrCode.points[i*3+1] = worldPoint[1];
            qrCode.points[i*3+2] = worldPoint[2];
          }
        } else {
          qrCodes.clear();
        }
      }

      // getOut() << "thread 10 " << data.length() << std::endl;

      running = false;
    }
  }).detach();
}
void QrEngine::setEnabled(bool enabled) {
  pvrcompositor->submitCallbacks.push_back([this](ID3D11Device5 *device, ID3D11DeviceContext4 *context, ID3D11Texture2D *colorTex) -> void {
    // getOut() << "cb 1" << std::endl;

    if (!running) {
      running = true;

      // getOut() << "cb 2" << std::endl;

      D3D11_TEXTURE2D_DESC desc;
      colorTex->GetDesc(&desc);
      
      // getOut() << "cb 3" << std::endl;

      HRESULT hr;
      if (!colorMirrorClientTex || desc.Width != colorBufferDesc.Width || desc.Height != colorBufferDesc.Height) {
        // getOut() << "cb 4" << std::endl;
        
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
        
        // getOut() << "cb 5" << std::endl;
      }
      
      // getOut() << "cb 6" << std::endl;

      context->CopyResource(
        colorMirrorClientTex,
        colorTex
      );
      
      // getOut() << "cb 7" << std::endl;
      
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

      // getOut() << "cb 8" << std::endl;

      sem.unlock();
      
      // getOut() << "cb 9" << std::endl;
    }
  });
}
void QrEngine::getQrCodes(std::function<void(const std::vector<QrCode> &)> cb) {
  std::lock_guard<Mutex> lock(mut);
  cb(qrCodes);
}
void QrEngine::InfoQueueLog() {
  if (qrInfoQueue) {
    vr::PVRCompositor::InfoQueueLog(qrInfoQueue);
  }
}