// Fill out your copyright notice in the Description page of Project Settings.


#include "FrameCaptureManager.h"

#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ShowFlags.h"

#include "Engine/TextureRenderTarget2D.h"

#include "Materials/Material.h"

#include "RHICommandList.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

#include "ImageUtils.h"

#include "Modules/ModuleManager.h"

#include "Misc/FileHelper.h"

// Sets default values
AFrameCaptureManager::AFrameCaptureManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AFrameCaptureManager::BeginPlay()
{
	Super::BeginPlay();
	
	if(CaptureComponent){ // nullptr check
		SetupCaptureComponent();
	} else{
		UE_LOG(LogTemp, Error, TEXT("No CaptureComponent set!"));
	}
}

// Called every frame
void AFrameCaptureManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if(!RenderRequestQueue.IsEmpty()){
        // Peek the next RenderRequest from queue
        TSharedPtr<FRenderRequestStruct> nextRenderRequest = *RenderRequestQueue.Peek();

        if(nextRenderRequest){ //nullptr check
            if(nextRenderRequest->RenderFence.IsFenceComplete() && nextRenderRequest->Readback.IsReady()){
                // Load the image wrapper module 
                //IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

                // Get Data from Readback
                nextRenderRequest->RawSize = nextRenderRequest->ImageSize.X * nextRenderRequest->ImageSize.Y * sizeof(FColor);
                if(ImageFormat == ECustomImageFormat::EXR){ // handle float case
                    nextRenderRequest->RawSize = nextRenderRequest->ImageSize.X * nextRenderRequest->ImageSize.Y * sizeof(FFloat16Color);
                }
                
                int32 RowPitchInPixels;
                nextRenderRequest->RawData = nextRenderRequest->Readback.Lock(RowPitchInPixels, nullptr); // Pass RowPitchInPixels and no buffer size
                
                // Generate image name
                FString fileName = "";
                fileName = FPaths::ProjectSavedDir() + SubDirectoryName + "/img" + "_" + ToStringWithLeadingZeros(ImgCounter, NumDigits);
                fileName += GetFileEnding(ImageFormat);

                // Pass the raw data, filename, width, height, and format to the async task
                RunAsyncImageSaveTask(nextRenderRequest, fileName, FrameWidth, FrameHeight, GetRGBFormatFromImageFormat(ImageFormat), ConvertImageFormat(ImageFormat));
                
                // Increase ImgCounter for file names
                ImgCounter += 1;

                // Release RenderRequest form the queue
                RenderRequestQueue.Pop(); // Delete the first element from RenderQueue
                // Put it into the queue of outsourced threads
                InThreadRenderRequestQueue.Enqueue(nextRenderRequest); //Push to InThreadRenderRequestQueue
            }
        }
    }
    if(!InThreadRenderRequestQueue.IsEmpty()){
        UE_LOG(LogTemp, Log, TEXT("InThreadRenderRequestQueue not empty."));

        // Get next element of the InThreadRenderRequestQueue
        TSharedPtr<FRenderRequestStruct> nextRenderRequest = *InThreadRenderRequestQueue.Peek();

        if(nextRenderRequest){ //nullptr check
            if(nextRenderRequest->bIsComplete){ //check if complete
                InThreadRenderRequestQueue.Pop(); // Remove from queue
            }
        }
    }
}

void AFrameCaptureManager::SetupCaptureComponent(){
    if(!IsValid(CaptureComponent)){
        UE_LOG(LogTemp, Error, TEXT("SetupCaptureComponent: CaptureComponent is not valid!"));
        return;
    }

    // Create RenderTargets
    UTextureRenderTarget2D* renderTarget2D = NewObject<UTextureRenderTarget2D>();
    renderTarget2D->InitAutoFormat(256, 256); // some random format, got crashing otherwise

    // Float Capture
    if(ImageFormat == ECustomImageFormat::EXR){ // handle float case
        renderTarget2D->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA32f;
        renderTarget2D->InitCustomFormat(FrameWidth, FrameHeight, PF_FloatRGBA, true); // PF_B8G8R8A8 disables HDR which will boost storing to disk due to less image information
    }
    // Color Capture
    else{
        renderTarget2D->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB; //ETextureRenderTargetFormat::RTF_RGBA8; //8-bit color format
        renderTarget2D->InitCustomFormat(FrameWidth, FrameHeight, PF_R8G8B8A8, true); // PF_R8G8B8A8 //PF_B8G8R8A8 // PF... disables HDR, which is most important since HDR gives gigantic overhead, and is not needed!
        renderTarget2D->bForceLinearGamma = true; // Important for viewport-like color reproduction.
    }
    renderTarget2D->bGPUSharedFlag = true; // demand buffer on GPU

    // Assign RenderTarget
    CaptureComponent->GetCaptureComponent2D()->TextureTarget = renderTarget2D;
    // Set Camera Properties
    CaptureComponent->GetCaptureComponent2D()->CaptureSource =  ESceneCaptureSource::SCS_FinalColorLDR;
    CaptureComponent->GetCaptureComponent2D()->TextureTarget->TargetGamma = GEngine->GetDisplayGamma();
    CaptureComponent->GetCaptureComponent2D()->ShowFlags.SetTemporalAA(true);
    // lookup more showflags in documentation

    // Assign PostProcess Material if assigned
    if(PostProcessMaterial){ // check nullptr
        CaptureComponent->GetCaptureComponent2D()->AddOrUpdateBlendable(PostProcessMaterial);
    } else {
        UE_LOG(LogTemp, Log, TEXT("No PostProcessMaterial is assigend"));
    }
    UE_LOG(LogTemp, Warning, TEXT("Initialized RenderTarget!"));
}



