# Image Capturing With UnrealEngine 4 or 5 (for deep learning)


| Color | Segmentation |
|---------|---------|
| ![](https://github.com/TimmHess/UnrealImageCapture/blob/master/gfx/CaptureResult_color.jpeg) | ![](https://github.com/TimmHess/UnrealImageCapture/blob/master/gfx/CaptureResult_segmentation.png) |

# Changelog
- Updated to UE5.5.4
- Streamlined capture code to not block the *render thread*, and further reduce the load on the *main game thread*.
- Fixed alpha channel for pixel annotation in the post-process material. Images are no longer appearing as "empty" because of their alpha mask being 0.
- Removed plugins from repository - reduce the mess of files
- Slightly updated the tutorial text

# Outline
- [Introduction](#a-small-introduction)
- [MAIN: How to Save Images to Disk (without blocking the main threads)](#how-to-save-images-to-disk-in-ue5-without-blocking-the-rendering-or-main-thread)
- [Capturing Object Pixel-Annotations (segmentation mask)]()
- [Capturing Scene Depth](#capturing)
- [Enable Lumen on SceneCapture2D]()
- [Known Issues](#known-issues)

# TLDR
Use these links to the [FrameCaptureManger.h]() and [FrameCaptureManger.cpp]() file. They are the only source needed.
Plus, make sure to link the correct unreal-libs to your project - check the [prerequisite](#prerequisite) or [CaptureToDisk.Build.cs]().

**Kudos to the UE4 and UE5 community!**

**Special thanks to @Panakotta00, for pointing to an even better GPU readback!**

Using the source as is you should get an `AFrameCaptureManager` (`Actor`) in your scene with the following settings.
![](https://github.com/TimmHess/UnrealImageCapture/blob/master/gfx/FrameCaptureManage_settings.png)

Maybe most important is the `ImageFormat` setting. You want to use `PNG` for lossless compression when storing [object annotation](#capturing-object-segmentation-masks). For color rendered images most likely `JPEG` gives you better file-sizes. `EXR` is the float format - the code automatically stores .exr images when used.

# Known Issues
Capturing per-pixel annotations is done using the `CustomDepth` feature. The `CustomDepthStencil` is of type `uint8` which allows a range of 0-255, i.e. **we can handle at most 256** different annotations per image!

#
# A Small Introduction
In this repository I condense my findings on how to implement a component to capture images to disk from an arbitrary UE5 (former UE4) scene **from scratch** lowering the bar for UE novices (and potentially bypassing the need for large frameworks that don't fit ones own particular needs). This will include:
1. Capturing rendered images to disk at high FPS, without blocking the UE rendering thread or the main game thread
2. Rendering pixel annotations (or other graphics buffers, such as depth) at the same time

UnrealEngine (UE) is a powerful tool to create virtual worlds capable of AAA productions. Generating temporally consistent data with automatic pixel-wise annotations from complex scenes, such as traffic scenarios, is a capability worth leveraging. Especially for training and validation of machine learning- or deep learning applications it has been explored in a variety of projects. Already, there are plugins available that allow rendering images from UE to disk at runtime, such as prominently [Carla](https://carla-ue5.readthedocs.io/en/latest/), [UnrealCV](https://unrealcv.org/), or [AirSim](https://github.com/microsoft/AirSim). This repository aims to be a tutorial that demonstrates such an 'image capturing' mechanism in detail for you to understand its inner workings, and in turn enable you to reuse it in a custom fashions that suit the needs of your project. 

When I was setting up scenes for my research the plugins mentioned above were just not yet supporting the latest engine versions that I wanted/needed to use. Also, I was missing a place where the knowledge of how to render images to disk was explains for non-advanced graphics-programmers. Of course, there are lots of sources for code available online and also there are community blog-entries scattered across multiple platforms explaining parts of the problem and possible solutions, even though they typically are targeting very particular scenarios.

**Disclaimer: I do not claim to own any of the code. Merely, I condensed the sources already available online for easier use and provide an overview to the general functionality of this particular approach!**



#
# How to Save Images to Disk In UE5 (without blocking the rendering or main thread)
I will go through the main components of the code step-by-step so that hopefully it will be easier to implement each step as you are following along. However, I recommend looking at the source that is merely a single class ([here]()). 

*In the explanations I skip certain quality-of-life-like aspects for sakes of readability, for example exposing image resolution settings to the editor instead of hardcoding them. Make sure to check out the sources linked in [TLDR](#tldr)*


#
## Prerequisite
You will need a UE5 C++ project. 

Also, you will have to add a few packages to your `'YourProjectName'.Build.cs` file. These are part of UnrealEngine, however, sometimes they are not added automatically resulting in unpleasant (linker) errors. Find the `'YourProjectName'.Build.cs` file in the `Source/'YourProjectName/` directory, and add or extend it to include the modules: `"ImageWrapper", "RenderCore", "Renderer", "RHI"`, for example like this:

``` cpp
PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" , "ImageWrapper", "RenderCore", "Renderer", "RHI"});
```

#
## Setup A FrameCapture Component
I am using ```SceneCaptureComponent2D``` as the basis for capturing images. Placing one of these into your scene will give you an ```ASceneCaptureComponent``` which is its `Actor` instance. It basically behaves like any other camera component, but its viewport is not restricted by your computer's monitor or main camera viewport. This provides us the possibility to render images of arbitrary resolution independent from the actual screen resolution.

> Add a ```FrameCaptureManager``` class of type `Actor` to your project.

All functionality to request the capturing of a frame, as well as receiving the rendered image back, and storing the frame to disk will be handled by the `FrameCaptureManager`. 

In the ```FrameCaptureManager.h``` we add the following:\
**FrameCaptureManager.h**
``` cpp
#pragma once
class ASceneCapture2D; // forward declaration

#include ... // the stuff that is already there
```
and to our public variables:
``` cpp
// Color Capture  Components
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture")
ASceneCapture2D* CaptureComponent;
```

This enables you to assign a ```CaptureComponent2d``` to your ```FrameCaptureManager``` inside the UE5 Editor.

> Compile and place a ```FrameCaptureManager``` in your scene. 

As it does not have any primitive to render you will only see it in the editor's outline. In the details panel of the placed ```FrameCaptureManager``` you can now see the ```CaptureComponent``` assigned to ```None```. From the drop down menu select the ```CaptureComponent2D``` you already placed in the scene.

Back to code: We will now prepare our yet "naked" `CaptureComponent2D` class for capturing images. This includes creating and assigning a `RenderTarget` - which is basically a `Texture` to store our image data to - and setting the camera properties. 

*Note: You could also do this in the Editor but if you deal with, i.e. multiple capture components, you may find it handy not to worry about creating and assigning all the components by hand!*

> Create a setup function to put all your setup code for the CaptureComponents in the CaptureManger:

**FrameCaptureManager.h**
``` cpp
protected:

    void SetupCaptureComponent();
```
**FrameCaptureManager.cpp**
``` cpp
#include ...

// A bunch of includes we need
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


void AFrameCaptureManager::SetupCaptureComponent(){
    if(!IsValid(CaptureComponent)){
        UE_LOG(LogTemp, Error, TEXT("SetupCaptureComponent: CaptureComponent is not valid!"));
        return;
    }

    // Create RenderTargets
    UTextureRenderTarget2D* renderTarget2D = NewObject<UTextureRenderTarget2D>();
    renderTarget2D->InitAutoFormat(256, 256); // some random format, got crashing otherwise
    
    renderTarget2D->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB; //ETextureRenderTargetFormat::RTF_RGBA8; //8-bit color format
    renderTarget2D->InitCustomFormat(FrameWidth, FrameHeight, PF_R8G8B8A8, true); // PF... disables HDR, which is most important since HDR gives gigantic overhead, and is not needed!
    renderTarget2D->bForceLinearGamma = true; // Important for viewport-like color reproduction.

    renderTarget2D->bGPUSharedFlag = true; // demand buffer on GPU

    // Assign RenderTarget
    CaptureComponent->GetCaptureComponent2D()->TextureTarget = renderTarget2D;
    // Set Camera Properties
    CaptureComponent->GetCaptureComponent2D()->CaptureSource =  ESceneCaptureSource::SCS_FinalColorLDR;
    CaptureComponent->GetCaptureComponent2D()->TextureTarget->TargetGamma = GEngine->GetDisplayGamma();
    CaptureComponent->GetCaptureComponent2D()->ShowFlags.SetTemporalAA(true);
    // lookup additional showflags in documentation
}
```
> Call the code during `BeginPlay` of the `FrameCaptureManager`

**FrameCaptureManager.cpp**
``` cpp
// Called when the game starts or when spawned
void AFrameCaptureManager::BeginPlay()
{
	Super::BeginPlay();

    // Setup CaptureComponent
    if(CaptureComponent){ // nullptr check
		SetupCaptureComponent();
	} else{
		UE_LOG(LogTemp, Error, TEXT("No CaptureComponent set!"));
	}
}

```
Now that because we have a `RenderTarget` applied to our `CaptureComponent` we can read its data and store it to disk.


#
## Organize RenderRequests
We do this by basically re-implementing UE's code for taking screenshots. Importantly, with the addition of not flushing our rendering pipeline. This prevents rendering *hiccups* that drop the framerate to 3 - 5 FPS. 

This addition will come at the price of needing to handle 'waiting times' before an image is done and copied from GPU. This is important to prevent reading old or uninitialized buffers (remember that `RenderThread` and `GameThread` are asynchronous). We do this by keeping a queue of ```RenderRequest``` that we can probe for being completed. 

> We add the following ```struct``` to our `FrameCaptureManager.h` above the `UCLASS()` definition:

**FrameCaptureManager.h**
``` cpp
#include ...

[...]

struct FRenderRequestStruct{
    FIntPoint ImageSize;
    FRHIGPUTextureReadback Readback;
    FRenderCommandFence RenderFence;

	void* RawData = nullptr;
	int64 RawSize = 0;

	bool bIsComplete = false;

    FRenderRequestStruct(
        const FIntPoint& ImageSize,
        const FRHIGPUTextureReadback& Readback) :
            ImageSize(ImageSize),
            Readback(Readback) {}
};

[...]
UCLASS()
class ...
[...]
```
The ```FRHIGPUTextureReadback``` will hold the rendered results, e.g. color or depth values. The ```RenderFence``` is a neat feature of UE, letting you put a 'fence' into the render pipeline that can be checked to notify when it has passed the full rendering-pipeline. This gives a way to determine whether our render request is done and the buffers are safe to read.

> We need to add a ```TQueue``` as a data structure to keep track of our render requests:

**CaptureManger.h**
``` cpp
protected:
    // RenderRequest Queue
    TQueue<TSharedPtr<FRenderRequest>> RenderRequestQueue;
    TQueue<TSharedPtr<FRenderRequestStruct>> InThreadRenderRequestQueue;
```

#
## Implement placing render requests: 
This function will place a render request on the UE rendering pipeline asking the data captured from our `CaptureComponent` to be copied in our image buffer so that we can further process it.

**CaptureManger.h**
``` cpp
public:
    UFUNCTION(BlueprintCallable, Category = "ImageCapture")
    void CaptureNonBlocking();
```

**CaptureManger.cpp**
``` cpp
void AFrameCaptureManager::CaptureNonBlocking(){
    if(!IsValid(CaptureComponent)){
        UE_LOG(LogTemp, Error, TEXT("CaptureColorNonBlocking: CaptureComponent was not valid!"));
        return;
    }
    CaptureComponent->GetCaptureComponent2D()->TextureTarget->TargetGamma = GEngine->GetDisplayGamma();

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
```
With this, the image data will be stored in our queue of requests. Now we can think of storing it to disk. 

*Note: UFUNCTION(BlueprintCallable, Category = "ImageCapture") exposes this function to blueprint, so that you can easily test it*


# 
## Save Image Data to Disk
In each tick of the `FrameCaptureManager` we look up the first element of the `RenderQueue`. If it's `RenderFence` is completed and the data is ready to read, we proceed with saving the image to disk.

We need a procedure to write the data to disk, preferably without blocking our `GameThread`. 
We implement an [asynchronous](https://wiki.unrealengine.com/Using_AsyncTasks) procedure storing the data to disk.

**FrameCaptureManager.h**
``` cpp
UCLASS()
class ... {
[...]
};

// Below the AFrameCaptureManager class definition

class AsyncSaveImageToDiskTask : public FNonAbandonableTask
{
public:
    AsyncSaveImageToDiskTask(
		TSharedPtr<FRenderRequestStruct> InRenderRequest, 
		FString ImageName, 
		int32 Width, 
		int32 Height, 
		ERGBFormat RGBFormat,
		EImageFormat ImageFormat);
    ~AsyncSaveImageToDiskTask();

    void DoWork();

    FORCEINLINE TStatId GetStatId() const
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(AsyncSaveImageToDiskTask, STATGROUP_ThreadPoolAsyncTasks);
    }

private:
    TSharedPtr<FRenderRequestStruct> RenderRequest;  // Hold the shared pointer
    FString FileName;
    int32 Width;
    int32 Height;
    ERGBFormat RGBFormat;
	EImageFormat ImageFormat;
};
```

**FrameCaptureManager.cpp**
``` cpp
#include ...

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
    ImageWrapper->SetRaw(RenderRequest->RawData, RenderRequest->RawSize, Width, Height, RGBFormat, PixelDepth);

    // Compress the image 
    const TArray64<uint8>& CompressedData = ImageWrapper->GetCompressed(100);

    // Save the compressed image to disk
    FFileHelper::SaveArrayToFile(CompressedData, *FileName);

    //unlock the readback after the processing is done
    RenderRequest->Readback.Unlock();

    // Indicate that the processing is complete (using a flag)
    RenderRequest->bIsComplete = true;
}
```
We offload the entire image processing into `DoWork()`, from applying compression encoding to finally storing it to disk, to the asynchronous thread. 

*Note that this requires our ``RenderRequest`, more precisely its `RawData` image-buffer to stay available while the data is being stored to disk. Otherwise we encounter segmentation faults and the engine will crash!*


#
## Override the `Tick` function of the `FrameCaptureManager`:
Finally, we put everything together in the `Tick` function of our `FrameCaptureManager`. We wait for a `RenderRequest` to become available, we hand it over to an asynchronous thread for being stored to disk, and we monitor the progress of the asynchronous thread to finally release the `RenderRequest`'s buffers to garbage collection.


**FrameCaptureManager.h**
``` cpp
public:	

	// Called every frame
	virtual void Tick(float DeltaTime) override;
```
**FrameCaptureManager.cpp**
``` cpp
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
                IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

                // Get Data from Readback
                nextRenderRequest->RawSize = nextRenderRequest->ImageSize.X * nextRenderRequest->ImageSize.Y * sizeof(FColor);
                
                int32 RowPitchInPixels;
                nextRenderRequest->RawData = nextRenderRequest->Readback.Lock(RowPitchInPixels, nullptr); // Pass RowPitchInPixels and no buffer size
                
                // Generate image name
                FString fileName = "";
                fileName = FPaths::ProjectSavedDir() + SubDirectoryName + "/img" + "_" + ToStringWithLeadingZeros(ImgCounter, NumDigits);
                fileName += GetFileEnding(ImageFormat);

                // Pass the raw data, filename, width, height, and format to the async task
                RunAsyncImageSaveTask(nextRenderRequest, fileName, 1920, 1080, ERGBFormat::RGBA, EImageFormat::PNG);

                // Security check                    
                if(VerboseLogging && !fileName.IsEmpty()){
                    UE_LOG(LogTemp, Warning, TEXT("%s"), *fileName);
                }
                
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
```

For testing purposes we can call the `CaptureColorBlocking()` from the `LevelBlueprint` by attaching it to a button pressed event.

![](https://github.com/TimmHess/UnrealImageCapture/blob/master/gfx/Debug_level_blueprint_capture.png)

The captured images will now be saved into your project's `Saved` directory.


#
# Capturing Annotations
To be able to render color and segmentation at the same time, we need one additional `SceneCapture2D` component in our scene for each *type* of capture you want. Attach it to the `SceneCapture2D` already placed in your scene (from earlier in this tutorial). Make sure both have exactly the same location, rotation, and perspective settings. Otherwise your annotation will not match.

# Capturing Object Segmentation Masks
To get labels for our images we will add a second `CaptureComponent` equipped with a `PostProcessMaterial` that visualizes `CustomDepth`. The `CustomDepthStencil` is settable for each actor in the scene, effectively letting us label and visualize categories of, as well as individual, actors.

## 1. Enable and Set-Up Custom Depth Stencils
Find the **ProjectSettings** in your editor and search for *stencil* which will bring up `Custom Depth-Stencil Pass`. Switch this option from `Enabled` to `Enabled with Stencil`. 

You can set the custom depth in editor or from code. For simplicity I chose the editor. Place an arbitrary object(MeshActor) into the scene, and search for `custom depth` in its details panel. Under `Rendering` enable `Render CustomDepth Pass`, and set `CustomDepth Stencil Value` to whatever you like. For illustration purposes set it to 200.

*Note: Make sure you have custom depth enabled with stencils in your project settings.*
![](https://github.com/TimmHess/UnrealImageCapture/blob/master/gfx/Enable_custom_depth_in_project.png)

![](https://github.com/TimmHess/UnrealImageCapture/blob/master/gfx/Apply_custom_depth_stencil.png)


## 2. Setting Up The PostProcess Material
Add a new `Material` to your project content. (I will call it `PP_Segmentation`)

Click on the material's output node and switch `MaterialDomain` from `Surface` to `PostProcess`.

In the same panel search for "alpha" and activate `Output Alpha`. Set this value to `1.0` in the Material node.

Right-click to open the node search and type `SceneTexture`, select the node from `Texture`-Category.

In the details of this node, select `CustomStencil` as `SceneTextureId`.

Add a `Division` node and connect the `SceneTexture`'s `Color` output to the division node. Set the division to be by 255. 

*Note: This is needed because the image buffer seems to be float valued, leading to values > 1 having no meaning, as image information ranges from 0.0 to 1.0.*

Apply and save the material.

![](https://github.com/TimmHess/UnrealImageCapture/blob/master/gfx/pp_segmentation.png)


**FrameCaptureManager.h**
``` cpp
public:
    // PostProcessMaterial used for segmentation
    UPROPERTY(EditAnywhere, Category="Segmentation Setup")
    UMaterial* PostProcessMaterial = nullptr;
```
**FrameCaptureManager.cpp**
``` cpp
void AFrameCaptureManager::SetupCaptureComponent(){
    [...] // previous function code

    // Assign PostProcess Material if assigned
    if(PostProcessMaterial){ // check nullptr
        CaptureComponent->GetCaptureComponent2D()->AddOrUpdateBlendable(PostProcessMaterial);
    } else {
        UE_LOG(LogTemp, Log, TEXT("No PostProcessMaterial is assigend"));
    }
}
```

You can now reference the ``PostProcessMaterial` in the details panel of the `FrameCaptureManager` in the editor just like before the `SceneCapture2D`.


#
# Setup a Depth Capture
Capturing `SceneDepth` information has one important caveat - it requires storing float images (.exr).
Every thing else follows like [Object Segmentation](#capturing-object-segmentation-masks). We create a `PostProcessMaterial` to access the respectie GPU buffer. 

![](https://github.com/TimmHess/UnrealImageCapture/blob/master/gfx/pp_depth.png)

Luckily UnrealEngine is perfectly capable of storing float images and even provides the .exr file format in its `IImageWrapper`. However, there are three places in our code where we need to take care to handle the float format correctly. Missing any of those will result in segmentation faults of the engine.

## 1. Accounting for the RawSize correctly
We adjust the code that allocates the RawSize of our captured images to accomodate the float values using 16 bits. 

*Note that I decide for the RawSize to used based on the ImageFormat. This I do not explain in the tutorial - please check the source.*

**FrameCaptureManager.cpp**
``` cpp
void AFrameCaptureManager::SetupCaptureComponent(){
    [...] 

    // Get Data from Readback
    nextRenderRequest->RawSize = nextRenderRequest->ImageSize.X * nextRenderRequest->ImageSize.Y * sizeof(FColor);
    if(ImageFormat == ECustomImageFormat::EXR){ // handle float case
        nextRenderRequest->RawSize = nextRenderRequest->ImageSize.X * nextRenderRequest->ImageSize.Y * sizeof(FFloat16Color); //FLOAT
    }

    [...] 
}
```

## 2. Adjust the correct RenderTarget format
**FrameCaptureManager.cpp**
``` cpp
void AFrameCaptureManager::SetupCaptureComponent(){
    [...] 

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

    [...] 
}
```

## 3. Adjust the PixelDepth when storing
**FrameCaptureManager.cpp**
``` cpp
void AsyncSaveImageToDiskTask::DoWork(){
    [...] 

    // Set the raw data 
    int32 PixelDepth = 8;
    if(ImageFormat == EImageFormat::EXR){ // Adjust pixel depth for EXR (float) data  // Has to be EImageFormat because already converted..
        PixelDepth = 16;
    }
    UE_LOG(LogTemp, Warning, TEXT("PixelDepth: %d"), PixelDepth);
    ImageWrapper->SetRaw(RenderRequest->RawData, RenderRequest->RawSize, Width, Height, RGBFormat, PixelDepth);

    [...] 
}
```


#
# Enable Lumen on SceneCapture2D
In my tests using UE5.5.3+ the `SceneCapture2D` was fully capable of rendering scenes with Lumen. However you need might need to actiate it in the `SceneCapture2D` itself as it was not listening to the `PostProcessVolume` in my scene.

![](https://github.com/TimmHess/UnrealImageCapture/blob/master/gfx/Activate_Lumen.png)


