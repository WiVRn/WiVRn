/*
 * Snapdragon™ Game Super Resolution
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * https://github.com/SnapdragonStudios/snapdragon-gsr
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#version 450

precision mediump float;
precision highp int;

#define OperationMode 1

layout (constant_id = 0) const bool use_edge_direction = true;
layout (constant_id = 1) const float edge_threshold = 4.0/255.0;
layout (constant_id = 2) const float edge_sharpness = 2.0;

layout(set = 0, binding = 0) uniform mediump sampler2D texSampler;

layout(location = 0) in highp vec2 inUV;

layout(location = 0) out vec4 outColor;

float fastLanczos2(float x)
{
	float wA = x-4.0;
	float wB = x*wA-wA;
	wA *= wA;
	return wB*wA;
}

vec2 weightY(float dx, float dy, float c, vec3 data)
{
	float std = data.x;
	vec2 dir = data.yz;

	float edgeDis = ((dx*dir.y)+(dy*dir.x));
	float x = (((dx*dx)+(dy*dy))+((edgeDis*edgeDis)*((clamp(((c*c)*std),0.0,1.0)*0.7)+-1.0)));

	float w = fastLanczos2(x);
	return vec2(w, w * c);	
}

vec2 weightYned(float dx, float dy, float c, float std)
{
	float x = ((dx*dx)+(dy* dy))* 0.55 + clamp(abs(c)*std, 0.0, 1.0);

	float w = fastLanczos2(x);
	return vec2(w, w * c);	
}

vec2 edgeDirection(vec4 left, vec4 right)
{
	vec2 dir;
	float RxLz = (right.x + (-left.z));
	float RwLy = (right.w + (-left.y));
	vec2 delta;
	delta.x = (RxLz + RwLy);
	delta.y = (RxLz + (-RwLy));
	float lengthInv = inversesqrt((delta.x * delta.x+ 3.075740e-05) + (delta.y * delta.y));
	dir.x = (delta.x * lengthInv);
	dir.y = (delta.y * lengthInv);
	return dir;
}

void main()
{
	vec4 color = textureLod(texSampler,inUV,0.0).xyzw;
	float alpha = color.a;

	if (OperationMode == 1)
		color.a = 0.0;

	if (OperationMode != 4)
	{
		vec2 dim = textureSize(texSampler, 0);
		vec4 viewportInfo = vec4(1/dim.x, 1/dim.y, dim.x, dim.y);

		highp vec2 imgCoord = ((inUV.xy*viewportInfo.zw)+vec2(-0.5,0.5));
		highp vec2 imgCoordPixel = floor(imgCoord);
		highp vec2 coord = (imgCoordPixel*viewportInfo.xy);
		vec2 pl = (imgCoord+(-imgCoordPixel));
		vec4 left = textureGather(texSampler,coord, OperationMode);

		float edgeVote = abs(left.z - left.y) + abs(color[OperationMode] - left.y)  + abs(color[OperationMode] - left.z) ;
		if(edgeVote > edge_threshold)
		{
			coord.x += viewportInfo.x;

			vec4 right = textureGather(texSampler,coord + vec2(viewportInfo.x, 0.0), OperationMode);
			vec4 upDown;
			upDown.xy = textureGather(texSampler,coord + vec2(0.0, -viewportInfo.y),OperationMode).wz;
			upDown.zw  = textureGather(texSampler,coord + vec2(0.0, viewportInfo.y), OperationMode).yx;

			float mean = (left.y+left.z+right.x+right.w)*0.25;
			left = left - vec4(mean);
			right = right - vec4(mean);
			upDown = upDown - vec4(mean);
			color.w =color[OperationMode] - mean;

			float sum = (((((abs(left.x)+abs(left.y))+abs(left.z))+abs(left.w))+(((abs(right.x)+abs(right.y))+abs(right.z))+abs(right.w)))+(((abs(upDown.x)+abs(upDown.y))+abs(upDown.z))+abs(upDown.w)));
			float sumMean = 1.014185e+01/sum;
			float std = (sumMean*sumMean);

			vec2 aWY;
			if (use_edge_direction) {
				vec3 data = vec3(std, edgeDirection(left, right));
				aWY = weightY(pl.x, pl.y+1.0, upDown.x,data);
				aWY += weightY(pl.x-1.0, pl.y+1.0, upDown.y,data);
				aWY += weightY(pl.x-1.0, pl.y-2.0, upDown.z,data);
				aWY += weightY(pl.x, pl.y-2.0, upDown.w,data);
				aWY += weightY(pl.x+1.0, pl.y-1.0, left.x,data);
				aWY += weightY(pl.x, pl.y-1.0, left.y,data);
				aWY += weightY(pl.x, pl.y, left.z,data);
				aWY += weightY(pl.x+1.0, pl.y, left.w,data);
				aWY += weightY(pl.x-1.0, pl.y-1.0, right.x,data);
				aWY += weightY(pl.x-2.0, pl.y-1.0, right.y,data);
				aWY += weightY(pl.x-2.0, pl.y, right.z,data);
				aWY += weightY(pl.x-1.0, pl.y, right.w,data);
			} else {
				aWY = weightYned(pl.x, pl.y+1.0, upDown.x,std);
				aWY += weightYned(pl.x-1.0, pl.y+1.0, upDown.y,std);
				aWY += weightYned(pl.x-1.0, pl.y-2.0, upDown.z,std);
				aWY += weightYned(pl.x, pl.y-2.0, upDown.w,std);
				aWY += weightYned(pl.x+1.0, pl.y-1.0, left.x,std);
				aWY += weightYned(pl.x, pl.y-1.0, left.y,std);
				aWY += weightYned(pl.x, pl.y, left.z,std);
				aWY += weightYned(pl.x+1.0, pl.y, left.w,std);
				aWY += weightYned(pl.x-1.0, pl.y-1.0, right.x,std);
				aWY += weightYned(pl.x-2.0, pl.y-1.0, right.y,std);
				aWY += weightYned(pl.x-2.0, pl.y, right.z,std);
				aWY += weightYned(pl.x-1.0, pl.y, right.w,std);
			}

			float finalY = aWY.y/aWY.x;
			float maxY = max(max(left.y,left.z),max(right.x,right.w));
			float minY = min(min(left.y,left.z),min(right.x,right.w));
			float deltaY = clamp(edge_sharpness*finalY, minY, maxY) -color.w;

			//smooth high contrast input
			deltaY = clamp(deltaY, -23.0 / 255.0, 23.0 / 255.0);

			color.x = clamp((color.x+deltaY),0.0,1.0);
			color.y = clamp((color.y+deltaY),0.0,1.0);
			color.z = clamp((color.z+deltaY),0.0,1.0);
		}
	}

    outColor = vec4(color.rgb, alpha);
}
