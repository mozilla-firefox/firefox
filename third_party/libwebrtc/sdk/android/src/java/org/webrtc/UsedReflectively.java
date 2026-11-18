/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * For internal use only.
 *
 * <p>This is meant for errorprone to understand that our {@link CalledByNative} and similar
 * annotations mean that the element they are annotating is called/accessed using JNI/reflection.
 * Errorprone only cares that those annotations are themselves meta-annotated with an annotation
 * with the simple name "UsedReflectively" hence this annotation.
 */
@Target({ElementType.ANNOTATION_TYPE})
@Retention(RetentionPolicy.CLASS)
@interface UsedReflectively {}