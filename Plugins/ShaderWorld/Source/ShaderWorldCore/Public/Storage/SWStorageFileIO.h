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

#pragma once

#include "CoreMinimal.h"


#if 0
/** Base for all importer settings */
struct SHADERWORLDCORE_API FSWStorageBasicImportSettings
{
public:
	/** Holds a flag determining whether the same settings should be applied to the whole import at once */
	bool bImportAll;

protected:
	/** Used to determine the correct Handler to use during serialization. */
	FString Filename;

public:
	FSWStorageBasicImportSettings(const FString& Filename)
		: bImportAll(false)
		, Filename(Filename)
	{
	}
	virtual ~FSWStorageBasicImportSettings() {}

	/**
	 * Should return true if the given file is compatible with this instance of settings
	 * Useful for detecting different headers
	 */
	virtual bool IsFileCompatible(const FString& InFilename) const { return false; }

	/**
	 * Links the FLidarPointCloudImportSettings with FArchive serialization
	 * No need to manually serialize Filename - it is handled by ULidarPointCloudFileIO::SerializeImportSettings
	 */
	virtual void Serialize(FArchive& Ar);

	FString GetFilename() const { return Filename; }

	/** Returns true it this is an instance of FLidarPointCloudImportSettings */
	bool IsGeneric() const { return GetUID().Equals(FSWStorageBasicImportSettings("").GetUID()); }

	virtual void SetNewFilename(const FString& NewFilename)
	{
		Filename = NewFilename;
	}

	/** Must return a unique id of this Import Settings type. */
	virtual FString GetUID() const { return "FSWStorageBasicImportSettings"; }

	/**
	 * Returns duplicate of this instance of Import Settings.
	 * Optionally, can use a different filename when cloning.
	 */
	virtual TSharedPtr<FSWStorageBasicImportSettings> Clone(const FString& NewFilename = "") { return nullptr; }

	static TSharedPtr<FSWStorageBasicImportSettings> MakeGeneric(const FString& Filename)
	{
		return TSharedPtr<FSWStorageBasicImportSettings>(new FSWStorageBasicImportSettings(Filename));
	}

#if WITH_EDITOR
	/** Used to create properties window. */
	virtual TSharedPtr<SWidget> GetWidget() { return nullptr; }

	/** Should return true, if the importer requires import settings UI to be shown. */
	virtual bool HasImportUI() const { return false; }
#endif

	friend class USWStorageBasicFileIO;
	friend class FSWStorageBasicFileIOHandler;
};


/** Stores the results of the import process */
struct SHADERWORLDCORE_API FSWStorageBasicImportResults
{
public:
	TArray64<FLidarPointCloudPoint> Points;
	FBox Bounds;
	FVector OriginalCoordinates;

	/** Contains the list of imported classification IDs */
	TArray<uint8> ClassificationsImported;

private:
	/** Used for async importing */
	TFunction<void(float)> ProgressCallback;
	FThreadSafeBool* bCancelled;
	uint64 ProgressFrequency;
	uint64 ProgressCounter;
	uint64 TotalProgressCounter;
	uint64 MaxProgressCounter;
	TFunction<void(const FBox& Bounds, FVector)> InitCallback;
	TFunction<void(TArray64<FLidarPointCloudPoint>*)> BufferCallback;

public:
	FSWStorageBasicImportResults(FThreadSafeBool* bInCancelled = nullptr, TFunction<void(float)> InProgressCallback = TFunction<void(float)>());
	FSWStorageBasicImportResults(FThreadSafeBool* bInCancelled, TFunction<void(float)> InProgressCallback, TFunction<void(const FBox& Bounds, FVector)> InInitCallback, TFunction<void(TArray64<FLidarPointCloudPoint>*)> InBufferCallback);

	void InitializeOctree(const FBox& InBounds);

	void ProcessBuffer(TArray64<FLidarPointCloudPoint>* InPoints);

	void SetPointCount(const uint64& InTotalPointCount);

	FORCEINLINE void AddPoint(const FVector& Location, const float& R, const float& G, const float& B, const float& A = 1.0f)
	{
		AddPoint(Location.X, Location.Y, Location.Z, R, G, B, A);
	}

	void AddPoint(const float& X, const float& Y, const float& Z, const float& R, const float& G, const float& B, const float& A = 1.0f);

	void AddPoint(const float& X, const float& Y, const float& Z, const float& R, const float& G, const float& B, const float& A, const float& NX, const float& NY, const float& NZ);

	void AddPointsBulk(TArray64<FLidarPointCloudPoint>& InPoints);

	void CenterPoints();

	FORCEINLINE bool IsCancelled() const
	{
		return bCancelled && *bCancelled;
	}

	void SetMaxProgressCounter(uint64 MaxCounter);

	void IncrementProgressCounter(uint64 Increment);
};

/** Base type implemented by all file handlers. */
class SHADERWORLDCORE_API FSWStorageBasicFileIOHandler
{
protected:
	/** Used for precision loss check and correction. */
	double PrecisionCorrectionOffset[3] = { 0, 0, 0 };
	bool bPrecisionCorrected = false;

public:
	/** Called before importing to prepare the data. */
	void PrepareImport();

	/** Must return true if the handler supports importing. */
	virtual bool SupportsImport() const { return false; }

	/** Must return true if the handler supports exporting. */
	virtual bool SupportsExport() const { return false; }

	/** This is what will actually be called to process the import of the file. */
	virtual bool HandleImport(const FString& Filename, TSharedPtr<FSWStorageBasicImportSettings> ImportSettings, FSWStorageBasicImportResults& OutImportResults) { return false; }

	/** These will actually be called to process the export of the asset. */
	virtual bool HandleExport(const FString& Filename, class ULidarPointCloud* PointCloud) { return false; }

	/** Returns a shared pointer for the import settings of this importer. */
	virtual TSharedPtr<FSWStorageBasicImportSettings> GetImportSettings(const FString& Filename) const { return TSharedPtr<FSWStorageBasicImportSettings>(new FSWStorageBasicImportSettings(Filename)); }

	/**
	 * Returns true if the given file supports importing and building an octree concurrently.
	 * False if the data needs caching first.
	 */
	virtual bool SupportsConcurrentInsertion(const FString& Filename) const { return false; }

	/**
	 * Must return true if the provided UID is of supported Import Settings type.
	 * Default implementation simply checks the default ImportSettings' UID to compare against.
	 */
	virtual bool IsSettingsUIDSupported(const FString& UID) { return UID.Equals(GetImportSettings("")->GetUID()); }

	/**
	 * Performs validation checks and corrections on the provided ImportSettings object using the given Filename.
	 * Returns true if the resulting object is valid.
	 */
	virtual bool ValidateImportSettings(TSharedPtr<FSWStorageBasicImportSettings>& ImportSettings, const FString& Filename);
};
#endif
/**
 * 
 */
class SHADERWORLDCORE_API SWStorageFileIO
{
public:
	SWStorageFileIO();
	~SWStorageFileIO();
};
