#ifdef NUM_BONE_INFLUENCES

qf_attribute vec4 a_BonesIndices, a_BonesWeights;
uniform vec4 u_DualQuats[MAX_UNIFORM_BONES*2];

#define DUAL_QUAT_TRANSFORM_TANGENT
#include "dualquat_overload.glsl"
#undef DUAL_QUAT_TRANSFORM_TANGENT
#include "dualquat_overload.glsl"

#endif // NUM_BONE_INFLUENCES

#ifdef APPLY_INSTANCED_TRANSFORMS

#if defined(APPLY_INSTANCED_ATTRIB_TRANSFORMS)
qf_attribute vec4 a_InstanceQuat, a_InstancePosAndScale;
#elif defined(GL_ARB_draw_instanced) || (defined(GL_ES) && (__VERSION__ >= 300))
uniform vec4 u_InstancePoints[MAX_UNIFORM_INSTANCES*2];
#define a_InstanceQuat u_InstancePoints[gl_InstanceID*2]
#define a_InstancePosAndScale u_InstancePoints[gl_InstanceID*2+1]
#else
uniform vec4 u_InstancePoints[2];
#define a_InstanceQuat u_InstancePoints[0]
#define a_InstancePosAndScale u_InstancePoints[1]
#endif // APPLY_INSTANCED_ATTRIB_TRANSFORMS

void InstancedTransform(inout vec4 Position, inout vec3 Normal)
{
	Position.xyz = (cross(a_InstanceQuat.xyz,
		cross(a_InstanceQuat.xyz, Position.xyz) + Position.xyz*a_InstanceQuat.w)*2.0 +
		Position.xyz) * a_InstancePosAndScale.w + a_InstancePosAndScale.xyz;
	Normal = cross(a_InstanceQuat.xyz, cross(a_InstanceQuat.xyz, Normal) + Normal*a_InstanceQuat.w)*2.0 + Normal;
}

#endif // APPLY_INSTANCED_TRANSFORMS

void TransformVerts(inout vec4 Position, inout vec3 Normal, inout vec2 TexCoord)
{
#ifdef NUM_BONE_INFLUENCES
	VertexDualQuatsTransform(Position, Normal);
#endif

#ifdef APPLY_DEFORMVERTS
	QF_DeformVerts(Position, Normal, TexCoord);
#endif

#ifdef APPLY_INSTANCED_TRANSFORMS
	InstancedTransform(Position, Normal);
#endif
}

void TransformVerts(inout vec4 Position, inout vec3 Normal, inout vec3 Tangent, inout vec2 TexCoord)
{
#ifdef NUM_BONE_INFLUENCES
	VertexDualQuatsTransform(Position, Normal, Tangent);
#endif

#ifdef APPLY_DEFORMVERTS
	QF_DeformVerts(Position, Normal, TexCoord);
#endif

#ifdef APPLY_INSTANCED_TRANSFORMS
	InstancedTransform(Position, Normal);
#endif
}
