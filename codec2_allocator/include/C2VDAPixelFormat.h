/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef ANDROID_C2_VDA_PIXELFORMAT_H
#define ANDROID_C2_VDA_PIXELFORMAT_H

#include <v4l2_codec2/common/VideoTypes.h>

namespace android {

// Returns a supported pixel format from |crcb| and |semiplanar|.
// |crcb| indicates U and V are reversed. |semiplanar| is a pixel format is semiplanar, that is, it
// has two planes, not three planes.
HalPixelFormat resolveBufferFormat(bool crcb, bool semiplanar);

// Returns a pixel format of a buffer allocating by gralloc() with OMX_COLOR_FormatYUV420Flexible.
HalPixelFormat getPlatformPixelFormat();

}  // namespace android
#endif  // ANDROID_C2_VDA_PIXELFORMAT_H
