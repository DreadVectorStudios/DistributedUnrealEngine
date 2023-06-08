// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PixelStreamingModule.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Containers/Array.h"
#include "PixelStreamingBlueprints.generated.h"

UCLASS()
class PIXELSTREAMING_API UPixelStreamingBlueprints : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
      /**
     * Send a specified byte array over the WebRTC peer connection data channel. The extension and mime type are used for file reconstruction on the front end 
     * 
     * @param   ByteArray       The raw data that will be sent over the data channel
     * @param   MimeType        The mime type of the file. Used for reconstruction on the front end
     * @param   FileExtension   The file extension. Used for file reconstruction on the front end
     */
    UFUNCTION(BlueprintCallable, Category = "Pixel Streaming Transmit")
    static void SendByteArray(TArray<uint8> ByteArray, FString MimeType, FString FileExtension);

    /**
     * Send a specified file over the WebRTC peer connection data channel. The extension and mime type are used for file reconstruction on the front end 
     * 
     * @param   FilePath        The path to the file that will be sent
     * @param   MimeType        The mime type of the file. Used for file reconstruction on the front end
     * @param   FileExtension   The file extension. Used for file reconstruction on the front end
     */
    UFUNCTION(BlueprintCallable, Category = "Pixel Streaming Transmit")
    static void SendFile(FString Filepath, FString MimeType, FString FileExtension);   
};