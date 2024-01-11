// Fill out your copyright notice in the Description page of Project Settings.


#include "CameraCaptureManager.h"

//#include "Engine.h"
#include "Runtime/Engine/Classes/Engine/Engine.h"

#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/GameplayStatics.h"
#include "ShowFlags.h"

#include "Materials/Material.h"

#include "RHICommandList.h"

#include "ImageWrapper/Public/IImageWrapper.h"
#include "ImageWrapper/Public/IImageWrapperModule.h"

#include "ImageUtils.h"

#include "Modules/ModuleManager.h"

#include "Misc/FileHelper.h"

// Sets default values
ACameraCaptureManager::ACameraCaptureManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void ACameraCaptureManager::BeginPlay()
{
	Super::BeginPlay();

	if(CaptureComponent){ // nullptr check
		SetupCaptureComponent();
	} else{
		UE_LOG(LogTemp, Error, TEXT("No CaptureComponent set!"));
	}
	
}

// Called every frame
void ACameraCaptureManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	 // Read pixels once RenderFence is completed
    if(!RenderRequestQueue.IsEmpty()){
        // Peek the next RenderRequest from queue
        TSharedPtr<FRenderRequestStruct> nextRenderRequest = *RenderRequestQueue.Peek();

        //int32 frameWidht = 640;
        //int32 frameHeight = 480;

        if(nextRenderRequest){ //nullptr check
            if(nextRenderRequest->RenderFence.IsFenceComplete() && nextRenderRequest->Readback.IsReady()) { // Check if rendering is done, indicated by RenderFence & Readback

                // Load the image wrapper module 
                IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

                // Get Data from Readback
                int64 RawSize = nextRenderRequest->ImageSize.X * nextRenderRequest->ImageSize.Y * sizeof(FColor);
                void* RawData = nextRenderRequest->Readback.Lock(RawSize);


                // Decide storing of data, either jpeg or png
                FString fileName = "";
                if(UsePNG){
                    //Generate image name
                    fileName = FPaths::ProjectSavedDir() + SubDirectoryName + "/img" + "_" + ToStringWithLeadingZeros(ImgCounter, NumDigits);
                    fileName += ".png"; // Add file ending

                    // Prepare data to be written to disk
                    static TSharedPtr<IImageWrapper> imageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG); //EImageFormat::PNG //EImageFormat::JPEG
                    imageWrapper->SetRaw(RawData, RawSize, FrameWidth, FrameHeight, ERGBFormat::BGRA, 8);
                    const TArray<uint8>& ImgData = imageWrapper->GetCompressed(5);
                    RunAsyncImageSaveTask(ImgData, fileName);
                } else{
                    // Generate image name
                    fileName = FPaths::ProjectSavedDir() + SubDirectoryName + "/img" + "_" + ToStringWithLeadingZeros(ImgCounter, NumDigits);
                    fileName += ".jpeg"; // Add file ending

                    // Prepare data to be written to disk
                    static TSharedPtr<IImageWrapper> imageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG); //EImageFormat::PNG //EImageFormat::JPEG
                    imageWrapper->SetRaw(RawData, RawSize, FrameWidth, FrameHeight, ERGBFormat::BGRA, 8);
                    const TArray<uint8>& ImgData = imageWrapper->GetCompressed(0);
                    RunAsyncImageSaveTask(ImgData, fileName);
                }

                if(VerboseLogging && !fileName.IsEmpty()){
                    UE_LOG(LogTemp, Warning, TEXT("%f"), *fileName);
                }

                ImgCounter += 1;

                // Delete the first element from RenderQueue
                RenderRequestQueue.Pop();

                UE_LOG(LogTemp, Log, TEXT("Done..."));
            }
        }
    }

}

