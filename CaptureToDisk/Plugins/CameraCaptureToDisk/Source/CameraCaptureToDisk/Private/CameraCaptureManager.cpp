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
    
    if(UseFloat){
        // READ FLOAT IMAGE
        if(!RenderFloatRequestQueue.IsEmpty()){
            // Peek the next RenderRequest from queue
            FFloatRenderRequestStruct* nextRenderRequest = nullptr;
            RenderFloatRequestQueue.Peek(nextRenderRequest);

            if(nextRenderRequest){
                if(nextRenderRequest->RenderFence.IsFenceComplete()){
                    // Load the image wrapper module 
                    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

                    FString fileName = "";
                    fileName = FPaths::ProjectSavedDir() + SubDirectoryName + "/img" + "_" + ToStringWithLeadingZeros(ImgCounter, NumDigits);
                    fileName += ".exr"; // Add file ending

                    static TSharedPtr<IImageWrapper> imageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::EXR); //EImageFormat::PNG //EImageFormat::JPEG
                    imageWrapper->SetRaw(nextRenderRequest->Image.GetData(), nextRenderRequest->Image.GetAllocatedSize(), FrameWidth, FrameHeight, ERGBFormat::RGBAF, 16);
                    const TArray64<uint8>& PngData = imageWrapper->GetCompressed(0);
                    FFileHelper::SaveArrayToFile(PngData, *fileName);

                     // Delete the first element from RenderQueue
                    RenderFloatRequestQueue.Pop();
                    delete nextRenderRequest;
                    
                    ImgCounter += 1;
                }
            }           
        }
    }
    // READ UINT8 IMAGE
	// Read pixels once RenderFence is completed
    else{
        if(!RenderRequestQueue.IsEmpty()){
            // Peek the next RenderRequest from queue
            FRenderRequestStruct* nextRenderRequest = nullptr;
            RenderRequestQueue.Peek(nextRenderRequest);

            if(nextRenderRequest){ //nullptr check
                if(nextRenderRequest->RenderFence.IsFenceComplete()){ // Check if rendering is done, indicated by RenderFence
                    // Load the image wrapper module 
                    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

                    // Decide storing of data, either jpeg or png
                    FString fileName = "";

                    if(UsePNG){
                        //Generate image name
                        fileName = FPaths::ProjectSavedDir() + SubDirectoryName + "/img" + "_" + ToStringWithLeadingZeros(ImgCounter, NumDigits);
                        fileName += ".png"; // Add file ending

                        // Prepare data to be written to disk
                        static TSharedPtr<IImageWrapper> imageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG); //EImageFormat::PNG //EImageFormat::JPEG
                        imageWrapper->SetRaw(nextRenderRequest->Image.GetData(), nextRenderRequest->Image.GetAllocatedSize(), FrameWidth, FrameHeight, ERGBFormat::BGRA, 8);
                        const TArray64<uint8>& ImgData = imageWrapper->GetCompressed(5);
                        //const TArray<uint8>& ImgData =  static_cast<TArray<uint8, FDefaultAllocator>> (imageWrapper->GetCompressed(5));
                        RunAsyncImageSaveTask(ImgData, fileName);
                    } else{
                        // Generate image name
                        fileName = FPaths::ProjectSavedDir() + SubDirectoryName + "/img" + "_" + ToStringWithLeadingZeros(ImgCounter, NumDigits);
                        fileName += ".jpeg"; // Add file ending
    
                        // Prepare data to be written to disk
                        static TSharedPtr<IImageWrapper> imageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG); //EImageFormat::PNG //EImageFormat::JPEG
                        imageWrapper->SetRaw(nextRenderRequest->Image.GetData(), nextRenderRequest->Image.GetAllocatedSize(), FrameWidth, FrameHeight, ERGBFormat::BGRA, 8);
                        const TArray64<uint8>& ImgData = imageWrapper->GetCompressed(0);
                        //const TArray<uint8>& ImgData = static_cast<TArray<uint8, FDefaultAllocator>> (imageWrapper->GetCompressed(0));
                        RunAsyncImageSaveTask(ImgData, fileName);
                    }
                    if(VerboseLogging && !fileName.IsEmpty()){
                        UE_LOG(LogTemp, Warning, TEXT("%f"), *fileName);
                    }
                    
                    ImgCounter += 1;

                    // Delete the first element from RenderQueue
                    RenderRequestQueue.Pop();
                    delete nextRenderRequest;
                }
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

    // Float Capture
    if(UseFloat){
        renderTarget2D->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA32f;
        renderTarget2D->InitCustomFormat(FrameWidth, FrameHeight, PF_FloatRGBA, true); // PF_B8G8R8A8 disables HDR which will boost storing to disk due to less image information
        UE_LOG(LogTemp, Warning, TEXT("Set Render Format for DepthCapture.."));
    }
    // Color Capture
    else{
        renderTarget2D->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8; //8-bit color format
        renderTarget2D->InitCustomFormat(FrameWidth, FrameHeight, PF_B8G8R8A8, true); // PF... disables HDR, which is most important since HDR gives gigantic overhead, and is not needed!
        UE_LOG(LogTemp, Warning, TEXT("Set Render Format for Color-Like-Captures"));
    }
    
    renderTarget2D->bGPUSharedFlag = true; // demand buffer on GPU

    // Assign RenderTarget
    CaptureComponent->GetCaptureComponent2D()->TextureTarget = renderTarget2D;
    // Set Camera Properties
    CaptureComponent->GetCaptureComponent2D()->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    CaptureComponent->GetCaptureComponent2D()->TextureTarget->TargetGamma = GEngine->GetDisplayGamma();
    CaptureComponent->GetCaptureComponent2D()->ShowFlags.SetTemporalAA(true);
    // lookup more showflags in the UE4 documentation..

    // Assign PostProcess Material if assigned
    if(PostProcessMaterial){ // check nullptr
        CaptureComponent->GetCaptureComponent2D()->AddOrUpdateBlendable(PostProcessMaterial);
    } else {
        UE_LOG(LogTemp, Log, TEXT("No PostProcessMaterial is assigend"));
    }
    UE_LOG(LogTemp, Warning, TEXT("Initialized RenderTarget!"));
}

