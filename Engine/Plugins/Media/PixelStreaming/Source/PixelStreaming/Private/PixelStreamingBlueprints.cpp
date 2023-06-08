// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingBlueprints.h"
#include "Misc/FileHelper.h"


void UPixelStreamingBlueprints::SendByteArray(TArray<uint8> ByteArray, FString MimeType, FString FileExtension) 
{
    IPixelStreamingModule* Module = FPixelStreamingModule::GetModule();
    if(Module)
    {
        Module->SendData(ByteArray, MimeType, FileExtension);
    }
}


void UPixelStreamingBlueprints::SendFile(FString FilePath, FString MimeType, FString FileExtension)
{
    IPixelStreamingModule* Module = FPixelStreamingModule::GetModule();
    if(Module)
    {
        TArray<uint8> ByteData;
	    bool bSuccess = FFileHelper::LoadFileToArray(ByteData, *FilePath);
        if(bSuccess)
        {
            Module->SendData(ByteData, MimeType, FileExtension);
        }
        else
        {
            UE_LOG(PixelStreamer, Error, TEXT("FileHelper failed to load file data"));
        }
    }
}