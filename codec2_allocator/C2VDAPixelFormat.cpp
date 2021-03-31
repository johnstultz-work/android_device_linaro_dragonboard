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

// #define LOG_NDEBUG 0
#include "C2VDAPixelFormat.h"

#include <C2Buffer.h>
#include <C2PlatformSupport.h>

#include <android/hardware/graphics/common/1.0/types.h>
#include <utils/Log.h>

#include <algorithm>
#include <memory>

using android::hardware::graphics::common::V1_0::BufferUsage;

namespace android {

namespace {

// The HAL pixel format information supported by Android flexible YUV format.
struct SupportedPixelFormat {
    bool mCrcb;
    bool mSemiplanar;
    HalPixelFormat mPixelFormat;
};

constexpr int kDummyBufferDimension = 32;

constexpr SupportedPixelFormat kSupportedPixelFormats[] = {
        // {mCrcb, mSemiplanar, mPixelFormat}
        {false, true, HalPixelFormat::NV12},
        {true, false, HalPixelFormat::YV12},
        // Add more buffer formats when needed
};

}  // namespace

HalPixelFormat resolveBufferFormat(bool crcb, bool semiplanar) {
    auto value = std::find_if(std::begin(kSupportedPixelFormats), std::end(kSupportedPixelFormats),
                              [crcb, semiplanar](const struct SupportedPixelFormat& f) {
                                  return f.mCrcb == crcb && f.mSemiplanar == semiplanar;
                              });
    LOG_ALWAYS_FATAL_IF(value == std::end(kSupportedPixelFormats),
                        "Unsupported pixel format: (crcb=%d, semiplanar=%d)", crcb, semiplanar);
    return value->mPixelFormat;
}

HalPixelFormat getPlatformPixelFormat() {
    static HalPixelFormat platform_pixel_format = HalPixelFormat::YCBCR_420_888;
    if (platform_pixel_format != HalPixelFormat::YCBCR_420_888) {
        return platform_pixel_format;
    }

    std::shared_ptr<C2BlockPool> blockPool;
    constexpr C2BlockPool::local_id_t bufferPoolId = C2BlockPool::BASIC_GRAPHIC;
    c2_status_t err = GetCodec2BlockPool(bufferPoolId, nullptr, &blockPool);
    if (err != C2_OK) {
        ALOGE("Graphic block allocator is invalid (err=%d).", static_cast<int>(err));
        return HalPixelFormat::UNKNOWN;
    }
    C2MemoryUsage usage(C2MemoryUsage::CPU_READ, static_cast<uint64_t>(BufferUsage::VIDEO_DECODER));
    std::shared_ptr<C2GraphicBlock> block;
    err = blockPool->fetchGraphicBlock(kDummyBufferDimension, kDummyBufferDimension,
                                       static_cast<uint32_t>(HalPixelFormat::YCBCR_420_888), usage,
                                       &block);
    if (err != C2_OK) {
        ALOGE("fetch graphic block is failed (err=%d).", static_cast<int>(err));
        return HalPixelFormat::UNKNOWN;
    }

    C2ConstGraphicBlock constBlock =
            block->share(C2Rect(block->width(), block->height()), C2Fence());
    const C2GraphicView& view = constBlock.map().get();
    const uint8_t* const* data = view.data();
    const C2PlanarLayout& layout = view.layout();

    // get offset from data pointers
    uint32_t offsets[C2PlanarLayout::MAX_NUM_PLANES];
    auto baseAddress = reinterpret_cast<intptr_t>(data[0]);
    for (uint32_t i = 0; i < layout.numPlanes; ++i) {
        auto planeAddress = reinterpret_cast<intptr_t>(data[i]);
        offsets[i] = static_cast<uint32_t>(planeAddress - baseAddress);
    }

    bool crcb = (layout.numPlanes == 3 &&
                 offsets[C2PlanarLayout::PLANE_U] > offsets[C2PlanarLayout::PLANE_V]);
    bool semiplanar = layout.planes[C2PlanarLayout::PLANE_U].colInc == 2;
    platform_pixel_format = resolveBufferFormat(crcb, semiplanar);
    return platform_pixel_format;
}

}  // namespace android
