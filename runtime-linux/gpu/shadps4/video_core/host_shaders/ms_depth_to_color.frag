// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450 core
#extension GL_EXT_samplerless_texture_functions : require

// Resolves a multi-sample depth image into a single-sample color image.
// Source is bound with aspect=Depth; destination is any Rxf color format
// whose R channel will receive the depth value. G/B left at 0, A at 1 so
// a shader that reads this as a sampler2D and swizzles to .rrrr or .r
// sees the expected depth value either way.
//
// Sample 0 is representative — averaging all samples would be wrong for
// depth-based effects that want a specific visibility decision rather than
// an intermediate value.

layout (binding = 0, set = 0) uniform texture2DMS depth_ms;

layout (location = 0) out vec4 out_color;

void main()
{
    const float d = texelFetch(depth_ms, ivec2(gl_FragCoord.xy), 0).r;
    out_color = vec4(d, 0.0, 0.0, 1.0);
}