void ACameraCaptureManager::CaptureNonBlocking(){
    if(!IsValid(CaptureComponent)){
        UE_LOG(LogTemp, Error, TEXT("CaptureColorNonBlocking: CaptureComponent was not valid!"));
        return;
    }
    UE_LOG(LogTemp, Warning, TEXT("Entering: CaptureNonBlocking"));
    CaptureComponent->GetCaptureComponent2D()->TextureTarget->TargetGamma = 1.2f;//GEngine->GetDisplayGamma();

    // Get RenderConterxt
    FTextureRenderTargetResource* renderTargetResource = CaptureComponent->GetCaptureComponent2D()->TextureTarget->GameThread_GetRenderTargetResource();
    UE_LOG(LogTemp, Warning, TEXT("Got display gamma"));
    struct FReadSurfaceContext{
        FRenderTarget* SrcRenderTarget;
        TArray<FColor>* OutData;
        FIntRect Rect;
        FReadSurfaceDataFlags Flags;
    };
    UE_LOG(LogTemp, Warning, TEXT("Inited ReadSurfaceContext"));
    // Init new RenderRequest
    FRenderRequestStruct* renderRequest = new FRenderRequestStruct();
    UE_LOG(LogTemp, Warning, TEXT("inited renderrequest"));

    // Setup GPU command
    FReadSurfaceContext readSurfaceContext = {
        renderTargetResource,
        &(renderRequest->Image),
        FIntRect(0,0,renderTargetResource->GetSizeXY().X, renderTargetResource->GetSizeXY().Y),
        FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX)
    };
    UE_LOG(LogTemp, Warning, TEXT("GPU Command complete"));

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

void ACameraCaptureManager::CaptureFloatNonBlocking(){
    // Initial Check
    if(!UseFloat){
        UE_LOG(LogTemp, Error, TEXT("Called CaptureFloatNonBlocking but UseFloat is false! Will omit this call to prevent crashes!"));
        return;
    }

    // Get RenderContext
    FTextureRenderTargetResource* renderTargetResource = CaptureComponent->GetCaptureComponent2D()->TextureTarget->GameThread_GetRenderTargetResource();

    // Read the render target surface data back.	
	struct FReadSurfaceFloatContext
	{
		FRenderTarget* SrcRenderTarget;
		TArray<FFloat16Color>* OutData;
		FIntRect Rect;
		ECubeFace CubeFace;
	};

    // Init new RenderRequest
    FFloatRenderRequestStruct* renderFloatRequest = new FFloatRenderRequestStruct();

    // Setup GPU command
	//TArray<FFloat16Color> SurfaceData;
	FReadSurfaceFloatContext Context = {
		renderTargetResource,
		&(renderFloatRequest->Image),
        //&SurfaceData,
		FIntRect(0, 0, FrameWidth, FrameHeight),
		ECubeFace::CubeFace_MAX //no cubeface	
	};

	ENQUEUE_RENDER_COMMAND(ReadSurfaceFloatCommand)(
		[Context](FRHICommandListImmediate& RHICmdList) {
			RHICmdList.ReadSurfaceFloatData(
				Context.SrcRenderTarget->GetRenderTargetTexture(),
				Context.Rect,
				*Context.OutData,
				Context.CubeFace,
				0,
				0
				);
		});

    RenderFloatRequestQueue.Enqueue(renderFloatRequest);
    renderFloatRequest->RenderFence.BeginFence();
}

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

void ACameraCaptureManager::RunAsyncImageSaveTask(TArray64<uint8> Image, FString ImageName){
    UE_LOG(LogTemp, Warning, TEXT("Running Async Task"));
    (new FAutoDeleteAsyncTask<AsyncSaveImageToDiskTask>(Image, ImageName))->StartBackgroundTask();
}



/*
*******************************************************************
*/

AsyncSaveImageToDiskTask::AsyncSaveImageToDiskTask(TArray64<uint8> Image, FString ImageName){
    ImageCopy = Image;
    FileName = ImageName;
}

AsyncSaveImageToDiskTask::~AsyncSaveImageToDiskTask(){
    //UE_LOG(LogTemp, Warning, TEXT("AsyncTaskDone"));
}

void AsyncSaveImageToDiskTask::DoWork(){
    UE_LOG(LogTemp, Warning, TEXT("Starting Work"));
    FFileHelper::SaveArrayToFile(ImageCopy, *FileName);
    UE_LOG(LogTemp, Log, TEXT("Stored Image: %s"), *FileName);
}