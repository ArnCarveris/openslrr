// Shared by scenery and vehicle materials. Sampler stages are assigned by callers.
// x: enabled, y: texel size, z: depth bias, w: 0 sun / 1 local lamp.
uniform vec4 u_staticShadowParams;
uniform vec4 u_staticShadowEye;
float sceneShadowVisibility(vec3 shadowPosition, vec3 worldPosition)
{
	float visibility = 1.0;
	if (u_staticShadowParams.x > 0.0) {
		vec3 p = shadowPosition;
		float edge = min(min(p.x, 1.0 - p.x), min(p.y, 1.0 - p.y));
		float range = u_staticShadowEye.w;
		float distanceToEye = length(worldPosition - u_staticShadowEye.xyz);
		if (distanceToEye < range && edge > u_staticShadowParams.y && p.z > 0.0 && p.z < 1.0) {
			float texel = u_staticShadowParams.y;
			// Measure slope in shadow UV space, not screen pixels. A receiver
			// also present in the depth pass needs bias across the PCF footprint.
			vec3 dx = dFdx(shadowPosition);
			vec3 dy = dFdy(shadowPosition);
			float det = dx.x * dy.y - dx.y * dy.x;
			vec2 gradient = vec2(0.0, 0.0);
			if (abs(det) > 1e-10)
				gradient = vec2(dy.y * dx.z - dx.y * dy.z,
				                dx.x * dy.z - dy.x * dx.z) / det;
			float slope = dot(abs(gradient), vec2(texel * 2.0, texel * 2.0));
			float depth = p.z - min(u_staticShadowParams.z + slope, 0.004);
			// Hardware 2x2 compare. A point-sampled disk drew the shadow
			// map's own texels as stripes on the garage apron.
			vec2 o = vec2(texel * 1.5, texel * 0.5);
			float visible = (
				shadow2D(s_staticShadow, vec3(p.xy + vec2(-o.x, -o.y), depth)) +
				shadow2D(s_staticShadow, vec3(p.xy + vec2( o.y, -o.x), depth)) +
				shadow2D(s_staticShadow, vec3(p.xy + vec2( o.x,  o.y), depth)) +
				shadow2D(s_staticShadow, vec3(p.xy + vec2(-o.y,  o.x), depth))
			) * 0.25;
			visibility -= (1.0 - visible) * u_staticShadowParams.x *
			              (1.0 - smoothstep(range * 0.2, range, distanceToEye)) *
			              smoothstep(0.0, 0.22, edge);
		}
	}
	return visibility;
}
