//
// Copyright (c) 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// SurfaceGL.cpp: OpenGL implementation of egl::Surface

#include "libANGLE/renderer/gl/SurfaceGL.h"

#include "libANGLE/renderer/gl/FramebufferGL.h"
#include "libANGLE/renderer/gl/RendererGL.h"

namespace rx
{

SurfaceGL::SurfaceGL(const egl::SurfaceState &state, RendererGL *renderer)
    : SurfaceImpl(state), mRenderer(renderer)
{
}

SurfaceGL::~SurfaceGL()
{
}

FramebufferImpl *SurfaceGL::createDefaultFramebuffer(const gl::FramebufferState &data)
{
    return new FramebufferGL(data, mRenderer->getFunctions(), mRenderer->getStateManager(),
                             mRenderer->getWorkarounds(), mRenderer->getBlitter(), true);
}
}
