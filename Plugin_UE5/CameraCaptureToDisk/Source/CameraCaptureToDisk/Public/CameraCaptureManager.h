// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

class ASceneCapture2D;
class UMaterial;

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Containers/Queue.h"
#include "CameraCaptureManager.generated.h"




USTRUCT()
struct FRenderRequestStruct{
    GENERATED_BODY()

    TArray<FColor> Image;
    FRenderCommandFence RenderFence;

    FRenderRequestStruct(){

    }
};


USTRUCT()
struct FFloatRenderRequestStruct{
    GENERATED_BODY()

    TArray<FFloat16Color> Image;
    FRenderCommandFence RenderFence;

    FFloatRenderRequestStruct(){

    }
};


UCLASS(Blueprintable)
class CAMERACAPTURETODISK_API ACameraCaptureManager : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ACameraCaptureManager();
    
    // Captured Data Sub-Directory Name 
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture")
    FString SubDirectoryName = "";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture")
    int NumDigits = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture")
    int FrameWidth = 640;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture")
    int FrameHeight = 480;

    // If not UsePNG, JPEG format is used (For Non-Color purposes PNG is necessary, elsewise compression will mess with labels!)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture")
    bool UsePNG = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture")
    bool UseFloat = false;

	// Color Capture Components
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture")
    ASceneCapture2D* CaptureComponent;

    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture")
    //ASceneCapture2D* SegmentationCapture = nullptr;

    // PostProcessMaterial used for segmentation
    UPROPERTY(EditAnywhere, Category="Capture")
    UMaterial* PostProcessMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category="Logging")
    bool VerboseLogging = false;

protected:
	// RenderRequest Queue
    TQueue<FRenderRequestStruct*> RenderRequestQueue;

    // FloatRenderRequest Queue
    TQueue<FFloatRenderRequestStruct*> RenderFloatRequestQueue;
    int ImgCounter = 0;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	void SetupCaptureComponent();

    // Creates an async task that will save the captured image to disk
    void RunAsyncImageSaveTask(TArray64<uint8> Image, FString ImageName);

    //void SpawnSegmentationCaptureComponent(ASceneCapture2D* ColorCapture);
    //void SetupSegmentationCaptureComponent(ASceneCapture2D* ColorCapture);

    FString ToStringWithLeadingZeros(int32 Integer, int32 MaxDigits);

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "ImageCapture")
    void CaptureNonBlocking();

    UFUNCTION(BlueprintCallable, Category = "ImageCapture")
    void CaptureFloatNonBlocking();
};




class AsyncSaveImageToDiskTask : public FNonAbandonableTask{
    public:
        AsyncSaveImageToDiskTask(TArray64<uint8> Image, FString ImageName);
        ~AsyncSaveImageToDiskTask();

    // Required by UE4!
    FORCEINLINE TStatId GetStatId() const{
        RETURN_QUICK_DECLARE_CYCLE_STAT(AsyncSaveImageToDiskTask, STATGROUP_ThreadPoolAsyncTasks);
    }

protected:
    TArray64<uint8> ImageCopy;
    FString FileName = "";

public:
    void DoWork();
};
