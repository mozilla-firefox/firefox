//
// Copyright 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// QueryGL.cpp: Implements the class methods for QueryGL.

#include "libANGLE/renderer/gl/QueryGL.h"

#include "common/debug.h"
#include "libANGLE/Context.h"
#include "libANGLE/renderer/gl/ContextGL.h"
#include "libANGLE/renderer/gl/FunctionsGL.h"
#include "libANGLE/renderer/gl/StateManagerGL.h"
#include "libANGLE/renderer/gl/renderergl_utils.h"

namespace
{

GLuint64 MergeQueryResults(gl::QueryType type, GLuint64 currentResult, GLuint64 newResult)
{
    switch (type)
    {
        case gl::QueryType::AnySamples:
        case gl::QueryType::AnySamplesConservative:
            return (currentResult == GL_TRUE || newResult == GL_TRUE) ? GL_TRUE : GL_FALSE;

        case gl::QueryType::TransformFeedbackPrimitivesWritten:
            return currentResult + newResult;

        case gl::QueryType::TimeElapsed:
            return currentResult + newResult;

        case gl::QueryType::Timestamp:
            return newResult;

        case gl::QueryType::PrimitivesGenerated:
            return currentResult + newResult;

        default:
            UNREACHABLE();
            return 0;
    }
}

// Some drivers tend to hang when flushing pending queries.  Wait until this number of queries have
// added up before checking if results are ready.
constexpr uint32_t kPauseResumeFlushThreshold = 5;
}  // anonymous namespace

namespace rx
{

QueryGL::QueryGL(gl::QueryType type) : QueryImpl(type) {}

QueryGL::~QueryGL() {}

StandardQueryGL::StandardQueryGL(gl::QueryType type,
                                 const FunctionsGL *functions,
                                 StateManagerGL *stateManager)
    : QueryGL(type),
      mFunctions(functions),
      mStateManager(stateManager),
      mActiveQuery(0),
      mPendingQueries(),
      mResultSum(0),
      mDeferredBufferDeletionSerial(0)
{}

StandardQueryGL::~StandardQueryGL()
{
    clearInternalQueries();
}

void StandardQueryGL::clearInternalQueries()
{
    if (mActiveQuery != 0)
    {
        mStateManager->endQuery(mType, this, mActiveQuery);
        mFunctions->deleteQueries(1, &mActiveQuery);
        mActiveQuery = 0;
    }

    while (!mPendingQueries.empty())
    {
        GLuint id = mPendingQueries.front();
        mFunctions->deleteQueries(1, &id);
        mPendingQueries.pop_front();
    }
}

angle::Result StandardQueryGL::begin(const gl::Context *context)
{
    clearInternalQueries();
    mResultSum                    = 0;
    mDeferredBufferDeletionSerial = mStateManager->onBeginQuery();
    return resume(context);
}

angle::Result StandardQueryGL::end(const gl::Context *context)
{
    return pause(context);
}

angle::Result StandardQueryGL::queryCounter(const gl::Context *context)
{
    ASSERT(mType == gl::QueryType::Timestamp);

    // Directly create a query for the timestamp and add it to the pending query queue, as timestamp
    // queries do not have the traditional begin/end block and never need to be paused/resumed
    GLuint query;
    mFunctions->genQueries(1, &query);
    mFunctions->queryCounter(query, GL_TIMESTAMP);
    mPendingQueries.push_back(query);

    return angle::Result::Continue;
}

template <typename T>
angle::Result StandardQueryGL::getResultBase(const gl::Context *context, T *params)
{
    ASSERT(mActiveQuery == 0);

    ANGLE_TRY(flush(context, true));
    ASSERT(mPendingQueries.empty());
    *params = static_cast<T>(mResultSum);

    return angle::Result::Continue;
}

angle::Result StandardQueryGL::getResult(const gl::Context *context, GLint *params)
{
    return getResultBase(context, params);
}

angle::Result StandardQueryGL::getResult(const gl::Context *context, GLuint *params)
{
    return getResultBase(context, params);
}

angle::Result StandardQueryGL::getResult(const gl::Context *context, GLint64 *params)
{
    return getResultBase(context, params);
}

angle::Result StandardQueryGL::getResult(const gl::Context *context, GLuint64 *params)
{
    return getResultBase(context, params);
}

angle::Result StandardQueryGL::isResultAvailable(const gl::Context *context, bool *available)
{
    ASSERT(mActiveQuery == 0);

    ANGLE_TRY(flush(context, false));
    *available = mPendingQueries.empty();
    return angle::Result::Continue;
}

angle::Result StandardQueryGL::pause(const gl::Context *context)
{
    if (mActiveQuery != 0)
    {
        mStateManager->endQuery(mType, this, mActiveQuery);

        mPendingQueries.push_back(mActiveQuery);
        mActiveQuery = 0;
    }

    // Flush to make sure the pending queries don't add up too much.
    if (mPendingQueries.size() >= kPauseResumeFlushThreshold)
    {
        ANGLE_TRY(flush(context, false));
    }

    return angle::Result::Continue;
}

angle::Result StandardQueryGL::resume(const gl::Context *context)
{
    if (mActiveQuery == 0)
    {
        // Flush to make sure the pending queries don't add up too much.
        if (mPendingQueries.size() >= kPauseResumeFlushThreshold)
        {
            ANGLE_TRY(flush(context, false));
        }

        mFunctions->genQueries(1, &mActiveQuery);
        mStateManager->beginQuery(mType, this, mActiveQuery);

        ContextGL *contextGL = GetImplAs<ContextGL>(context);
        contextGL->markWorkSubmitted();
    }

    return angle::Result::Continue;
}

angle::Result StandardQueryGL::flush(const gl::Context *context, bool force)
{
    while (!mPendingQueries.empty())
    {
        GLuint id = mPendingQueries.front();
        if (!force)
        {
            GLuint resultAvailable = 0;
            mFunctions->getQueryObjectuiv(id, GL_QUERY_RESULT_AVAILABLE, &resultAvailable);
            if (resultAvailable == GL_FALSE)
            {
                return angle::Result::Continue;
            }
        }

        // Even though getQueryObjectui64v was introduced for timer queries, there is nothing in the
        // standard that says that it doesn't work for any other queries. It also passes on all the
        // trybots, so we use it if it is available
        if (mFunctions->getQueryObjectui64v != nullptr)
        {
            GLuint64 result = 0;
            mFunctions->getQueryObjectui64v(id, GL_QUERY_RESULT, &result);
            mResultSum = MergeQueryResults(mType, mResultSum, result);
        }
        else
        {
            GLuint result = 0;
            mFunctions->getQueryObjectuiv(id, GL_QUERY_RESULT, &result);
            mResultSum = MergeQueryResults(mType, mResultSum, static_cast<GLuint64>(result));
        }

        mFunctions->deleteQueries(1, &id);

        mPendingQueries.pop_front();
    }

    if (mActiveQuery == 0 && mDeferredBufferDeletionSerial > 0)
    {
        mStateManager->onSerialCompleted(mDeferredBufferDeletionSerial);
    }

    return angle::Result::Continue;
}
}  // namespace rx
