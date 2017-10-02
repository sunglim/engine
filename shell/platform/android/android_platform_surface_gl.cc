// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/android/android_platform_surface_gl.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <jni.h>
#include "flutter/common/threads.h"
#include "flutter/lib/ui/painting/resource_context.h"
#include "flutter/shell/platform/android/platform_view_android_jni.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/gpu/GrBackendSurface.h"
#include "third_party/skia/include/gpu/GrTexture.h"
#include "third_party/skia/include/gpu/GrTypes.h"

namespace shell {

AndroidPlatformSurfaceGL::~AndroidPlatformSurfaceGL() {
  fxl::AutoResetWaitableEvent latch;
  blink::Threads::IO()->PostTask([this, &latch]() {
    glDeleteTextures(1, &texture_id_);
    latch.Signal();
  });
  latch.Wait();
}

AndroidPlatformSurfaceGL::AndroidPlatformSurfaceGL(std::shared_ptr<PlatformViewAndroid> platformView): platform_view_(platformView) {
  fxl::AutoResetWaitableEvent latch;
  blink::Threads::IO()->PostTask([this, &latch]() {
    GrGLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, texID);
    texture_id_ = texID;
    latch.Signal();
  });
  latch.Wait();
}

void AndroidPlatformSurfaceGL::MarkNewFrameAvailable() {
  ASSERT_IS_PLATFORM_THREAD;
  blink::Threads::IO()->PostTask([this]() { new_frame_ready_ = true; });
}

sk_sp<SkImage> AndroidPlatformSurfaceGL::MakeSkImage(int width,
                                                     int height,
                                                     GrContext* grContext) {
  ASSERT_IS_GPU_THREAD;
  fxl::AutoResetWaitableEvent latch;
  blink::Threads::IO()->PostTask([this, &latch]() {
    if (new_frame_ready_) {
      platform_view_->UpdateTexImage(Id());
      first_frame_seen_ = true;
      new_frame_ready_ = false;
    }
    latch.Signal();
  });
  latch.Wait();
  if (!first_frame_seen_) {
    return nullptr;
  }
  GrGLTextureInfo textureInfo = {GL_TEXTURE_EXTERNAL_OES, texture_id_};
  GrBackendTexture backendTexture(width, height, kRGBA_8888_GrPixelConfig,
                                  textureInfo);
  sk_sp<SkImage> sk_image = SkImage::MakeFromTexture(
      grContext, backendTexture, kTopLeft_GrSurfaceOrigin,
      SkAlphaType::kPremul_SkAlphaType, nullptr);
  return sk_image;
}

}  // namespace shell