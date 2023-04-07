
vec3 yuv2rgb(vec3 YUV, mat4 colorTrans)
{
	mat3 coeff = mat3(
		colorTrans[0].xyz,
		colorTrans[1].xyz,
		colorTrans[2].xyz
	);

	// Offset in last column of matrix
	YUV -=  vec3(colorTrans[0].w, colorTrans[1].w, colorTrans[2].w);
	return mul(coeff, YUV);
}
