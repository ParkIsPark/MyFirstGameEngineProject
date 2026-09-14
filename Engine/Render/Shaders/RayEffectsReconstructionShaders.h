#pragma once

namespace RayEffectsReconstructionShaders
{
inline constexpr const char* FullscreenVertex = R"GLSL(#version 330 core
out vec2 vUV;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);vUV=p;gl_Position=vec4(p*2.0-1.0,0,1);}
)GLSL";

inline constexpr const char* AccumulateFragment = R"GLSL(#version 330 core
in vec2 vUV;layout(location=0)out vec4 oColor;
uniform sampler2D uCurrent,uHistory;uniform float uCurrentWeight;uniform bool uReset;
void main(){vec4 c=texture(uCurrent,vUV);oColor=uReset?c:mix(texture(uHistory,vUV),c,uCurrentWeight);}
)GLSL";

inline constexpr const char* EdgeAwareFragment = R"GLSL(#version 330 core
in vec2 vUV;layout(location=0)out vec4 oColor;
uniform sampler2D uInput,uDepth,uGeometricNormal;uniform usampler2D uIdentity;
uniform ivec2 uDirection;uniform int uStep,uAtrous;
float edgeWeight(ivec2 center,ivec2 samplePixel){
 uvec2 ci=texelFetch(uIdentity,center,0).rg,si=texelFetch(uIdentity,samplePixel,0).rg;
 if(ci.x==0u)return all(equal(center,samplePixel))?1.:0.;
 if(any(notEqual(ci,si)))return 0.;
 float cd=texelFetch(uDepth,center,0).r,sd=texelFetch(uDepth,samplePixel,0).r;
 if(abs(sd-cd)>max(.01,.02*abs(cd)))return 0.;
 vec3 cn=normalize(texelFetch(uGeometricNormal,center,0).xyz),sn=normalize(texelFetch(uGeometricNormal,samplePixel,0).xyz);
 float nd=dot(cn,sn);if(nd<.8)return 0.;return pow(nd,32.);
}
void main(){
 ivec2 size=textureSize(uInput,0),p=clamp(ivec2(vUV*vec2(size)),ivec2(0),size-1);
 vec4 sum=vec4(0);float total=0.;
 if(uAtrous!=0){
  const float k[3]=float[3](.375,.25,.0625);
  for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x){ivec2 q=clamp(p+ivec2(x,y)*uStep,ivec2(0),size-1);float w=k[abs(x)]*k[abs(y)]*edgeWeight(p,q);sum+=texelFetch(uInput,q,0)*w;total+=w;}
 }else{
  const float k[3]=float[3](.375,.25,.0625);
  for(int i=-2;i<=2;++i){ivec2 q=clamp(p+uDirection*i*uStep,ivec2(0),size-1);float w=k[abs(i)]*edgeWeight(p,q);sum+=texelFetch(uInput,q,0)*w;total+=w;}
 }
 oColor=total>0.?sum/total:texelFetch(uInput,p,0);
}
)GLSL";
} // namespace RayEffectsReconstructionShaders
