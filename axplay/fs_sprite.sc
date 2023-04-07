$input v_texcoord0,v_color0

/*
 * Copyright 2011-2023 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */
#include "../bgfx/examples/common/common.sh"

SAMPLER2D(u_tex0, 0);

void main()
{
    vec4 texColor = texture2D(u_tex0, v_texcoord0);
	gl_FragColor = texColor * v_color0;
}
