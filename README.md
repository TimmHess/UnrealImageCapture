# Image Capturing With UnrealEngine 4 or 5 For Deep Learning


![alt text](https://github.com/TimmHess/UnrealImageCapture/blob/master/gfx/CaptureResult.png)

#
__To review the code please find it in the Plugin directory!__ (The CaptureToDisk is currently not up to date, I will try to find time to fix this in the near future!)

# A Small Introduction
UnrealEngine is known to be a powerful tool to create virtual worlds as it is a highly valued AAA production game engine. Generating temporally consistent data with automatic pixel-wise annotations from complex scenes, such as traffic scenarios, is a capability worth leveraging, especially for training and validation of machine learning, or more explicitly deep learning, applications, and has been explored in a variety of projects already. Already, there are plugins available that allow rendering images from UE4 to disk at runtime, such as prominently [UnrealCV](https://unrealcv.org/) and [AirSim](https://github.com/microsoft/AirSim). This repository aims to be a tutorial that demonstrates such an 'image capturing' mechanism in detail fo you to understand its inner workings, and in turn enable you to reuse it in a custom fashions that suit the needs of your project. 

*When I was setting up scenes for my research the formerly mentioned plugins were just not yet supporting the latest engine versions I wanted/needed to use, and also I was missing a place where the knowledge of how to render images to disk myself was explains for non-advanced graphics-programmers. Of course, there are lots of sources for code available online and also there are community blog-entries scattered across multiple platforms explaining parts of the problem as well as possible solutions, even though they typically are targeting different issues.*

In this repository I want to condense my findings on how to implement a component to capture images to disk from an arbitrary UE4 (which fortunately apply likewise to UE5) scene **from scratch** lowering the bar for UE novices. This will include:
* Rendering images at high FPS without blocking the UE rendering thread
* Rendering segmentation (or other graphics buffers) at the same time
 

**Disclaimer: I do not own any of the code. Merely, IO condensed the sources already available online for easier use and provide an overview to the general functionality of this particular approach!**

**Kudos to the UE4 AnswerHub community!**

# Plugin Support
The general idea of this repository is to communicate a possible setup for custom image capturing in code. This shall provide a baseline for further development to adapt the code to ones individual needs. 

I understand that Unreal's Blueprint interface is powerful and some people have their reasons not to dive into C++ development with UE.

This is why now there is also a **Plugin** version of the code available. However, it will still need you to have a C++ project rather than Bluerpint-onle.

To incorporate the Plugin in to your project: Create a **Plugins** directory in your project and copy the ```\UnrealImageCapture\Plugins\CameraCaptureToDisk``` directory. Load the plugin in your project, if not automatically done by the editor, and place the `CameraCaptureManager_BP`, which is to be found in the plugin's contents, in the scene and fill in its required slots as depicted below. This will require you to place a ```SceneCapture2D``` in your scene.
A ```PostProcessMaterial``` for segmentation is also located in the plugin's contents.

![alt text](https://github.com/TimmHess/UnrealImageCapture/blob/master/gfx/ColorCaptureOutline.png)
![alt text](https://github.com/TimmHess/UnrealImageCapture/blob/master/gfx/SegmentationCaptureOutline.png)

**Currently one should use JPEG for Color and PNG for Pixel-Segmentation** 

An image-capturing-command can be triggered from Blueprint as exemplary depicted for the Level-Blueprint below:

![alt text](https://github.com/TimmHess/UnrealImageCapture/blob/master/gfx/BlueprintCapture.png)


# How to Save Images to Disk In UE4 (without blocking the rendering thread)
I will go through the code step-by-step so that hopefully it will be easier to implement each step as you are following along. The full source code is placed in this repository.

## Prerequisite
You will need a UE4 (or UE5) C++ project. 

Also, you might have to add a few packages to your `'YourProjectName'.Build.cs` file. These are part of UnrealEngine, however, sometimes they are not added automatically resulting in unpleasant linker errors. Find the `'YourProjectName'.Build.cs` file in the `Source/'YourProjectName/` directory, and add or extend it to include all modules listed in the following line:

``` cpp
PublicDependencyModuleNames.AddRange(new string[] {"Core", "CoreUObject", "Engine", "InputCore", "ImageWrapper", "RenderCore", "Renderer", "RHI" });
```

#
## Setup A ColorCapture Component
I am using a ```SceneCaptureComponent2D``` as the basis for capturing images. Placing one of these into your scene will give you an ```ASceneCaptureComponent``` which is its `Actor` instance. It basically behaves like any other camera component, but its viewport is not restricted by your computer's monitor or main camera viewport. This provides us the possibility to render images of arbitrary resolution independent from the actual screen resolution.

#

> Add a ```CaptureManager``` class of type Actor to your project.

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

> Compile and place a ```CaptureManager``` in your scene. 

As it does not have any primitive to render you will only see it in the editor's outline. In the details panel of the placed ```CaptureManager``` you can now see the ```ColorCaptureComponent``` assigned to ```None```. From the drop down menu select the ```CaptureComponent2D``` you already placed in the scene.

Back to code: We will now prepare our yet "naked" `CaptureComponent2D` class for capturing images, creating and assigning a `RenderTarget`, which is basically a `Texture` to store our image data to, and setting the camera properties. *Note: You could also do this in the Editor but if you deal with, i.e. multiple capture components, you may find it handy not to worry about creating and assigning all the components by hand!*

> Create a setup function to put all your setup code for the CaptureComponents in the CaptureManger:

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
> Call the code during `BeginPlay` of the `CaptureManager`

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
Now that because we have a `RenderTarget` applied to our `CaptureComponent` we can read its data to disk.

#
## Organize RenderRequests
We do this by basically re-implementing UE4's code for taking screenshots. Importantly, with the addition of not flushing our rendering pipeline. This prevents rendering *hiccups* that drop the framerate to 3 - 5 FPS. 

This addition will come with the price of needing to handle 'waiting times' before an image is done and copied from GPU. This is important to prevent reading old or uninitialized buffers (remember that `RenderThread` and `GameThread` are asynchronous). We do this by keeping a queue of ```RenderRequest``` that we can probe for being completed. 

> We add the following ```struct``` to our `CaptureManager` class:

**CaptureManager.h**
``` cpp
#include ...

[...]

struct FRenderRequest{
    FIntPoint ImageSize;
    FRHIGPUTextureReadback Readback;
    FRenderCommandFence RenderFence;

    FRenderRequest(
        const FIntPoint& ImageSize,
        const FRHIGPUTextureReadback& Readback) :
            ImageSize(ImageSize),
            Readback(Readback) {}
};

[...]
UCLASS(Blueprintable)
class ...
[...]
```

The ```FRHIGPUTextureReadback``` will hold the rendered results, e.g. color or depth values. The ```RenderFence``` is a neat feature of UE, letting you put a 'fence' into the render pipeline that can be checked to notify when it has passed the full pipeline. This gives a way to determine whether our render command has passed as well.

> We need to add a ```TQueue``` as a data structure to keep track of our render requests:

**CaptureManger.h**

``` cpp
protected:
    // RenderRequest Queue
    TQueue<TSharedPtr<FRenderRequest>> RenderRequestQueue;
```

#
## Implement the image capturing function: 
This function will place a render request on the UE rendering pipeline asking the data captured from our `CaptureComponent` to be copied in our Image buffer so that we can further process it in `GameThread`.

**CaptureManger.h**

``` cpp
public:
    UFUNCTION(BlueprintCallable, Category = "ImageCapture")
    void CaptureNonBlocking();
```

**CaptureManger.cpp**

``` cpp
void ACaptureManager::CaptureNonBlocking(){
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
```

With this, the image data is already stored in our queue, and we now need to store it to disk. *Note: UFUNCTION(BlueprintCallable, Category = "ImageCapture") exposes this function to blueprint, so that you can easily test it*

# 
## Save Image Data to Disk
To do so, in each tick of the `CaptureManager` we look up the first element of the `RenderQueue`, if it's `RenderFence` is completed and also whether the data is ready to read. Only if both check-out true, we proceed with saving the image to disk.

The last thing we need is a procedure to write the data to disk, preferably without blocking our `GameThread`. 
>We implement an [asynchronous](https://wiki.unrealengine.com/Using_AsyncTasks) procedure storing the data to disk.

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
> And a call from the `CaptureManager` to start the async saving process:

**CaptureManager.h**
``` cpp
protected:
    // Creates an async task that will save the captured image to disk
    void RunAsyncImageSaveTask(TArray<uint8> Image, FString ImageName);
```

**CaptureManager.cpp**
``` cpp
void ACaptureManager::RunAsyncImageSaveTask(TArray64<uint8> Image, FString ImageName){
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
```

# 

For testing purposes we can call the `CaptureColorBlocking()` from the `LevelBlueprint` by attaching it to a button pressed event.

[Image of the level blueprint]

The captured images will now be saved into your project's `Saved` directory.

#
#

# Capturing Segmentation
To get labels for our images we will add a second `CaptureComponent` equipped with a `PostProcessMaterial` that visualizes `CustomDepth`. The `CustomDepthStencil` is settable for each actor in the scene, effectively letting us label and visualize categories of, as well as individual, actors.

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
You can set the custom depth in editor or from code. For simplicity I chose the editor. Place an arbitrary object(MeshActor) into the scene, and search for `custom depth` in its details panel. Under `Rendering` enable `Render CustomDepth Pass`, and set `CustomDepth Stencil Value` to whatever you like. For illustration purposes set it to 200.

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
The `IImageWrapperModule`'s wrapping of the data is still done in `GameThread` rather than in an async call, which can actually consume more runtime than the saving to disk. Simply pushing the WrapperModule into the async procedure does suffice since 1) it is a shared pointer, 2) the `ImageWrapperModule.CreateImageWrapper(...)` needs to be called from GameThread. I am grateful for any ideas on that..

It is possible that an image is saved every game tick at high fps. If saving to disk is actually slower than the delta time of the game tick another call to the shared `IImageWrapper` is made while its buffer is read for saving to disk. This results in a game crash. This should be fixable by adding semaphores, I just did not have the time to test this yet.
