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

#include "Math/Vector.isph"
#include "Math/Quat.isph"
#include "Math/Transform.isph"

/*
 * B-G-R-A
 */
struct SWFColor
{
#if EXPLICIT_VECTOR == 0
    uint8 V[4];
#else
    uint8<4> V;
#endif   
};

export void ShaderWorld_SetRedirectionData(
        uniform const int ElementCount,
        uniform const int InstanceOffset,
        uniform const int InstanceIndexSource[],
        uniform const int IndexToSpawnableIndex,
        uniform const int Variety,
        uniform int Redirection_Indexes[],
        uniform int Index_ToSpawnable[],
        uniform int IndexToVariety[])
{
    foreach (i = 0 ... ElementCount) 
      {
        Redirection_Indexes[InstanceOffset + i] = InstanceIndexSource[i];
        Index_ToSpawnable[InstanceOffset + i] = IndexToSpawnableIndex;
        IndexToVariety[InstanceOffset + i] = Variety;
      }
}
