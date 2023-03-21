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

#include "Component/SWFoliageInstancedSMeshComponent.h"

#include "SWStats.h"
#include "Actor/ShaderWorldActor.h"
#include "Engine/InstancedStaticMesh.h"


USWCollectableInstancedSMeshComponent::USWCollectableInstancedSMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}


bool USWCollectableInstancedSMeshComponent::RemoveInstance(int32 InstanceIndex)
{
	FTransform CurrentT;
	if(GetInstanceTransform(InstanceIndex, CurrentT, false))
	{
		CurrentT.SetScale3D(FVector(0.f, 0.f, 0.f));
		
		if (Visual_Mesh_Owner)
		{
			/*
			{
				const int32 RenderIndex = Visual_Mesh_Owner->GetRenderIndex(Redirection->Redirection_Indexes[InstanceIndex]);
				const FMatrix OldTransform = Visual_Mesh_Owner->PerInstanceSMData[Redirection->Redirection_Indexes[InstanceIndex]].Transform;
				const FTransform NewLocalTransform = CurrentT;
				const FVector NewLocalLocation = NewLocalTransform.GetTranslation();

				// if we are only updating rotation/scale we update the instance directly in the cluster tree
				const bool bIsOmittedInstance = (RenderIndex == INDEX_NONE);
				const bool bIsBuiltInstance = !bIsOmittedInstance && RenderIndex < Visual_Mesh_Owner->NumBuiltRenderInstances;
				const bool bDoInPlaceUpdate = bIsBuiltInstance && NewLocalLocation.Equals(OldTransform.GetOrigin()) && (Visual_Mesh_Owner->PerInstanceRenderData.IsValid() && PerInstanceRenderData->InstanceBuffer.RequireCPUAccess);

				FString deb = "";
				deb += bIsOmittedInstance? "bIsOmittedInstance True |":"bIsOmittedInstance False |";
				deb += bIsBuiltInstance ? "bIsBuiltInstance True |" : "bIsBuiltInstance False |";
				deb += bDoInPlaceUpdate ? "bDoInPlaceUpdate True |" : "bDoInPlaceUpdate False |";
				if(bDoInPlaceUpdate)
				{
					SW_LOG("Visual_Mesh_Owner bDoInPlaceUpdate %s", *deb)
				}
				else
				{
					SW_LOG("Visual_Mesh_Owner NOT bDoInPlaceUpdate %s", *deb)
				}
			}*/
			if(InstanceIndex>=Redirection->Redirection_Indexes.Num())
			{
				SW_LOG("NOPE InstanceIndex %d >= Redirection->Redirection_Indexes.Num() %d", InstanceIndex, Redirection->Redirection_Indexes.Num())
				return false;
			}

			Visual_Mesh_Owner->UpdateInstanceTransform(Redirection->Redirection_Indexes[InstanceIndex], CurrentT, false, true, true);
			Visual_Mesh_Owner->bAutoRebuildTreeOnInstanceChanges = false;

			// Need to access Mesh.InstancesT->Transforms and set scale to zero to this element,
			// Otherwise any adjustments might make it appear again
			// Notably, we remove/collect instance at proximity so it shouldn't adjust anymore,
			// Unless we moved so fast to this location that the latest spawnable transform isn't the most accurate possible 

			int32 SpawnableMeshID = Redirection->ColIndexToSpawnableIndex[InstanceIndex];
			int32 SpawnableVarietyID = Redirection->ColIndexToVarietyIndex[InstanceIndex];
			
			if (AShaderWorldActor* OwningActor = Cast<AShaderWorldActor>(GetOwner()))
			{
				if (OwningActor->CollisionToSpawnable.Contains(this))
				{
					FSpawnableMesh* Spawn = (*OwningActor->CollisionToSpawnable.Find(this));
					FSpawnableMeshElement& Mesh = Spawn->SpawnablesElem[SpawnableMeshID];

					// We have the global index id as far as HISM is concerned: Redirection_Indexes[InstanceIndex]
					// We need to convert it to a local index
					// Index Global - Offset of index in current Compute Mesh Element
					const int32 LocalIndexOfAsset = Redirection->Redirection_Indexes[InstanceIndex] - Mesh.InstanceOffset[SpawnableVarietyID];
					Mesh.InstancesT->Transforms[SpawnableVarietyID][LocalIndexOfAsset].Transform = Mesh.InstancesT->Transforms[SpawnableVarietyID][LocalIndexOfAsset].Transform.ApplyScale(0.0);// SetScale3D(FVector(0.f));
					//UE_LOG(LogTemp,Warning,TEXT("InstancesT Adjust SpawnableID %d SpawnableMeshID %d SpawnableVarietyID %d InstanceIndex %d LocalIndexOfAsset %d"), SpawnableID, SpawnableMeshID, SpawnableVarietyID, InstanceIndex, LocalIndexOfAsset);

				}
			}			
		}

		UpdateInstanceTransform(InstanceIndex, CurrentT, false, true, true);
		return true;
	}
	
	return false;
}