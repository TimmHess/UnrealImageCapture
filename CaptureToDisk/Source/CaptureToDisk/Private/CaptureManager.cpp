// Fill out your copyright notice in the Description page of Project Settings.

#include "CaptureManager.h"

//#include "Runtime/Engine/Classes/Components/SceneCaptureComponent2D.h"
//#include "Runtime/Engine/Classes/Engine/TextureRenderTarget2D.h"
#include "Engine.h"
//#include <Runtime/Engine/Classes/Kismet/GameplayStatics.h>
//#include <Runtime/Engine/Public/ShowFlags.h>
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/GameplayStatics.h"
#include "ShowFlags.h"

#include "RHICommandList.h"

#include "ImageWrapper/Public/IImageWrapper.h"
#include "ImageWrapper/Public/IImageWrapperModule.h"
//#include "IImageWrapper.h"
//#include "IImageWrapperModule.h"
#include "ImageUtils.h"

#include "Modules/ModuleManager.h"


// Static ImageWrapperModule to prevent reloading -> this thing does not like to be reloaded..
//static IImageWrapperModule &ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));


// Sets default values
ACaptureManager::ACaptureManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void ACaptureManager::BeginPlay()
{
	Super::BeginPlay();

    SetupColorCaptureComponent(ColorCaptureComponents);
	
    SetupSegmentationCaptureComponent(ColorCaptureComponents);

}

// Called every frame
void ACaptureManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

    // Read pixels once RenderFence is completed
    if(!RenderRequestQueue.IsEmpty()){
        // Peek the next RenderRequest from queue
        FRenderRequest* nextRenderRequest = nullptr;
        RenderRequestQueue.Peek(nextRenderRequest);

        int32 frameWidht = 640;
        int32 frameHeight = 480;

        if(nextRenderRequest){ //nullptr check
            if(nextRenderRequest->RenderFence.IsFenceComplete()){ // Check if rendering is done, indicated by RenderFence

                // Load the image wrapper module 
                IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

                // Decide storing of data, either jpeg or png
                if(nextRenderRequest->isPNG){
                    //Generate image name
                    FString fileName = FPaths::ProjectSavedDir() + "mask";
                    fileName += ".png"; // Add file ending

                    // Prepare data to be written to disk
                    static TSharedPtr<IImageWrapper> imageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG); //EImageFormat::PNG //EImageFormat::JPEG
                    imageWrapper->SetRaw(nextRenderRequest->Image.GetData(), nextRenderRequest->Image.GetAllocatedSize(), frameWidht, frameHeight, ERGBFormat::BGRA, 8);
                    const TArray<uint8>& ImgData = imageWrapper->GetCompressed(5);
                    RunAsyncImageSaveTask(ImgData, fileName);
                } else{
                    UE_LOG(LogTemp, Log, TEXT("Started Saving Color Image"));
                    // Generate image name
                    FString fileName = FPaths::ProjectSavedDir() + "color";
                    fileName += ".jpeg"; // Add file ending

                    // Prepare data to be written to disk
                    static TSharedPtr<IImageWrapper> imageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG); //EImageFormat::PNG //EImageFormat::JPEG
                    imageWrapper->SetRaw(nextRenderRequest->Image.GetData(), nextRenderRequest->Image.GetAllocatedSize(), frameWidht, frameHeight, ERGBFormat::BGRA, 8);
                    const TArray<uint8>& ImgData = imageWrapper->GetCompressed(0);
                    RunAsyncImageSaveTask(ImgData, fileName);
                }

                // Delete the first element from RenderQueue
                RenderRequestQueue.Pop();
                delete nextRenderRequest;

                UE_LOG(LogTemp, Log, TEXT("Done..."));
            }
        }
    }
}

void ACaptureManager::SetupColorCaptureComponent(ASceneCapture2D* captureComponent){
    // Create RenderTargets
    UTextureRenderTarget2D* renderTarget2D = NewObject<UTextureRenderTarget2D>();

    // Set FrameWidth and FrameHeight
    renderTarget2D->TargetGamma = 1.2f;// for Vulkan //GEngine->GetDisplayGamma(); // for DX11/12

    // Setup the RenderTarget capture format
    renderTarget2D->InitAutoFormat(256, 256); // some random format, got crashing otherwise
    int32 frameWidht = 640;
    int32 frameHeight = 480;
    renderTarget2D->InitCustomFormat(frameWidht, frameHeight, PF_B8G8R8A8, true); // PF_B8G8R8A8 disables HDR which will boost storing to disk due to less image information
    renderTarget2D->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
    renderTarget2D->bGPUSharedFlag = true; // demand buffer on GPU

    // Assign RenderTarget
    captureComponent->GetCaptureComponent2D()->TextureTarget = renderTarget2D;

    // Set Camera Properties
    captureComponent->GetCaptureComponent2D()->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    captureComponent->GetCaptureComponent2D()->ShowFlags.SetTemporalAA(true);
    // lookup more showflags in the UE4 documentation..
}

