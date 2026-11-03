//
// Copyright 2026 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// ObjectMap.h: Defines the egl::ObjectMap template class for thread-safe EGL object maps.

#ifndef LIBANGLE_OBJECT_MAP_H_
#define LIBANGLE_OBJECT_MAP_H_

#include <type_traits>
#include <vector>

#include "common/FastVector.h"
#include "common/PackedEnums.h"
#include "common/SimpleMutex.h"
#include "common/hash_containers.h"
#include "libANGLE/Error.h"

namespace egl
{
namespace priv
{

template <typename ContextMutexType>
class ContextMapImpl;

template <typename ObjectType, typename MutexType = angle::SimpleMutex>
class ObjectMap : angle::NonCopyable
{
  public:
    using Map            = angle::HashMap<GLuint, ObjectType *>;
    using ResourceVector = angle::FastVector<ObjectType *, 8>;

    ObjectMap()  = default;
    ~ObjectMap() = default;

    bool empty() const
    {
        std::lock_guard<MutexType> lock(mMutex);
        return mObjects.empty();
    }

    ObjectType *first() const
    {
        std::lock_guard<MutexType> lock(mMutex);
        return mObjects.empty() ? nullptr : mObjects.begin()->second;
    }

    ObjectType *find(typename ResourceTypeToID<ObjectType>::IDType handle) const
    {
        std::lock_guard<MutexType> lock(mMutex);
        auto iter = mObjects.find(handle.value);
        return iter != mObjects.end() ? iter->second : nullptr;
    }

    void insert(typename ResourceTypeToID<ObjectType>::IDType handle, ObjectType *object)
    {
        std::lock_guard<MutexType> lock(mMutex);
        mObjects.insert(std::pair(handle.value, object));
    }

    bool erase(typename ResourceTypeToID<ObjectType>::IDType handle)
    {
        std::lock_guard<MutexType> lock(mMutex);
        auto iter = mObjects.find(handle.value);
        if (iter != mObjects.end())
        {
            mObjects.erase(iter);
            return true;
        }
        return false;
    }

    ResourceVector extractAll()
    {
        std::lock_guard<MutexType> lock(mMutex);
        ResourceVector objects;
        objects.reserve(mObjects.size());
        for (const auto &pair : mObjects)
        {
            objects.push_back(pair.second);
        }
        mObjects.clear();
        return objects;
    }

    template <typename Callback>
    auto forEach(Callback &&callback) const
    {
        using ReturnType             = std::invoke_result_t<Callback, ObjectType *>;
        constexpr bool kReturnsError = std::is_same_v<ReturnType, egl::Error> ||
                                       std::is_same_v<ReturnType, angle::Result> ||
                                       std::is_same_v<ReturnType, bool>;

        using ErrorType = std::conditional_t<kReturnsError, ReturnType, egl::Error>;

        auto invoke = [&](ObjectType *object) -> ErrorType {
            if constexpr (kReturnsError)
            {
                return callback(object);
            }
            else
            {
                callback(object);
                return egl::NoError();
            }
        };

        ErrorType result = []() {
            if constexpr (std::is_same_v<ErrorType, angle::Result>)
            {
                return angle::Result::Continue;
            }
            else if constexpr (std::is_same_v<ErrorType, bool>)
            {
                return true;
            }
            else
            {
                return egl::NoError();
            }
        }();

        if constexpr (std::is_same_v<MutexType, angle::NoOpMutex>)
        {
            // This lock really is a no-op. It is there to satisfy the template
            // requirement.
            std::lock_guard<MutexType> lock(mMutex);
            for (const auto &pair : mObjects)
            {
                if (!IsError(result))
                {
                    result = invoke(pair.second);
                }
            }
        }
        else
        {
            // Collect objects while holding the lock to avoid holding it for the entire
            // duration of the callback which may end up with deadlock if callback ends up calling
            // into this ObjectMap.
            ResourceVector objects;
            {
                std::lock_guard<MutexType> lock(mMutex);
                objects.reserve(mObjects.size());
                for (const auto &pair : mObjects)
                {
                    pair.second->addRef();
                    objects.push_back(pair.second);
                }
            }

            for (ObjectType *object : objects)
            {
                if (!IsError(result))
                {
                    result = invoke(object);
                }
                object->release();
            }
        }

        if constexpr (kReturnsError)
        {
            return result;
        }
    }

  protected:
    template <typename ContextMutexType>
    friend class ContextMapImpl;

    mutable MutexType mMutex;
    Map mObjects;
};

}  // namespace priv
}  // namespace egl

#endif  // LIBANGLE_OBJECT_MAP_H_
