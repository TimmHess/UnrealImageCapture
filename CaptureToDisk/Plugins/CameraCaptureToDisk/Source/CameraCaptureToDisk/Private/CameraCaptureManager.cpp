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
        if(!RenderFloatRequestQueue.IsEmpty()){
            UE_LOG(LogTemp, Warning, TEXT("Beginning to save"));
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
                    imageWrapper->SetRaw(nextRenderRequest->Image.GetData(), nextRenderRequest->Image.GetAllocatedSize(), FrameWidth, FrameHeight, ERGBFormat::RGBA, 16);
                    const TArray64<uint8>& PngData = imageWrapper->GetCompressed(0);
                    FFileHelper::SaveArrayToFile(PngData, *fileName);
                }
            }

            // Delete the first element from RenderQueue
            RenderFloatRequestQueue.Pop();
            delete nextRenderRequest;

            ImgCounter += 1;
        }
    }

	// Read pixels once RenderFence is completed
    else{
        if(!RenderRequestQueue.IsEmpty()){
            // Peek the next RenderRequest from queue
            FRenderRequestStruct* nextRenderRequest = nullptr;
            RenderRequestQueue.Peek(nextRenderRequest);

            //int32 frameWidht = 640;
            //int32 frameHeight = 480;

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

    // Set FrameWidth and FrameHeight
    renderTarget2D->TargetGamma = GEngine->GetDisplayGamma(); //1.2f; // for Vulkan //GEngine->GetDisplayGamma(); // for DX11/12

    // Setup the RenderTarget capture format
    renderTarget2D->InitAutoFormat(256, 256); // some random format, got crashing otherwise
    //int32 frameWidht = 640;
    //int32 frameHeight = 480;
    
    //renderTarget2D->InitCustomFormat(FrameWidth, FrameHeight, PF_B8G8R8A8, true); // PF_B8G8R8A8 disables HDR which will boost storing to disk due to less image information
    //renderTarget2D->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
    
    renderTarget2D->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA32f;
    renderTarget2D->InitCustomFormat(FrameWidth, FrameHeight, PF_FloatRGBA, true); // PF_B8G8R8A8 disables HDR which will boost storing to disk due to less image information
    
    renderTarget2D->bGPUSharedFlag = true; // demand buffer on GPU

    // Assign RenderTarget
    CaptureComponent->GetCaptureComponent2D()->TextureTarget = renderTarget2D;

    // Set Camera Properties
    CaptureComponent->GetCaptureComponent2D()->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    //CaptureComponent->GetCaptureComponent2D()->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;

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

    CaptureComponent->GetCaptureComponent2D()->TextureTarget->TargetGamma = GEngine->GetDisplayGamma();

    // Get RenderConterxt
    FTextureRenderTargetResource* renderTargetResource = CaptureComponent->GetCaptureComponent2D()->TextureTarget->GameThread_GetRenderTargetResource();

    struct FReadSurfaceContext{
        FRenderTarget* SrcRenderTarget;
        TArray<FColor>* OutData;
        FIntRect Rect;
        FReadSurfaceDataFlags Flags;
    };

    // Init new RenderRequest
    FRenderRequestStruct* renderRequest = new FRenderRequestStruct();

    // Setup GPU command
    FReadSurfaceContext readSurfaceContext = {
        renderTargetResource,
        &(renderRequest->Image),
        FIntRect(0,0,renderTargetResource->GetSizeXY().X, renderTargetResource->GetSizeXY().Y),
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



void ACameraCaptureManager::CaptureFloatNonBlocking(){
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

	//FlushRenderingCommands();

    ////////////////////


    /*
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    FString fileName = "";
    fileName = FPaths::ProjectSavedDir() + SubDirectoryName + "/img" + "_" + ToStringWithLeadingZeros(ImgCounter, NumDigits);
    fileName += ".exr"; // Add file ending

    static TSharedPtr<IImageWrapper> imageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::EXR); //EImageFormat::PNG //EImageFormat::JPEG
    imageWrapper->SetRaw(renderFloatRequest->Image.GetData(), renderFloatRequest->Image.GetAllocatedSize(), FrameWidth, FrameHeight, ERGBFormat::RGBA, 16);
    const TArray64<uint8>& PngData = imageWrapper->GetCompressed(0);
    FFileHelper::SaveArrayToFile(PngData, *fileName);
    UE_LOG(LogTemp, Warning, TEXT("Saving Complete"));
    */


    ///////////////////////

	// Copy the surface data into the output array.
	//FFloat16Color* OutImageColors = reinterpret_cast<FFloat16Color*> (OutImageData);

	// Cache width and height as its very expensive to call these virtuals in inner loop (never inlined)
	//const int32 ImageWidth = GetSizeXY().X;
	//const int32 ImageHeight = GetSizeXY().Y;
	//const int32 ImageWidth = FrameWidth;
    //const int32 ImageHeight = FrameHeight;
    //for (int32 Y = 0; Y < ImageHeight; Y++)
	//{
	//	FFloat16Color* SourceData = (FFloat16Color*)SurfaceData.GetData() + Y * ImageWidth;
	//	for (int32 X = 0; X < ImageWidth; X++) {
	//		OutImageColors[ Y * ImageWidth + X ] = SourceData[X];
	//	}
	//}
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