void ACaptureManager::CaptureColorNonBlocking(ASceneCapture2D* CaptureComponent, bool IsSegmentation){
    if(!IsValid(CaptureComponent)){
        UE_LOG(LogTemp, Error, TEXT("CaptureColorNonBlocking: CaptureComponent was not valid!"));
        return;
    }

    // Get RenderConterxt
    FTextureRenderTargetResource* renderTargetResource = CaptureComponent->GetCaptureComponent2D()->TextureTarget->GameThread_GetRenderTargetResource();

    struct FReadSurfaceContext{
        FRenderTarget* SrcRenderTarget;
        TArray<FColor>* OutData;
        FIntRect Rect;
        FReadSurfaceDataFlags Flags;
    };

    // Init new RenderRequest
    FRenderRequest* renderRequest = new FRenderRequest();
    renderRequest->isPNG = IsSegmentation;

    // Setup GPU command
    FReadSurfaceContext readSurfaceContext = {
        renderTargetResource,
        &(renderRequest->Image),
        FIntRect(0,0,renderTargetResource->GetSizeXY().X,          renderTargetResource->GetSizeXY().Y),
        FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX)
    };

    // Send command to GPU
   /* Up to version 4.22 use this
    ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
        SceneDrawCompletion,//ReadSurfaceCommand,
        FReadSurfaceContext, Context, readSurfaceContext,
    {
        RHICmdList.ReadSurfaceData(
            Context.SrcRenderTarget->GetRenderTargetTexture(),
            Context.Rect,
            *Context.OutData,
            Context.Flags
        );
    });
    */
    // Above 4.22 use this
    ENQUEUE_RENDER_COMMAND(SceneDrawCompletion)(
    [readSurfaceContext](FRHICommandListImmediate& RHICmdList){
        RHICmdList.ReadSurfaceData(
            readSurfaceContext.SrcRenderTarget->GetRenderTargetTexture(),
            readSurfaceContext.Rect,
            *readSurfaceContext.OutData,
            readSurfaceContext.Flags
        );
    });

    // Notifiy new task in RenderQueue
    RenderRequestQueue.Enqueue(renderRequest);

    // Set RenderCommandFence
    renderRequest->RenderFence.BeginFence();
}


void ACaptureManager::SpawnSegmentationCaptureComponent(ASceneCapture2D* ColorCapture){
    // Spawning a new SceneCaptureComponent
    ASceneCapture2D* newSegmentationCapture = (ASceneCapture2D*) GetWorld()->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass());
    if(!newSegmentationCapture){ // nullptr check
        UE_LOG(LogTemp, Error, TEXT("Failed to spawn SegmentationComponent"));
        return;
    }
    // Register new CaptureComponent to game
    newSegmentationCapture->GetCaptureComponent2D()->RegisterComponent();
    // Attach SegmentationCaptureComponent to match ColorCaptureComponent
    newSegmentationCapture->AttachToActor(ColorCapture, FAttachmentTransformRules::SnapToTargetNotIncludingScale);

    // Get values from "parent" ColorCaptureComponent
    newSegmentationCapture->GetCaptureComponent2D()->FOVAngle = ColorCapture->GetCaptureComponent2D()->FOVAngle;

    // Set pointer to new segmentation capture component
    SegmentationCapture = newSegmentationCapture;

    UE_LOG(LogTemp, Warning, TEXT("Done..."));
}

void ACaptureManager::SetupSegmentationCaptureComponent(ASceneCapture2D* ColorCapture){
    // Spawn SegmentaitonCaptureComponents
    SpawnSegmentationCaptureComponent(ColorCapture);

    // Setup SegmentationCaptureComponent
    SetupColorCaptureComponent(SegmentationCapture);

    // Assign PostProcess Material
    if(PostProcessMaterial){ // check nullptr
        SegmentationCapture->GetCaptureComponent2D()->AddOrUpdateBlendable(PostProcessMaterial);
    } else {
        UE_LOG(LogTemp, Error, TEXT("PostProcessMaterial was nullptr!"));
    }
}


void ACaptureManager::RunAsyncImageSaveTask(TArray<uint8> Image, FString ImageName){
    (new FAutoDeleteAsyncTask<AsyncSaveImageToDiskTask>(Image, ImageName))->StartBackgroundTask();
}




AsyncSaveImageToDiskTask::AsyncSaveImageToDiskTask(TArray<uint8> Image, FString ImageName){
    ImageCopy = Image;
    FileName = ImageName;
}

AsyncSaveImageToDiskTask::~AsyncSaveImageToDiskTask(){
    //UE_LOG(LogTemp, Warning, TEXT("AsyncTaskDone"));
}

void AsyncSaveImageToDiskTask::DoWork(){
    FFileHelper::SaveArrayToFile(ImageCopy, *FileName);
    UE_LOG(LogTemp, Log, TEXT("Stored Image: %s"), *FileName);
}
