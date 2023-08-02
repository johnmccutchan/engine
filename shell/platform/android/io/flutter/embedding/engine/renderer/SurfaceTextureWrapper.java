// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package io.flutter.embedding.engine.renderer;

import android.graphics.SurfaceTexture;
import androidx.annotation.Keep;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import io.flutter.Log;

/**
 * A wrapper for a SurfaceTexture that tracks whether the texture has been released.
 *
 * <p>The engine calls {@code SurfaceTexture.release} on the platform thread, but {@code
 * updateTexImage} is called on the raster thread. This wrapper will prevent {@code updateTexImage}
 * calls on an abandoned texture.
 */
@Keep
public class SurfaceTextureWrapper {
  private SurfaceTexture surfaceTexture;
  private boolean released;
  private boolean attached;
  private Runnable onFrameConsumed;

  public SurfaceTextureWrapper(@NonNull SurfaceTexture surfaceTexture) {
    this(surfaceTexture, null);
  }

  /**
   * A wrapper for a SurfaceTexture.
   *
   * <p>The provided {@code onFrameConsumed} callback must be invoked when the most recent image was
   * consumed.
   *
   * @param onFrameConsumed The callback after the {@code updateTexImage} is called.
   */
  public SurfaceTextureWrapper(
      @NonNull SurfaceTexture surfaceTexture, @Nullable Runnable onFrameConsumed) {
    this.surfaceTexture = surfaceTexture;
    this.released = false;
    this.onFrameConsumed = onFrameConsumed;
  }

  @NonNull
  public SurfaceTexture surfaceTexture() {
    return surfaceTexture;
  }

  // Called by native.
  @SuppressWarnings("unused")
  public void updateTexImage() {
    synchronized (this) {
      if (!released) {
        surfaceTexture.updateTexImage();
        Log.e("SurfaceTextureWrapper", "Data space = " + surfaceTexture.getDataSpace());
        if (onFrameConsumed != null) {
          onFrameConsumed.run();
        }
      }
    }
  }

  public void release() {
    synchronized (this) {
      if (!released) {
        surfaceTexture.release();
        released = true;
        attached = false;
      }
    }
  }

  // Called by native.
  @SuppressWarnings("unused")
  public void attachToGLContext(int texName) {
    synchronized (this) {
      if (released) {
        return;
      }
      // When the rasterizer tasks run on a different thread, the GrContext is re-created.
      // This causes the texture to be in an uninitialized state.
      // This should *not* be an issue once platform views are always rendered as TextureLayers
      // since thread merging will be always disabled on Android.
      // For more see: AndroidExternalTextureGL::OnGrContextCreated in
      // android_external_texture_gl.cc, and
      // https://github.com/flutter/flutter/issues/98155
      if (attached) {
        surfaceTexture.detachFromGLContext();
      }
      surfaceTexture.attachToGLContext(texName);
      attached = true;
    }
  }

  // Called by native.
  @SuppressWarnings("unused")
  public void detachFromGLContext() {
    synchronized (this) {
      if (attached && !released) {
        surfaceTexture.detachFromGLContext();
        attached = false;
      }
    }
  }

  // Called by native.
  @SuppressWarnings("unused")
  public void getTransformMatrix(@NonNull float[] mtx) {
    surfaceTexture.getTransformMatrix(mtx);
  }
}
