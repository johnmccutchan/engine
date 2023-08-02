// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/android/android_external_texture_gl.h"

#include <utility>

#include "flutter/display_list/effects/dl_color_source.h"
#include "flutter/fml/logging.h"
#include "impeller/core/texture_descriptor.h"
#include "impeller/display_list/dl_image_impeller.h"
#include "impeller/renderer/backend/gles/handle_gles.h"
#include "impeller/renderer/backend/gles/texture_gles.h"

#include "third_party/skia/include/core/SkAlphaType.h"
#include "third_party/skia/include/core/SkColorSpace.h"
#include "third_party/skia/include/core/SkColorType.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/gpu/GrBackendSurface.h"
#include "third_party/skia/include/gpu/GrDirectContext.h"
#include "third_party/skia/include/gpu/ganesh/SkImageGanesh.h"
#include "third_party/skia/include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "third_party/skia/include/gpu/gl/GrGLTypes.h"

namespace flutter {

AndroidExternalTextureGL::AndroidExternalTextureGL(
    int64_t id,
    const fml::jni::ScopedJavaGlobalRef<jobject>& surface_texture,
    std::shared_ptr<PlatformViewAndroidJNI> jni_facade,
    const std::shared_ptr<impeller::ContextGLES>& impeller_context)
    : Texture(id),
      jni_facade_(std::move(jni_facade)),
      surface_texture_(surface_texture),
      impeller_context_(impeller_context),
      transform_(SkMatrix::I()) {}

AndroidExternalTextureGL::~AndroidExternalTextureGL() {
  if (impeller_context_ != nullptr) {
    FML_LOG(ERROR) << "Impeller TODO: Destructor";
  } else {
    if (state_ == AttachmentState::kAttached) {
      glDeleteTextures(1, &texture_name_);
    }
  }
}

void AndroidExternalTextureGL::OnGrContextCreated() {
  state_ = AttachmentState::kUninitialized;
  external_image_.reset();
  texture_gles_.reset();
}

void AndroidExternalTextureGL::OnGrContextDestroyed() {
  external_image_.reset();
  texture_gles_.reset();
  if (impeller_context_ == nullptr) {
    if (state_ == AttachmentState::kAttached) {
      Detach();
      glDeleteTextures(1, &texture_name_);
    }
  }
  state_ = AttachmentState::kDetached;
}

void AndroidExternalTextureGL::MarkNewFrameAvailable() {
  new_frame_ready_ = true;
}

void AndroidExternalTextureGL::Paint(PaintContext& context,
                                     const SkRect& bounds,
                                     bool freeze,
                                     const DlImageSampling sampling) {
  if (state_ == AttachmentState::kDetached) {
    return;
  }
  if (state_ == AttachmentState::kUninitialized) {
    Inititialize(bounds.width(), bounds.height());
    FML_CHECK(state_ == AttachmentState::kAttached);
  }

  const bool needs_update =
      (!freeze && new_frame_ready_) || external_image_ == nullptr;

  if (needs_update) {
    Update(context);
    new_frame_ready_ = false;
  }

  FML_LOG(ERROR) << "::Paint bounds = " << bounds.width() << "x"
                 << bounds.height() << " offset = " << bounds.x() << "x"
                 << bounds.y() << " identity=" << transform_.isIdentity();
  transform_.dump();

  if (external_image_) {
    DlAutoCanvasRestore autoRestore(context.canvas, true);

    // The incoming texture is vertically flipped, so we flip it
    // back. OpenGL's coordinate system has Positive Y equivalent to up, while
    // Skia's coordinate system has Negative Y equvalent to up.
    context.canvas->Translate(bounds.x(), bounds.y() + bounds.height());
    context.canvas->Scale(bounds.width(), -bounds.height());

    if (!transform_.isIdentity()) {
      DlImageColorSource source(external_image_, DlTileMode::kRepeat,
                                DlTileMode::kRepeat, sampling, &transform_);

      DlPaint paintWithShader;
      if (context.paint) {
        paintWithShader = *context.paint;
      }
      paintWithShader.setColorSource(&source);
      context.canvas->DrawRect(SkRect::MakeWH(1, 1), paintWithShader);
    } else {
      context.canvas->DrawImage(external_image_, {0, 0}, sampling,
                                context.paint);
    }
  }
}

void AndroidExternalTextureGL::UpdateTransform() {
  jni_facade_->SurfaceTextureGetTransformMatrix(
      fml::jni::ScopedJavaLocalRef<jobject>(surface_texture_), transform_);

  // Android's SurfaceTexture transform matrix works on texture coordinate
  // lookups in the range 0.0-1.0, while Skia's Shader transform matrix works
  // on the image itself, as if it were inscribed inside a clip rect. An
  // Android transform that scales lookup by 0.5 (displaying 50% of the
  // texture) is the same as a Skia transform by 2.0 (scaling 50% of the image
  // outside of the virtual "clip rect"), so we invert the incoming matrix.
  SkMatrix inverted;
  if (!transform_.invert(&inverted)) {
    FML_LOG(FATAL) << "Invalid SurfaceTexture transformation matrix";
  }
  transform_ = inverted;
}

void AndroidExternalTextureGL::Inititialize(int width, int height) {
  GLuint texture_id;
  if (impeller_context_ == nullptr) {
    glGenTextures(1, &texture_name_);
    texture_id = texture_name_;
  } else {
    FML_CHECK(impeller_context_ != nullptr);
    impeller::TextureDescriptor desc;
    desc.storage_mode = impeller::StorageMode::kDevicePrivate;
    desc.format = impeller::PixelFormat::kR8G8B8A8UNormInt;
    desc.size = {width, height};
    desc.mip_count = 1;
    texture_gles_ = std::make_shared<impeller::TextureGLES>(
        impeller_context_->GetReactor(), desc, GL_TEXTURE_EXTERNAL_OES,
        GL_TEXTURE_EXTERNAL_OES);
    // texture_gles_->SetIntent(impeller::TextureIntent::kUploadFromHost);
    auto maybe_handle = texture_gles_->GetGLHandle();
    FML_CHECK(maybe_handle.has_value());
    if (maybe_handle.has_value()) {
      texture_id = maybe_handle.value();
    } else {
      texture_id = 0;
    }
  }
  Attach(static_cast<jint>(texture_id));
  FML_LOG(ERROR) << "attached tex id=" << texture_id;
  state_ = AttachmentState::kAttached;
}

void AndroidExternalTextureGL::Attach(jint textureName) {
  jni_facade_->SurfaceTextureAttachToGLContext(
      fml::jni::ScopedJavaLocalRef<jobject>(surface_texture_), textureName);
}

void AndroidExternalTextureGL::Update(PaintContext& context) {
  jni_facade_->SurfaceTextureUpdateTexImage(
      fml::jni::ScopedJavaLocalRef<jobject>(surface_texture_));
  UpdateTransform();
  FML_LOG(ERROR) << "AndroidExternalTextureGL::Update";
  if (texture_gles_ != nullptr) {
    external_image_ = impeller::DlImageImpeller::Make(texture_gles_);
  } else {
    GrGLTextureInfo textureInfo = {GL_TEXTURE_EXTERNAL_OES, texture_name_,
                                   GL_RGBA8_OES};
    GrBackendTexture backendTexture(1, 1, GrMipMapped::kNo, textureInfo);
    external_image_ = DlImage::Make(SkImages::BorrowTextureFrom(
        context.gr_context, backendTexture, kTopLeft_GrSurfaceOrigin,
        kRGBA_8888_SkColorType, kPremul_SkAlphaType, nullptr));
  }
}

void AndroidExternalTextureGL::Detach() {
  jni_facade_->SurfaceTextureDetachFromGLContext(
      fml::jni::ScopedJavaLocalRef<jobject>(surface_texture_));
}

void AndroidExternalTextureGL::OnTextureUnregistered() {}

}  // namespace flutter
