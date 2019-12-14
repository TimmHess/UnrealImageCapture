# Image Capturing With UnrealEngine4 For Deep Learning

# A Small Introduction
UnrealEngine4 is known to be a powerful tool to create virtual worlds as it is a AAA production game engine. Generating temporally consistent data, with automatic pixel-wise annotations from complex scenes, such as traffic scenarios, is a capability worth leveraging for machine learning, or more explicitly deep learning, contexts, and has been explored for a series of projects already. There are plugins available that handle rendering images from UE4 to disk at runtime, such as [UnrealCV](https://unrealcv.org/)  and [AirSim](https://github.com/microsoft/AirSim). 

When I was setting up a scene for my research these plugins were just not yet supporting the latest engine version I wanted/needed to use for various feature reasons, and I was missing a place where knowledge of how to setup a capturing component for rendering images to disk myself was explains for non graphics-programmers. There is but of course a lot of source code available from the projects mentioned earlier and there are a lot of postings scattered across multiple platforms explaining parts of the problem and giving code for possible solutions even though they may be meant for a different issue.\
[Image of Scene with Segmentation]\
In this post I want to condense my findings on how to implement a component to capture images to disk from an arbitrary UE4 scene **from scratch** lowering the bar for UE4 beginners. This will include:\
* Rendering images at high FPS without blocking the UE4 rendering thread
* Rendering segmentation (or other graphics buffers) at the same time

I will try to list all the postings that helped my put this together as they deserve a lot of credit, especially AirSim and UnrealCV, and of course Epic themselves giving full access to their source for the engine. Also this guide is not meant be perfect or complete, there is always room to enhance this methods, I just found my method to be working for all my current projects and would like to share it with you.

**Disclaimer: I do not own any of the code. I merely condensed the sources already available online for easier use!**\
**Also huge thanks to the UE4 AnswerHub community!**
* Link to rendering to image
* Link to async saving
* Link to EpicGit
* Link to UECV
* Link to AirSim

# How to Save Images to Disk In UE4 (without blocking the rendering thread)
## Prerequisite
You will need a UE4 c++ project. I will go through the code step by step so that it is hopefully easier to implement each step as you are following along. The full source code is placed in this git.

#
## Setup A ColorCapture Component
The component I am using for capturing is the ```SceneCaptureComponent2D``` provided as default by the UE4Editor. Placing one of these into your scene will give you a ```ASceneCaptureComponent``` which is its Actor instance. It basically behaves like any other camera component, except for having a viewport that is not restricted by your computer's monitor or main camera viewport, giving the possibility to render images larger than than the actual screen resolution.

Add a ```CaptureManager``` class of type Actor to your project.

In the ```CaptureManager.h``` we add the following:\
**CaptureManager.h**
``` cpp
#pragma once
class ASceneCapture2D; //forward declaration

#include ...
```
and to our public variables:
``` cpp
// Color Capture  Components
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Capture")
ASceneCapture2D* ColorCaptureComponents;
```

This enables you to assign a ```CaptureComponent2d``` to your ```CaptureManager``` code.

Compile and place a ```CaptureManager``` in your scene. As it does not have any primitive to render you will only see it in the editor's outline. In the details panel of the placed ```CaptureManager``` you can now see the ```ColorCaptureComponent``` assigned to ```None```. From the drop down menu select the ```CaptureComponent2D``` you already placed in the scene.

Back to code: We will now prepare our yet "naked" CaptureComponent2D for capturing images, creating and assigning a RenderTarget, which is basically a Texture to store our image data to, and setting the camera properties. *Note: You could also do the this in the Editor but if you deal with, for example, multiple capture components etc., you may find it handy to not worry about creating and assigning all the components by hand*.

Create a setup function to put all your setup code for the CaptureComponents in the CaptureManger:

**CaptureManager.h**
``` cpp
protected:

    void SetupColorCaptureComponent(ASceneCapture2D* captureComponent);
```
**CaptureManager.cpp**
``` cpp
#include ...

#include "Runtime/Engine/Classes/Components/SceneCaptureComponent2D.h"
#include "Runtime/Engine/Classes/Engine/TextureRenderTarget2D.h"
#include "Engine.h"
#include <Runtime/Engine/Classes/Kismet/GameplayStatics.h>
#include <Runtime/Engine/Public/ShowFlags.h>

#include "RHICommandList.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"


void ACaptureManager::SetupColorCaptureComponents(ASceneCapture2D* captureComponent){
    // Create RenderTargets
    UTextureRenderTarget2D* renderTarget2D = NewObject<UTextureRenderTarget2D>();

    // Set FrameWidth and FrameHeight
    renderTarget2D->TargetGamma = 1.2f;// for Vulkan //GEngine->GetDisplayGamma(); // for DX11/12

    // Setup the RenderTarget capture format
    renderTarget2D->InitAutoFormat(256, 256); // some random format, got crashing otherwise
    int32 frameWidth = 640;
    int32 frameHeight = 480;
    renderTarget2D->InitCustomFormat(frameWidth, frameHeight, PF_B8G8R8A8, true); // PF_B8G8R8A8 disables HDR which will boost storing to disk due to less image information
    renderTarget2D->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
    renderTarget2D->bGPUSharedFlag = true; // demand buffer on GPU

    // Assign RenderTarget
    captureComponent->GetCaptureComponent2D()->TextureTarget = renderTarget2D;

    // Set Camera Properties
    captureComponent->GetCaptureComponent2D()->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    captureComponent->GetCaptureComponent2D()->ShowFlags.SetTemporalAA(true);
    // lookup more showflags in the UE4 documentation.. 
}
```
And call the code during `BeginPlay` of the `CaptureManager`

**CaptureManager.cpp**
``` cpp
// Called when the game starts or when spawned
void ACaptureManager::BeginPlay()
{
	Super::BeginPlay();

    // Setup CaptureComponents
    SetupColorCaptureComponent(ColorCaptureComponents);
}

```
Now that we have a RenderTarget applied to our CaptureComponent we can read its data to disk.

#
## Organize RenderRequests
We do this by basically re-implementing UE4's code for taking screenshots with the addition of not flushing our rendering pipeline to prevent rendering hiccups dropping the framerate to 3 - 5 FPS. 

This comes with the price of needing to handle the waiting before an image is done being copied from GPU so that we do not read an old or uninitialized buffer (Render Thread and GameThread are not synchronous). We do this by keeping a queue of ```RenderRequest``` that we can probe for being completed. 

We add the following ```struct``` to our CaptureManager class:

**CaptureManager.h**
``` cpp
#include ...

[...]

USTRUCT()
struct FRenderRequest{
    GENERATED_BODY()

    TArray<FColor> Image;
    FRenderCommandFence RenderFence;
    bool isPNG;

    FRenderRequest(){
        isPNG = false;
    }
};

[...]
UCLASS()
class ...
[...]
```
The ```Image``` will be the color buffer our CaptureComponent writes to. ```RenderFence``` is a neat feature of UE4 letting you put a  "fence" into the render pipeline that knows when it has passed the full pipeline, giving a way to assess whether our render command must have been passed as well. The ```isPNG``` flag will be important later when we want to also store semantic labels which should not be stored as JPEG as the compression introduces small errors into the color/label data...

Also we need to add our ```TQueue```, keeping track of our render requests:

**CaptureManger.h**

``` cpp
protected:
    // RenderRequest Queue
    TQueue<FRenderRequest*> RenderRequestQueue;
```

#
## Implement the image capturing function: 
This function will place a render request on the UE4 rendering pipeline asking the data captured from our CaptureComponent to be copied in our Image buffer so that we can further process it in GameThread.

**CaptureManger.h**

``` cpp
public:
    UFUNCTION(BlueprintCallable, Category = "ImageCapture")
    void CaptureColorNonBlocking(ASceneCapture2D* CaptureComponent, bool IsSegmentation=false);
```

**CaptureManger.cpp**

``` cpp
void ACaptureManager::CaptureColorNonBlocking(ASceneCapture2D* CaptureComponent, bool IsSegmentation){
    // Get RenderContext
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

    // Notify new task in RenderQueue
    RenderRequestQueue.Enqueue(renderRequest);

    // Set RenderCommandFence
    renderRequest->RenderFence.BeginFence();
}
```

With this the image data is already stored in our queue, we now need to store it to disk. *Note: UFUNCTION(BlueprintCallable, Category = "ImageCapture") exposes this function to blueprint, so that you could easily test it*

# 
## Save Image Data to Disk
To do so, each tick of the `CaptureManager` we look up the first element of the `RenderQueue`, if its `RenderFence` is completed then we save the image to disk, else we do nothing.

The last thing we need is a procedure to write to disk, this time without blocking our game thread. For this we implement an asynchronous procedure storing
the data to disk.
[Link to UnrealWiki](https://wiki.unrealengine.com/Using_AsyncTasks)

**CaptureManager.h**
``` cpp
UCLASS()
class ... {
[...]
};

class AsyncSaveImageToDiskTask : public FNonAbandonableTask{
    public:
        AsyncSaveImageToDiskTask(TArray<uint8> Image, FString ImageName);
        ~AsyncSaveImageToDiskTask();

    // Required by UE4!
    FORCEINLINE TStatId GetStatId() const{
        RETURN_QUICK_DECLARE_CYCLE_STAT(AsyncSaveImageToDiskTask, STATGROUP_ThreadPoolAsyncTasks);
    }

protected:
    TArray<uint8> ImageCopy;
    FString FileName = "";

public:
    void DoWork();
};
```

**CaptureManager.cpp**
``` cpp
#include ...

// Static ImageWrapperModule to prevent reloading -> this thing does not like to be reloaded..
static IImageWrapperModule &ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

[...]

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
```
And a call from the `CaptureManager` to start the async saving process:

**CaptureManager.h**
``` cpp
protected:
    // Creates an async task that will save the captured image to disk
    void RunAsyncImageSaveTask(TArray<uint8> Image, FString ImageName);
```

**CaptureManager.cpp**
``` cpp
void ACaptureManager::RunAsyncImageSaveTask(TArray<uint8> Image, FString ImageName){
    (new FAutoDeleteAsyncTask<AsyncSaveImageToDiskTask>(Image, ImageName))->StartBackgroundTask();
}
```

#
## Override the `Tick` function of the `CaptureManager`:

**CaptureManager.h**
``` cpp
public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
```
**CaptureManager.cpp**
``` cpp
// Called every frame
void ACaptureManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

    // Read pixels once RenderFence is completed
    if(!RenderRequestQueue.IsEmpty()){
        // Peek the next RenderRequest from queue
        FRenderRequest* nextRenderRequest = nullptr;
        RenderRequestQueue.Peek(nextRenderRequest);

        int32 frameWidth = 640;
        int32 frameHeight = 480;

        if(nextRenderRequest){ //nullptr check
            if(nextRenderRequest->RenderFence.IsFenceComplete()){ // Check if rendering is done, indicated by RenderFence
                // Decide storing of data, either jpeg or png
                if(nextRenderRequest->isPNG){
                    //Generate image name
                    FString fileName = FPaths::ProjectSavedDir();
                    fileName += ".png"; // Add file ending

                    // Prepare data to be written to disk
                    static TSharedPtr<IImageWrapper> imageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG); //EImageFormat::PNG //EImageFormat::JPEG
                    imageWrapper->SetRaw(nextRenderRequest->Image.GetData(), nextRenderRequest->Image.GetAllocatedSize(), frameWidth, frameHeight, ERGBFormat::BGRA, 8);
                    const TArray<uint8>& ImgData = imageWrapper->GetCompressed(5);
                    RunAsyncImageSaveTask(ImgData, fileName);
                } else{
                    UE_LOG(LogTemp, Log, TEXT("Started Saving Color Image"));
                    // Generate image name
                    FString fileName = FPaths::ProjectSavedDir();
                    fileName += ".jpeg"; // Add file ending

                    // Prepare data to be written to disk
                    static TSharedPtr<IImageWrapper> imageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG); //EImageFormat::PNG //EImageFormat::JPEG
                    imageWrapper->SetRaw(nextRenderRequest->Image.GetData(), nextRenderRequest->Image.GetAllocatedSize(), frameWidth, frameHeight, ERGBFormat::BGRA, 8);
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
```

# 

For test purposes we can call the `CaptureColorNonBlocking()` from the `LevelBlueprint` attaching it to a button press.

[Image of the level blueprint]

The images captured will now be saved into your project's `Saved` directory.

#
#

# Capturing Segmentation
To get labels for our images we will add a second CaptureComponent equipped with a `PostProcessMaterial` that will render `CustomDepth` that is settable for each actor in the scene, effectively letting us label and visualize categories of actors.

## Enable Custom Depth Stencils
Find the **ProjectSettings** in your editor and search for *stencil* which will bring up `Custom Depth-Stencil Pass`. Switch this option from `Enabled` to `Enabled with Stencil`. 


## Setting Up The PostProcess Material
Add a new `Material` to your project content. (I will call it `PP_Segmentation`)

Click on the material's output node and switch `MaterialDomain` from `Surface` to `PostProcess`.

Right-click to open the node search and type `SceneTexture`, select the node from `Texture`-Category.

In the details of this node, select `CustomStencil` as `SceneTextureId`.

Add a `Division` node and connect the `SceneTexture`'s `Color` output to the division node. Set the division to be by 255. *Note: This is needed because the image buffer seems to be float valued, leading to values > 1 having no meaning, as image information ranges from 0.0 to 1.0.*

Apply and save the material.

## Setting up Custom-Depth Stencils
You can set the custom-depth in editor or from code, for simplicity I will this time use the editor. Place an arbitrary object(MeshActor) into the scene, and search for `custom depth` in its details panel. Under `Rendering` enable `Render CustomDepth Pass`, and set `CustomDepth Stencil Value` to whatever you like. For illustration purposes set it to 200.

#

## Organize the Segmentation CaptureComponent
To be able to render color and segmentation at the same time, we need a second `SceneCapture2D` component in our scene. To not worry about placement and setup later on we will spawn this component by code, aligning it to our ColorCapture, and add our post process material.

To add the post process material we first need access to it by code. We could do a search for it through our project content, but since this would be done by the name of the material I found it to be a rather unsafe method. I prefer to add a reference to the material to the `CaptureManager`

**CaptureManager.h**
``` cpp
public:
    // PostProcessMaterial used for segmentation
    UPROPERTY(EditAnywhere, Category="Segmentation Setup")
    UMaterial* PostProcessMaterial = nullptr;
```
You can now reference the post process material in the details panel of the `CaptureManager` in the editor just like before the `SceneCapture2D`.

# 
## Spawn the SegmentationCapture Component

Add code to spawn the new `SceneCapture2D` component and get settings from the ColorCapture:

**CaptureManager.h**
``` cpp
protected:
    ASceneCapture2D* SegmentationCapture = nullptr;

    void SpawnSegmentationCaptureComponent(ASceneCapture2D* ColorCapture);
```

**CaptureManager.cpp**
``` cpp
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
```

#
## Setup the SegmentationComponent

**CaptureManager.h**
``` cpp
protected:
    void SetupSegmentationCaptureComponent(ASceneCapture2D* SegmentationCapture);
```
**CaptureManager.cpp**
``` cpp
void ACaptureManager::SetupSegmentationCaptureComponent(ASceneCapture2D* ColorCapture){
    // Spawn SegmentationCaptureComponents
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
```

## Add the function call to BeginPlay

**CaptureManager.cpp**
``` cpp
void ACaptureManager::BeginPlay()
{
	Super::BeginPlay();

    SetupColorCaptureComponent(ColorCaptureComponents);
    SetupSegmentationCaptureComponent(ColorCaptureComponents);
}
```

#

To save the image information from SegmentationCapture we can simply use the `CaptureColorNonBlocking()` method. Be sure to set `isSegmentation = true` to get PNG compressed data.

#
#

# Known Issues
The `IImageWrapperModule`'s wrapping of the data is still done in GameThread rather than in a async call, which can actually consume more runtime than the saving to disk. Simply pushing the WrapperModule into the async procedure does suffice since 1) it is a shared pointer, 2) the `ImageWrapperModule.CreateImageWrapper(...)` needs to be called from GameThread. I am grateful for any ideas on that..

It is possible that an image is saved every game tick at high fps. If saving to disk is actually slower than the delta time of the game tick another call to the shared IImageWrapper is made while its buffer is read for saving to disk. This results in a game crash. This should be fixable by adding semaphores, I just did not have the time to test this yet.