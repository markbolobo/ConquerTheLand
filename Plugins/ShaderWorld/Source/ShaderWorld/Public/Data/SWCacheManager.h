#pragma once
#include "SWStats.h"
#include "Engine/TextureRenderTarget2D.h"


/*
 * General Cache Manager Template
 */

struct FSWCacheManager
{
public:
	struct FCacheElem
	{
		int32 ID = -1;
		FIntVector Location = FIntVector(0, 0, 0);
		int32 Data = -1;

		bool operator ==(const FCacheElem& Rhs) const{
			return ID == Rhs.ID;
		}
		bool operator ==(const FCacheElem& Rhs)
		{
			return static_cast<const FCacheElem&>(*this) == Rhs;			
		}
	};	

	inline FCacheElem& GetACacheElem();
	inline void ReleaseCacheElem(int ID);
	inline void AssignDataElement(int ID, int DataID) { CacheElem[ID].Data = DataID;}

	virtual bool WithinBudget() = 0;
	

	inline virtual void CleanUp();

	void ForEachElement(TFunction<void(const FCacheElem&)> Task) const
	{
		for (auto& El : CacheElem)
			Task(El);
	}
	void ForEachElement(TFunction<void(const FCacheElem&)> Task)
	{
		static_cast<const FSWCacheManager&>(*this).ForEachElement(Task);
	}

	/*
	 * The default constructor should not be used, but having it delete prevent ustruct compilations.
	 */
	FSWCacheManager() = default;
	virtual ~FSWCacheManager() = default;

	TMap<FIntVector, int32 > CacheLayout;
	TArray<FCacheElem> CacheElem;
	TArray<int> AvailableCacheElem;
	TArray<int> UsedCacheElem;

	TArray<int32> UnInitializedDataElem;
};

typename FSWCacheManager::FCacheElem& FSWCacheManager::GetACacheElem()
{
	if (AvailableCacheElem.Num() > 0)
	{
		FCacheElem& Elem = CacheElem[AvailableCacheElem[AvailableCacheElem.Num() - 1]];
		UsedCacheElem.Add(Elem.ID);
		AvailableCacheElem.RemoveAt(AvailableCacheElem.Num() - 1);
		return Elem;
	}

	FCacheElem& NewElem = CacheElem.AddDefaulted_GetRef();
	NewElem.ID = CacheElem.Num() - 1;

	UnInitializedDataElem.Add(NewElem.ID);

	UsedCacheElem.Add(NewElem.ID);

	return NewElem;
}

void FSWCacheManager::ReleaseCacheElem(int ID)
{
	check(ID < CacheElem.Num())

	AvailableCacheElem.Add(ID);
	UsedCacheElem.Remove(ID);
	CacheLayout.Remove(CacheElem[ID].Location);
}

void FSWCacheManager::CleanUp()
{
	UnInitializedDataElem.Empty();

	CacheElem.Empty();
	AvailableCacheElem.Empty();
	UsedCacheElem.Empty();
	CacheLayout.Empty();
}

/*
 * Ring Cache Manager
 */

struct FSWRingCacheManager : public FSWCacheManager
{
	FSWRingCacheManager(int32 InRingCount)
		: FSWCacheManager()
		, RingCount(InRingCount)
	{}

	int32 RingCount = 0;

	virtual bool WithinBudget() override;

	void ReleaseBeyondRange(TSet< FIntVector >& ReferencePoints)
	{
		if (ReferencePoints.Num() <= 0)
			return;

		FIntVector RefPoint(0);
		for(const auto& Pt : ReferencePoints)
		{
			RefPoint = Pt;
			break;
		}

		for (int i = this->UsedCacheElem.Num() - 1; i >= 0; --i)
		{
			FCacheElem& El = this->CacheElem[this->UsedCacheElem[i]];

			FVector ToCacheElement = FVector(El.Location - RefPoint);

			if (FMath::Abs(ToCacheElement.X) > (RingCount + 0.1f) ||
				FMath::Abs(ToCacheElement.Y) > (RingCount + 0.1f) ||
				FMath::Abs(ToCacheElement.Z) > (RingCount + 0.1f) )
			{
				this->ReleaseCacheElem(El.ID);
			}
		}
	}

	TArray<int32> CollectWork(TSet< FIntVector >& ReferencePoints)
	{
		TArray<int32> Work;

		if (ReferencePoints.Num() > 0)
		{
			FIntVector RefPoint(0);
			for (const auto& Pt : ReferencePoints)
			{
				RefPoint = Pt;
				break;
			}

			bool InterruptUpdate = false;

			for (int r = 0; r <= RingCount; r++)
			{
				if (InterruptUpdate)
					break;

				for (int i = -r; i <= r; i++)
				{
					if (InterruptUpdate)
						break;

					for (int j = -r; j <= r; j++)
					{
						if (abs(j) != r && abs(i) != r)
							continue;

						FIntVector CacheLocation = RefPoint + FIntVector(i, j, 0);

						if (!CacheLayout.Contains(CacheLocation))
						{							
							if (WithinBudget())
							{
								FSWCacheManager::FCacheElem& Elem = GetACacheElem();

								Elem.Location = CacheLocation;

								Work.Add(Elem.ID);
								CacheLayout.Add(CacheLocation, Elem.ID);
							}
							else
							{
								InterruptUpdate = true;
								break;
							}							
						}
					}
				}
			}
		}

		return MoveTemp(Work);
	}
};


/*
 * Specialized Texture Ring Cache
 */

struct FSWTextureSettings
{
	/*
	 * Texture Settings
	 */
	int32 Dimension = 0;
	TEnumAsByte<enum TextureFilter> LayerFiltering = TextureFilter::TF_Nearest;
	TEnumAsByte<ETextureRenderTargetFormat> Format = ETextureRenderTargetFormat::RTF_RGBA8;
};

struct FSWTexture2DCacheManager : public FSWRingCacheManager
{
	FSWTexture2DCacheManager(int32 RingCount, int32 Dimensions, TEnumAsByte<enum TextureFilter> LayerFiltering, TEnumAsByte<ETextureRenderTargetFormat> Format)
		:FSWRingCacheManager(RingCount)
	{}

	
	virtual ~FSWTexture2DCacheManager() override;
	
	//virtual void ConstructElemData(UWorld* World, FCacheElem& Elem) override;
	//virtual void CleanUpElement(FCacheElem& Element) override;

	virtual bool WithinBudget() override;

};