void AFrameCaptureManager::CaptureNonBlocking(){
    if(!IsValid(CaptureComponent)){
        UE_LOG(LogTemp, Error, TEXT("CaptureColorNonBlocking: CaptureComponent was not valid!"));
        return;
    }
    CaptureComponent->GetCaptureComponent2D()->TextureTarget->TargetGamma = GEngine->GetDisplayGamma(); // 1.2f; 

    // Get RenderConterxt
    FTextureRenderTargetResource* renderTargetResource = CaptureComponent->GetCaptureComponent2D()->TextureTarget->GameThread_GetRenderTargetResource();
    
    TSharedPtr<FRenderRequestStruct> renderRequest = 
        MakeShared<FRenderRequestStruct>(
            renderTargetResource->GetSizeXY(), 
            FRHIGPUTextureReadback(TEXT("CameraCaptureManagerReadback")
        )
    );

    ENQUEUE_RENDER_COMMAND(SceneDrawCompletion)(
    [renderRequest, renderTargetResource](FRHICommandListImmediate& RHICmdList) {
        FTextureRHIRef Target = renderTargetResource->GetRenderTargetTexture();
        renderRequest->Readback.EnqueueCopy(RHICmdList, Target);
    });

    // Notifiy new task in RenderQueue
    RenderRequestQueue.Enqueue(renderRequest);

    // Set RenderCommandFence
    renderRequest->RenderFence.BeginFence();
}



FString AFrameCaptureManager::ToStringWithLeadingZeros(int32 Integer, int32 MaxDigits){
    FString result = FString::FromInt(Integer);
    int32 stringSize = result.Len();
    int32 stringDelta = MaxDigits - stringSize;
    if(stringDelta < 0){
        UE_LOG(LogTemp, Error, TEXT("MaxDigits of ImageCounter Overflow!"));
        return result;
    }
    //FIXME: Smarter function for this..
    FString leadingZeros = "";
    for(size_t i=0;i<stringDelta;i++){
        leadingZeros += "0";
    }
    result = leadingZeros + result;

    return result;
}

void AFrameCaptureManager::RunAsyncImageSaveTask(
        TSharedPtr<FRenderRequestStruct> RenderRequest, 
        FString ImageName, 
        int32 Width, 
        int32 Height, 
        ERGBFormat ActualRGBFormat,
        EImageFormat ActualImageFormat) 
    {
    UE_LOG(LogTemp, Warning, TEXT("Running Async Task"));
    (new FAutoDeleteAsyncTask<AsyncSaveImageToDiskTask>(RenderRequest, ImageName, Width, Height, ActualRGBFormat, ActualImageFormat))->StartBackgroundTask();
}



/*
*******************************************************************
*/

AsyncSaveImageToDiskTask::AsyncSaveImageToDiskTask(
        TSharedPtr<FRenderRequestStruct> InRenderRequest, 
        FString ImageName, 
        int32 Width, 
        int32 Height, 
        ERGBFormat RGBFormat,
        EImageFormat ImageFormat
    ):
    RenderRequest(InRenderRequest),
    FileName(ImageName),
    Width(Width),
    Height(Height),
    RGBFormat(RGBFormat),
    ImageFormat(ImageFormat)
{
}

AsyncSaveImageToDiskTask::~AsyncSaveImageToDiskTask(){
    UE_LOG(LogTemp, Warning, TEXT("AsyncTaskDone"));
}


void AsyncSaveImageToDiskTask::DoWork(){
    UE_LOG(LogTemp, Warning, TEXT("Starting Work"));

    // Load the image wrapper module (if not already loaded)
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

    // Create an image wrapper
    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

    // Error handling
    if (!ImageWrapper.IsValid()) {
        UE_LOG(LogTemp, Error, TEXT("Failed to create IImageWrapper!"));
        return;
    }

    // Set the raw data 
    int32 PixelDepth = 8;
    if(ImageFormat == EImageFormat::EXR){ // Adjust pixel depth for EXR (float) data  // Has to be EImageFormat because already converted..
        PixelDepth = 16;
    }
    UE_LOG(LogTemp, Warning, TEXT("PixelDepth: %d"), PixelDepth);
    ImageWrapper->SetRaw(RenderRequest->RawData, RenderRequest->RawSize, Width, Height, RGBFormat, PixelDepth);

    // Compress the image 
    /** from /Engine/Source/Runtime/ImageWrapper/Public/IImageWrapper.h
     * Enumerates available image compression qualities.
     * 
     * JPEG interprets Quality as 1-100
     * JPEG default quality is 85 , Uncompressed means 100
     * 
     * for PNG:
     * Negative qualities in [-1,-9] set PNG zlib level
     * PNG interprets "Uncompressed" as zlib level 0 (none)
     * otherwise default zlib level 3 is used.
     * 
     * EXR respects the "Uncompressed" flag to turn off compression; otherwise ZIP_COMPRESSION is used.
    */
    const TArray64<uint8>& CompressedData = ImageWrapper->GetCompressed(100);

    // Save the compressed image to disk
    FFileHelper::SaveArrayToFile(CompressedData, *FileName);

    //unlock the readback after the processing is done
    RenderRequest->Readback.Unlock();

    // Indicate that the processing is complete (using a flag)
    RenderRequest->bIsComplete = true;

    //UE_LOG(LogTemp, Log, TEXT("Stored Image: %s"), *FileName);
}