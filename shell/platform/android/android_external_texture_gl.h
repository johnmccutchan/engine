// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_ANDROID_EXTERNAL_TEXTURE_GL_H_
#define FLUTTER_SHELL_PLATFORM_ANDROID_EXTERNAL_TEXTURE_GL_H_

#include <GLES/gl.h>

#include "flutter/common/graphics/texture.h"
#include "flutter/impeller/renderer/backend/gles/context_gles.h"
#include "flutter/impeller/renderer/backend/gles/handle_gles.h"
#include "flutter/impeller/renderer/backend/gles/reactor_gles.h"
#include "flutter/shell/platform/android/platform_view_android_jni_impl.h"
#include "impeller/renderer/backend/gles/texture_gles.h"

namespace flutter {

class AndroidExternalTextureGL : public flutter::Texture {
 public:
  AndroidExternalTextureGL(
      int64_t id,
      const fml::jni::ScopedJavaGlobalRef<jobject>& surface_texture,
      std::shared_ptr<PlatformViewAndroidJNI> jni_facade,
      const std::shared_ptr<impeller::ContextGLES>& impeller_context);

  ~AndroidExternalTextureGL() override;

  void Paint(PaintContext& context,
             const SkRect& bounds,
             bool freeze,
             const DlImageSampling sampling) override;

  void OnGrContextCreated() override;

  void OnGrContextDestroyed() override;

  void MarkNewFrameAvailable() override;

  void OnTextureUnregistered() override;

 private:
  void Inititialize(int width, int height);
  void Attach(jint textureName);

  void Update(PaintContext& context);

  void Detach();

  void UpdateTransform();

  enum class AttachmentState { kUninitialized, kAttached, kDetached };

  std::shared_ptr<PlatformViewAndroidJNI> jni_facade_;
  fml::jni::ScopedJavaGlobalRef<jobject> surface_texture_;
  std::shared_ptr<impeller::ContextGLES> impeller_context_;

  AttachmentState state_ = AttachmentState::kUninitialized;

  bool new_frame_ready_ = false;
  SkMatrix transform_;
  sk_sp<DlImage> external_image_;

  // NOTE: Only populated when running under impeller.
  std::shared_ptr<impeller::TextureGLES> texture_gles_;
  // NOTE: Only populated when not running under impeller.
  GLuint texture_name_ = 0;

  FML_DISALLOW_COPY_AND_ASSIGN(AndroidExternalTextureGL);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_ANDROID_EXTERNAL_TEXTURE_GL_H_
