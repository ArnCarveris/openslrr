$input a_position, a_normal, a_texcoord0, a_texcoord1, a_texcoord2, a_color0, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_normal, v_texcoord0, v_texcoord1, v_texcoord2, v_color0, v_staticShadow, v_worldPos, v_tint

#include <bgfx_shader.sh>

uniform mat4 u_staticShadowMtx;

void main()
{
	mat4 world = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	vec4 position = mul(world, vec4(a_position, 1.0));
	gl_Position = mul(u_viewProj, position);
	v_normal = mul(world, vec4(a_normal, 0.0)).xyz;
	v_texcoord0 = a_texcoord0;
	v_texcoord1 = a_texcoord1;
	v_texcoord2 = a_texcoord2;
	v_color0 = a_color0;
	v_tint = i_data4;
	// Night overlays (#264): the same 2 mm normal offset as vs_mesh. The flag
	// rides in the instance colour's w (2 for overlays, 1 otherwise): a
	// u_alphaTest uniform in this shader made the harness render thread ~45 ms per frame.
	float surfaceNormalLength = dot(v_normal, v_normal);
	if (i_data4.w > 1.5 && surfaceNormalLength > 1e-8) {
		vec4 overlay = position;
		overlay.xyz += v_normal * (0.002 / sqrt(surfaceNormalLength));
		gl_Position = mul(u_viewProj, overlay);
	}
	v_worldPos = position.xyz; // Unbiased physical position for lighting.
	float nl = dot(v_normal, v_normal);
	if (nl > 1e-8)
		position.xyz += v_normal * (0.05 / sqrt(nl));
	v_staticShadow = mul(u_staticShadowMtx, position).xyz;
}
