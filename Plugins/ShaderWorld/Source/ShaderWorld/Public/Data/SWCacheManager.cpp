#include "Data/SWCacheManager.h"
#include "SWorldSubsystem.h"

bool FSWRingCacheManager::WithinBudget()
{
	return true;
}

FSWTexture2DCacheManager::~FSWTexture2DCacheManager(){}

bool FSWTexture2DCacheManager::WithinBudget() { return true; }

/*
void FSWTexture2DCacheManager::CleanUpElement(FCacheElem& Element) {}


void FSWTexture2DCacheManager::ConstructElemData(UWorld* World, FCacheElem& Elem)
{
	UTextureRenderTarget2D* NewTexture = nullptr;
	SW_RT(NewTexture, World, Config.Dimension, Config.LayerFiltering, Config.Format)
	Elem.Data = NewTexture;
}*/