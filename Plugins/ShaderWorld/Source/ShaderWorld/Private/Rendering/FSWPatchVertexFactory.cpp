/*
 * ShaderWorld: A procedural framework.
 * Website : https://www.shader.world/
 * Copyright (c) 2021-2023 MONSIEUR MAXIME DUPART
 *
 * This content is provided under the license of :
 * Epic Content License Agreement - https://www.unrealengine.com/en-US/eula/content
 *
 * You may not Distribute Licensed Content in source format to third parties except to employees,
 * affiliates, and contractors who are utilizing the Licensed Content in good faith to develop a Project
 * on your behalf. Those employees, affiliates, and contractors you share Licensed Content
 * with are not permitted to further Distribute the Licensed Content (including as incorporated in a Project)
 * and must delete the Licensed Content once it is no longer needed for developing a Project on your behalf.
 * You are responsible for ensuring that any employees, affiliates, or contractors you share Licensed Content
 * with comply with the terms of this Agreement.
 *
 * General Restrictions - You may not:
 * i. attempt to reverse engineer, decompile, translate, disassemble, or derive source code from Licensed Content;
 * ii. sell, rent, lease, or transfer Licensed Content on a “stand-alone basis”
 * (Projects must reasonably add value beyond the value of the Licensed Content,
 * and the Licensed Content must be merely a component of the Project and not the primary focus of the Project);
 *
 */

 /*
  * Main authors: Maxime Dupart (https://twitter.com/Max_Dupt)
  */

#include "Rendering/FSWPatchVertexFactory.h"
#include "Engine/TextureRenderTarget2D.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FSWPatchParameters, "SWPatchData");


IMPLEMENT_TYPE_LAYOUT(FSWPatchVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FSWPatchVertexFactoryMorphing, SF_Vertex, FSWPatchVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FSWPatchVertexFactoryMorphing, SF_Pixel, FSWPatchVertexFactoryShaderParameters);

#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FSWPatchVertexFactoryMorphing, SF_RayHitGroup, FSWPatchVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FSWPatchVertexFactoryMorphing, SF_Compute, FSWPatchVertexFactoryShaderParameters);
#endif // RHI_RAYTRACING


IMPLEMENT_TEMPLATE_VERTEX_FACTORY_TYPE(template<>, FSWPatchVertexFactoryMorphing, "/ShaderWorld/SWPatchVertexFactory.ush",

	EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsDynamicLighting
	| EVertexFactoryFlags::SupportsPrecisePrevWorldPos
	| EVertexFactoryFlags::SupportsPositionOnly
	| EVertexFactoryFlags::SupportsCachingMeshDrawCommands
	| EVertexFactoryFlags::SupportsPrimitiveIdStream
	| EVertexFactoryFlags::SupportsRayTracing
	| EVertexFactoryFlags::SupportsRayTracingDynamicGeometry
	//| EVertexFactoryFlags::SupportsPSOPrecaching
	
);

//IMPLEMENT_TYPE_LAYOUT(FSWPatchVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FSWPatchVertexFactoryNoMorphing, SF_Vertex, FSWPatchVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FSWPatchVertexFactoryNoMorphing, SF_Pixel, FSWPatchVertexFactoryShaderParameters);

#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FSWPatchVertexFactoryNoMorphing, SF_RayHitGroup, FSWPatchVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FSWPatchVertexFactoryNoMorphing, SF_Compute, FSWPatchVertexFactoryShaderParameters);
#endif // RHI_RAYTRACING


IMPLEMENT_TEMPLATE_VERTEX_FACTORY_TYPE(template<>, FSWPatchVertexFactoryNoMorphing, "/ShaderWorld/SWPatchVertexFactory.ush",


	EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsDynamicLighting
	| EVertexFactoryFlags::SupportsPrecisePrevWorldPos
	| EVertexFactoryFlags::SupportsPositionOnly
	| EVertexFactoryFlags::SupportsCachingMeshDrawCommands
	| EVertexFactoryFlags::SupportsPrimitiveIdStream
	| EVertexFactoryFlags::SupportsRayTracing
	| EVertexFactoryFlags::SupportsRayTracingDynamicGeometry
	//| EVertexFactoryFlags::SupportsPSOPrecaching
	
);
/*
EVertexFactoryFlags::UsedWithMaterials
| EVertexFactoryFlags::SupportsStaticLighting
| EVertexFactoryFlags::SupportsDynamicLighting
| EVertexFactoryFlags::SupportsPrecisePrevWorldPos
| EVertexFactoryFlags::SupportsPositionOnly
| EVertexFactoryFlags::SupportsCachingMeshDrawCommands
| EVertexFactoryFlags::SupportsPrimitiveIdStream
| EVertexFactoryFlags::SupportsRayTracing
| EVertexFactoryFlags::SupportsRayTracingDynamicGeometry
| EVertexFactoryFlags::SupportsLightmapBaking
| EVertexFactoryFlags::SupportsManualVertexFetch
| EVertexFactoryFlags::SupportsPSOPrecaching
| EVertexFactoryFlags::SupportsGPUSkinPassThrough
*/

