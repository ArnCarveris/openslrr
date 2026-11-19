// Independent interior daylight field; ambient is never multiplied by it.
uniform mat4 u_interiorSunMtx;
uniform vec4 u_interiorSunDir; // toward sun, atlas texel size
uniform vec4 u_interiorSunColor; // direct radiance, enabled
float interiorSunVisibility(vec3 world)
{
    if(u_interiorSunColor.w<0.5) return 0.0;
    vec3 p=mul(u_interiorSunMtx,vec4(world,1.0)).xyz;
    if(any(lessThanEqual(p,vec3(0.0,0.0,0.0))) || any(greaterThanEqual(p,vec3(1.0,1.0,1.0)))) return 0.0;
    float bias=0.000015;
    float t=u_interiorSunDir.w*0.5;
    return (shadow2D(s_interiorSun,vec3(p.xy+vec2(t,t),p.z-bias))+
            shadow2D(s_interiorSun,vec3(p.xy+vec2(-t,t),p.z-bias))+
            shadow2D(s_interiorSun,vec3(p.xy+vec2(t,-t),p.z-bias))+
            shadow2D(s_interiorSun,vec3(p.xy-vec2(t,t),p.z-bias)))*0.25;
}
vec3 interiorSunDiffuse(vec3 normal,vec3 world)
{
    return u_interiorSunColor.rgb*max(dot(normal,normalize(u_interiorSunDir.xyz)),0.0)*interiorSunVisibility(world);
}
// Shared world-space visibility; stages are assigned by each material caller.
// x enabled, y atlas texel, z single-field bias, w sun(0)/local(1).
uniform vec4 u_staticShadowParams;
uniform vec4 u_staticShadowEye;
uniform mat4 u_shadowCascades[4];
uniform vec4 u_shadowOrigin;
uniform vec4 u_shadowRanges; // x=0 selects single/local field, otherwise radial splits.
uniform mat4 u_pointShadowFaces[6];
uniform vec4 u_pointShadowParams; // enabled, inv atlas width/height, bias metres
uniform vec4 u_pointLightColor; // source colour and engine intensity
vec3 localLightRadiance(vec3 delta,float range,bool punctual)
{
    float d2=dot(delta,delta);
    float fade=saturate(1.0-d2/(range*range));
    float attenuation=fade*fade/(1.0+d2);
    if(punctual) {
        float window=saturate(1.0-pow(d2/(range*range),2.0));
        attenuation=window*window/max(d2,0.25);
    }
    return u_pointLightColor.rgb*(u_pointLightColor.w*attenuation);
}
float pointShadowVisibility(vec3 world)
{
    vec3 delta=world-u_shadowOrigin.xyz;
    float distance=length(delta);
    if(distance<0.05) return 1.0;
    vec3 a=abs(delta);
    int face=a.x>=a.y && a.x>=a.z ? (delta.x>=0.0?0:1)
           : a.y>=a.z ? (delta.y>=0.0?2:3) : (delta.z>=0.0?4:5);
    vec3 biased=delta*(max(distance-u_pointShadowParams.w,0.0)/distance);
    vec4 clip=mul(u_pointShadowFaces[face],vec4(biased,1.0));
    if(clip.w<=0.0) return 1.0;
    vec3 p=clip.xyz/clip.w;
    if(any(lessThan(p,vec3(0.0,0.0,0.0))) || any(greaterThan(p,vec3(1.0,1.0,1.0)))) return 1.0;
    vec2 scale=vec2(1.0/3.0,0.5);
    float row=step(2.5,float(face));
    vec2 tile=vec2((float(face)-row*3.0)/3.0,row*0.5);
    vec2 texel=u_pointShadowParams.yz;
    vec2 uv=clamp(tile+p.xy*scale,tile+texel*1.5,tile+scale-texel*1.5);
    vec2 offset=texel*0.5;
    return (shadow2D(s_staticShadow,vec3(uv+offset,p.z))+
            shadow2D(s_staticShadow,vec3(uv-offset,p.z))+
            shadow2D(s_staticShadow,vec3(uv+vec2(offset.x,-offset.y),p.z))+
            shadow2D(s_staticShadow,vec3(uv+vec2(-offset.x,offset.y),p.z)))*0.25;
}
float receiverBias(vec3 p,float bias,float texel)
{
    vec3 dx=dFdx(p),dy=dFdy(p);
    float det=dx.x*dy.y-dx.y*dy.x;
    vec2 gradient=vec2(0.0,0.0);
    if(abs(det)>1e-10) gradient=vec2(dy.y*dx.z-dx.y*dy.z,dx.x*dy.z-dy.x*dx.z)/det;
    return min(bias+dot(abs(gradient),vec2(texel*2.0,texel*2.0)),0.004);
}
float shadowTapField(vec3 p, vec2 tile, float scale, float bias)
{
    float edge=min(min(p.x,1.0-p.x),min(p.y,1.0-p.y));
    float texel=u_staticShadowParams.y/scale;
    if(edge<=texel*2.0 || p.z<=0.0 || p.z>=1.0) return 1.0;
    float depth=p.z-bias;
    vec2 uv=p.xy*scale+tile;
    // Symmetric tent PCF: a smooth footprint for thin poles/cables moving
    // across texels as sunlight changes. Hardware compare remains bilinear.
    float t=u_staticShadowParams.y*1.5;
    float result=shadow2D(s_staticShadow,vec3(uv,depth))*4.0;
    result+=(shadow2D(s_staticShadow,vec3(uv+vec2(t,0.0),depth))+
             shadow2D(s_staticShadow,vec3(uv-vec2(t,0.0),depth))+
             shadow2D(s_staticShadow,vec3(uv+vec2(0.0,t),depth))+
             shadow2D(s_staticShadow,vec3(uv-vec2(0.0,t),depth)))*2.0;
    result+=shadow2D(s_staticShadow,vec3(uv+vec2(t,t),depth))+
            shadow2D(s_staticShadow,vec3(uv+vec2(t,-t),depth))+
            shadow2D(s_staticShadow,vec3(uv+vec2(-t,t),depth))+
            shadow2D(s_staticShadow,vec3(uv-vec2(t,t),depth));
    return result/16.0;
}
float cascadeVisibility(vec3 world, float distanceToEye)
{
    // Evaluate coordinates outside distance branches to keep their derivatives coherent.
    vec4 w=vec4(world-u_shadowOrigin.xyz,1.0);
    vec3 p0=mul(u_shadowCascades[0],w).xyz;
    vec3 p1=mul(u_shadowCascades[1],w).xyz;
    vec3 p2=mul(u_shadowCascades[2],w).xyz;
    vec3 p3=mul(u_shadowCascades[3],w).xyz;
    // Bias represents 2 cm along the light depth, independent of cascade size.
    vec4 bias=vec4(.02,.02,.02,.02)/(2.0*(u_shadowRanges*1.1+vec4(768.0,768.0,768.0,768.0)));
    float texel=u_staticShadowParams.y*2.0;
    bias=vec4(receiverBias(p0,bias.x,texel),receiverBias(p1,bias.y,texel),
              receiverBias(p2,bias.z,texel),receiverBias(p3,bias.w,texel));
    // Select only the current and adjacent level; do not sample every tile.
    float s1=step(u_shadowRanges.x,distanceToEye);
    float s2=step(u_shadowRanges.y,distanceToEye);
    float s3=step(u_shadowRanges.z,distanceToEye);
    vec3 primary=mix(p0,p1,s1);
    primary=mix(primary,p2,s2);primary=mix(primary,p3,s3);
    vec3 adjacent=mix(p1,p2,s1);adjacent=mix(adjacent,p3,s2);
    vec2 tile=mix(vec2(0.0,0.0),vec2(.5,0.0),s1);
    tile=mix(tile,vec2(0.0,.5),s2);tile=mix(tile,vec2(.5,.5),s3);
    vec2 nextTile=mix(vec2(.5,0.0),vec2(0.0,.5),s1);
    nextTile=mix(nextTile,vec2(.5,.5),s2);
    float primaryBias=mix(bias.x,bias.y,s1);
    primaryBias=mix(primaryBias,bias.z,s2);primaryBias=mix(primaryBias,bias.w,s3);
    float nextBias=mix(bias.y,bias.z,s1);nextBias=mix(nextBias,bias.w,s2);
    float radius=mix(u_shadowRanges.x,u_shadowRanges.y,s1);
    radius=mix(radius,u_shadowRanges.z,s2);
    float blend=smoothstep(radius*.85,radius,distanceToEye)*(1.0-s3);
    float a=shadowTapField(primary,tile,.5,primaryBias);
    float next=shadowTapField(adjacent,nextTile,.5,nextBias);
    float result=mix(a,next,blend);
    return mix(result,1.0,smoothstep(u_shadowRanges.w*.9,u_shadowRanges.w,distanceToEye));
}
float sceneShadowVisibility(vec3 p,vec3 world)
{
    if(u_staticShadowParams.x<=0.0) return 1.0;
    if(u_pointShadowParams.x>0.5) return pointShadowVisibility(world);
    float distanceToEye=length(world-u_staticShadowEye.xyz);
    if(u_shadowRanges.x>0.0)
        return mix(1.0,cascadeVisibility(world,distanceToEye),u_staticShadowParams.x);
    float edge=min(min(p.x,1.0-p.x),min(p.y,1.0-p.y));
    // A fitted interior field (w=0) covers its complete footprint. The
    // projection/tap boundary already rejects points outside that field.
    float fade=1.0;
    if(u_staticShadowEye.w>0.0)
        fade=(1.0-smoothstep(u_staticShadowEye.w*.2,u_staticShadowEye.w,distanceToEye))*smoothstep(0.0,.22,edge);
    float visible=shadowTapField(p,vec2(0.0,0.0),1.0,receiverBias(p,u_staticShadowParams.z,u_staticShadowParams.y));
    return mix(1.0,visible,u_staticShadowParams.x*fade);
}
