$input v_texcoord0,v_color0

/*
 * Copyright 2011-2023 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */
#include "../bgfx/examples/common/common.sh"
#include "./color_utils.h"

SAMPLER2D(u_tex0, 0);
SAMPLER2D(u_tex1, 1); 

uniform mat4 colorTransform;

void main()
{
    vec3 YUV;
    
    YUV.x = texture2D(u_tex0, v_texcoord0).x; // Y
    YUV.yz = texture2D(u_tex1, v_texcoord0).xy; // CbCr

    /* Convert YUV to RGB */
    vec4 OutColor;
    OutColor.xyz = yuv2rgb(YUV, colorTransform);
    OutColor.w = 1.0;

	gl_FragColor = OutColor * v_color0;
}