void ACameraCaptureManager::SetupCaptureComponent(){
    if(!IsValid(CaptureComponent)){
        UE_LOG(LogTemp, Error, TEXT("SetupCaptureComponent: CaptureComponent is not valid!"));
        return;
    }

    // Create RenderTargets
    UTextureRenderTarget2D* renderTarget2D = NewObject<UTextureRenderTarget2D>();

    // Set FrameWidth and FrameHeight
    renderTarget2D->TargetGamma = GEngine->GetDisplayGamma(); //1.2f; // for Vulkan //GEngine->GetDisplayGamma(); // for DX11/12

    // Setup the RenderTarget capture format
    renderTarget2D->InitAutoFormat(256, 256); // some random format, got crashing otherwise
    //int32 frameWidht = 640;
    //int32 frameHeight = 480;
    renderTarget2D->InitCustomFormat(FrameWidth, FrameHeight, PF_B8G8R8A8, true); // PF_B8G8R8A8 disables HDR which will boost storing to disk due to less image information
    renderTarget2D->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
    renderTarget2D->bGPUSharedFlag = true; // demand buffer on GPU

    // Assign RenderTarget
    CaptureComponent->GetCaptureComponent2D()->TextureTarget = renderTarget2D;

    // Set Camera Properties
    CaptureComponent->GetCaptureComponent2D()->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    CaptureComponent->GetCaptureComponent2D()->ShowFlags.SetTemporalAA(true);
    // lookup more showflags in the UE4 documentation..

    // Assign PostProcess Material if assigned
    if(PostProcessMaterial){ // check nullptr
        CaptureComponent->GetCaptureComponent2D()->AddOrUpdateBlendable(PostProcessMaterial);
    } else {
        UE_LOG(LogTemp, Log, TEXT("No PostProcessMaterial is assigend"));
    }

}

void ACameraCaptureManager::CaptureNonBlocking(){
    if(!IsValid(CaptureComponent)){
        UE_LOG(LogTemp, Error, TEXT("CaptureColorNonBlocking: CaptureComponent was not valid!"));
        return;
    }

    CaptureComponent->GetCaptureComponent2D()->TextureTarget->TargetGamma = GEngine->GetDisplayGamma();

    // Get RenderConterxt
    FTextureRenderTargetResource* renderTargetResource = CaptureComponent->GetCaptureComponent2D()->TextureTarget->GameThread_GetRenderTargetResource();

    // Init new RenderRequest
    TSharedPtr<FRenderRequestStruct> renderRequest = MakeShared<FRenderRequestStruct>(renderTargetResource->GetSizeXY(), FRHIGPUTextureReadback(TEXT("CameraCaptureManagerReadback")));

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
    [renderTargetResource](FRHICommandListImmediate& RHICmdList) {
        FTexture2DRHIRef Target = renderTargetResource->GetRenderTargetTexture();
        RenderRequest->Readback.EnqueueCopy(RHICmdList, Target);
    });

    // Notifiy new task in RenderQueue
    RenderRequestQueue.Enqueue(renderRequest);

    // Set RenderCommandFence
    renderRequest->RenderFence.BeginFence();
}


/*
void ACameraCaptureManager::SpawnSegmentationCaptureComponent(ASceneCapture2D* ColorCapture){
	if(!IsValid(ColorCapture)){
        UE_LOG(LogTemp, Error, TEXT("CaptureColorNonBlocking: CaptureComponent was not valid!"));
        return;
    }

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
*/
/*
void ACameraCaptureManager::SetupSegmentationCaptureComponent(ASceneCapture2D* ColorCapture){
	if(!IsValid(ColorCapture)){
        UE_LOG(LogTemp, Error, TEXT("CaptureColorNonBlocking: CaptureComponent was not valid!"));
        return;
    }

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
*/

FString ACameraCaptureManager::ToStringWithLeadingZeros(int32 Integer, int32 MaxDigits){
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





void ACameraCaptureManager::RunAsyncImageSaveTask(TArray<uint8> Image, FString ImageName){
    (new FAutoDeleteAsyncTask<AsyncSaveImageToDiskTask>(Image, ImageName))->StartBackgroundTask();
}



/*
*******************************************************************
*/

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
