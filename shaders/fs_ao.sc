$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_aoDepth,0);
SAMPLER2D(s_aoInput,1);
uniform vec4 u_aoScreen; // inverse depth size, projection scale
uniform vec4 u_aoProjection; // projection A/B, homogeneous depth, bottom-left texture
uniform vec4 u_aoConfig; // method, quality, world radius
uniform vec4 u_aoParams; // pass, inverse AO size
const float PI=3.14159265359;
float depthAt(vec2 uv) {return texture2DLod(s_aoDepth,uv,0.0).r;}
float linearDepth(float d) {
    float ndc=u_aoProjection.z>0.5?d*2.0-1.0:d;
    float divisor=ndc-u_aoProjection.x;
    return u_aoProjection.y/(abs(divisor)>1e-8?divisor:-1e-8);
}
vec3 positionAt(vec2 uv) {
    // Nearest-depth sampling and position reconstruction must refer to the
    // same texel centre. Subpixel rays with a quantised depth invent slopes
    // even on a perfectly planar receiver.
    uv=clamp((floor(uv/u_aoScreen.xy)+vec2(0.5,0.5))*u_aoScreen.xy,
             u_aoScreen.xy*0.5,vec2(1.0,1.0)-u_aoScreen.xy*0.5);
    float z=linearDepth(depthAt(uv));
    vec2 ndc=uv*2.0-1.0;
    if(u_aoProjection.w<0.5) ndc.y=-ndc.y;
    return vec3(ndc/u_aoScreen.zw*z,z);
}
vec3 surfaceNormal(vec2 uv,vec3 p) {
    vec2 t=u_aoScreen.xy;
    vec3 l=positionAt(clamp(uv-vec2(t.x,0.0),vec2(0.0,0.0),vec2(1.0,1.0)));
    vec3 r=positionAt(clamp(uv+vec2(t.x,0.0),vec2(0.0,0.0),vec2(1.0,1.0)));
    vec3 d=positionAt(clamp(uv-vec2(0.0,t.y),vec2(0.0,0.0),vec2(1.0,1.0)));
    vec3 u=positionAt(clamp(uv+vec2(0.0,t.y),vec2(0.0,0.0),vec2(1.0,1.0)));
    vec3 dx=abs(l.z-p.z)<abs(r.z-p.z)?p-l:r-p;
    vec3 dy=abs(d.z-p.z)<abs(u.z-p.z)?p-d:u-p;
    vec3 n=cross(dx,dy);float n2=dot(n,n);
    n=n2>1e-12?n*inversesqrt(n2):normalize(-p);
    return dot(n,-p)<0.0?-n:n;
}
vec3 sampleDelta(vec2 uv,vec3 p) {
    if(any(lessThan(uv,vec2(0.0,0.0))) || any(greaterThan(uv,vec2(1.0,1.0))) || depthAt(uv)>=0.999999)
        return vec3(0.0,0.0,0.0);
    return positionAt(uv)-p;
}
float hbao(vec2 uv,vec3 p,vec3 n,vec2 radiusUv,float rotation) {
    int directions=u_aoConfig.y<0.5?4:u_aoConfig.y<1.5?6:8;
    int steps=u_aoConfig.y<0.5?3:u_aoConfig.y<1.5?4:6;
    float sum=0.0,r2=u_aoConfig.z*u_aoConfig.z;
    for(int i=0;i<8;++i) {
        if(i>=directions) break;
        float angle=2.0*PI*(float(i)+rotation)/float(directions);
        vec2 direction=vec2(cos(angle),sin(angle))*radiusUv;
        float horizon=0.05;
        for(int k=0;k<6;++k) {
            if(k>=steps) break;
            float distance=(float(k)+0.75)/float(steps);distance*=distance;
            vec3 delta=sampleDelta(uv+direction*distance,p);
            float d2=dot(delta,delta);
            if(d2<0.0001 || d2>r2) continue;
            float elevation=max(dot(n,delta*inversesqrt(d2)),0.0);
            float next=max(horizon,elevation);
            sum+=(next-horizon)*saturate(1.0-d2/r2);
            horizon=next;
        }
    }
    return saturate(1.0-sum*2.0/float(directions));
}
// Ground-truth AO slice integral (Jimenez et al. 2016), independently expressed
// for bgfx. The projected normal and two horizon angles define each cosine-
// weighted visible arc. No vendor SDK, temporal history or deinterleaving.
float gtao(vec2 uv,vec3 p,vec3 n,vec2 radiusUv,float rotation) {
    int slices=u_aoConfig.y<0.5?2:u_aoConfig.y<1.5?3:4;
    int steps=u_aoConfig.y<0.5?4:u_aoConfig.y<1.5?6:8;
    vec3 view=normalize(-p);float visibility=0.0,r2=u_aoConfig.z*u_aoConfig.z;
    for(int i=0;i<4;++i) {
        if(i>=slices) break;
        float phi=PI*(float(i)+rotation)/float(slices);
        vec2 screenDirection=vec2(cos(phi),sin(phi));
        vec3 direction=vec3(screenDirection.x,u_aoProjection.w>0.5?screenDirection.y:-screenDirection.y,0.0);
        vec3 tangent=direction-dot(direction,view)*view;
        vec3 axis=cross(tangent,view);axis*=inversesqrt(max(dot(axis,axis),1e-8));
        vec3 projected=n-axis*dot(n,axis);
        float lengthN=max(length(projected),0.0001);
        float cosine=clamp(dot(projected,view)/lengthN,0.0,1.0);
        float normalAngle=(dot(tangent,projected)<0.0?-1.0:1.0)*acos(cosine);
        float low0=cos(normalAngle+PI*0.5),low1=cos(normalAngle-PI*0.5);
        float h0=low0,h1=low1;
        for(int k=0;k<8;++k) {
            if(k>=steps) break;
            float distance=(float(k)+0.75)/float(steps);distance*=distance;
            vec2 offset=screenDirection*radiusUv*distance;
            vec3 a=sampleDelta(uv+offset,p),b=sampleDelta(uv-offset,p);
            float a2=dot(a,a),b2=dot(b,b);
            if(a2>0.0001 && a2<r2) h0=max(h0,mix(low0,dot(a*inversesqrt(a2),view),saturate(1.0-a2/r2)));
            if(b2>0.0001 && b2<r2) h1=max(h1,mix(low1,dot(b*inversesqrt(b2),view),saturate(1.0-b2/r2)));
        }
        float a0=clamp(-acos(clamp(h1,-1.0,1.0)),normalAngle-PI*0.5,normalAngle+PI*0.5);
        float a1=clamp( acos(clamp(h0,-1.0,1.0)),normalAngle-PI*0.5,normalAngle+PI*0.5);
        float integral0=(cosine+2.0*a0*sin(normalAngle)-cos(2.0*a0-normalAngle))*0.25;
        float integral1=(cosine+2.0*a1*sin(normalAngle)-cos(2.0*a1-normalAngle))*0.25;
        // Normalise the finite-slice quadrature against its unobstructed
        // hemisphere. A tilted plane must remain fully visible at every tier.
        float open0=normalAngle-PI*0.5,open1=normalAngle+PI*0.5;
        float baseline=(2.0*cosine+2.0*(open0+open1)*sin(normalAngle)
                       -cos(2.0*open0-normalAngle)-cos(2.0*open1-normalAngle))*0.25;
        visibility+=clamp((integral0+integral1)/max(baseline,0.0001),0.0,1.0);
    }
    return clamp(visibility/float(slices),0.03,1.0);
}
void main() {
    vec2 uv=v_texcoord0;
    float depth=depthAt(uv);
    if(depth>=0.999999) {gl_FragColor=vec4(1.0,1.0,1.0,1.0);return;}
    vec2 receiverUv=clamp((floor(uv/u_aoScreen.xy)+vec2(0.5,0.5))*u_aoScreen.xy,
                          u_aoScreen.xy*0.5,vec2(1.0,1.0)-u_aoScreen.xy*0.5);
    vec3 p=positionAt(receiverUv);
    if(u_aoParams.x>0.5) {
        vec2 direction=u_aoParams.x<1.5?vec2(u_aoParams.y,0.0):vec2(0.0,u_aoParams.z);
        float sum=0.0,weight=0.0;
        for(int k=-2;k<=2;++k) {
            vec2 q=clamp(uv+direction*float(k),vec2(0.0,0.0),vec2(1.0,1.0));
            if(depthAt(q)>=0.999999) continue;
            float z=linearDepth(depthAt(q));
            float spatial=k==0?0.375:abs(k)==1?0.25:0.0625;
            float w=spatial*exp2(-abs(z-p.z)/max(u_aoConfig.z*0.08,0.02));
            sum+=texture2DLod(s_aoInput,q,0.0).r*w;weight+=w;
        }
        float result=weight>0.0001?sum/weight:1.0;
        gl_FragColor=vec4(result,result,result,1.0);return;
    }
    vec3 n=surfaceNormal(receiverUv,p);
    vec2 radiusUv=u_aoConfig.z*u_aoScreen.zw/(2.0*max(p.z,0.05));
    radiusUv=min(radiusUv,u_aoScreen.xy*96.0);
    float rotation=fract(52.9829189*fract(dot(floor(uv/u_aoParams.yz),vec2(0.06711056,0.00583715))));
    float visibility=u_aoConfig.x<1.5?hbao(receiverUv,p,n,radiusUv,rotation):gtao(receiverUv,p,n,radiusUv,rotation);
    gl_FragColor=vec4(visibility,visibility,visibility,1.0);
}
