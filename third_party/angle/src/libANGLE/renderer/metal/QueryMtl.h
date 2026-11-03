//
// Copyright (c) 2020 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// QueryMtl.h:
//    Defines the class interface for QueryMtl, implementing QueryImpl.
//

#ifndef LIBANGLE_RENDERER_METAL_QUERYMTL_H_
#define LIBANGLE_RENDERER_METAL_QUERYMTL_H_

#include "libANGLE/renderer/QueryImpl.h"
#include "libANGLE/renderer/metal/mtl_common.h"
#include "libANGLE/renderer/metal/mtl_resources.h"

namespace rx
{

class ContextMtl;

class QueryMtl : public QueryImpl
{
  public:
    QueryMtl(gl::QueryType type);
    ~QueryMtl() override;

    void onDestroy(const gl::Context *context) override;

    angle::Result begin(const gl::Context *context) override;
    angle::Result end(const gl::Context *context) override;
    angle::Result queryCounter(const gl::Context *context) override;
    angle::Result getResult(const gl::Context *context, GLint *params) override;
    angle::Result getResult(const gl::Context *context, GLuint *params) override;
    angle::Result getResult(const gl::Context *context, GLint64 *params) override;
    angle::Result getResult(const gl::Context *context, GLuint64 *params) override;
    angle::Result isResultAvailable(const gl::Context *context, bool *available) override;

    // Returns the buffer containing the final occlusion query result.
    const mtl::BufferRef &getVisibilityResultBuffer() const { return mVisibilityResultBuffer; }

    void onTransformFeedbackEnd(const gl::Context *context);

    // If the timestamp query is still active upon context switch,
    // must set/unset the active timestamp query entry on the command
    // queue.
    void onContextMakeCurrent(const gl::Context *context);
    void onContextUnMakeCurrent(const gl::Context *context);

  private:
    template <typename T>
    angle::Result waitAndGetResult(const gl::Context *context, T *params);

    // Buffer holding the final combined visibility result for this query.
    mtl::BufferRef mVisibilityResultBuffer;

    size_t mTransformFeedbackPrimitivesDrawn = 0;

    uint64_t mTimeElapsedEntry = 0;
};

}  // namespace rx

#endif /* LIBANGLE_RENDERER_METAL_QUERYMTL_H_ */
