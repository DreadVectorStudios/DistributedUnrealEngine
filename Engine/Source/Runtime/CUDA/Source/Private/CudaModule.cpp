// Copyright Epic Games, Inc. All Rights Reserved.
#include "CudaModule.h"

#include "Misc/CoreDelegates.h"

#if PLATFORM_SUPPORTS_CUDA
#include "VulkanRHIPrivate.h"
#endif

#if PLATFORM_WINDOWS
	#include "DynamicRHI.h"
    #include "Windows/AllowWindowsPlatformTypes.h"
	#include "dxgi.h"
    #include "d3d12.h"
    #include "d3d11.h"
    #include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "HAL/UnrealMemory.h"

DEFINE_LOG_CATEGORY(LogCUDA);

// CUDA 11.0.0 is our minimum required version
#define CUDA_MINIMUM_REQUIRED_VERSION 11000

void FCUDAModule::StartupModule()
{
	// Attempt to load the CUDA library and wire up our post-init delegate if loading was successful
	if (LoadCuda() == true)
	{
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FCUDAModule::InitCuda);
		UE_LOG(LogCUDA, Display, TEXT("CUDA module ready pending PostEngineInit."));
	}
}

void FCUDAModule::ShutdownModule()
{
	UnloadCuda();
}

bool FCUDAModule::IsAvailable()
{
	return (DriverLibrary != nullptr);
}

const CUDA_DRIVER_API_FUNCTION_LIST* FCUDAModule::DriverAPI()
{
	return &DriverApiPtrs;
}

bool FCUDAModule::LoadCuda()
{
	// Ensure we do not load the shared library for the CUDA Driver API twice
	UnloadCuda();
	
	// Attempt to load the shared library for the CUDA Driver API
	DriverLibrary = OpenCudaDriverLibrary();
	if (DriverLibrary)
	{
		// Attempt to retrieve the list of function pointers for the Driver API
		LoadCudaDriverFunctions(DriverLibrary, &DriverApiPtrs);
		
		// Verify that we were able to load the function pointers and that the minimum CUDA version requirement is met
		int driverVersion = -1;
		if (DriverApiPtrs.cuDriverGetVersion != nullptr)
		{
			if (DriverApiPtrs.cuDriverGetVersion(&driverVersion) == CUDA_SUCCESS)
			{
				if (driverVersion >= CUDA_MINIMUM_REQUIRED_VERSION)
				{
					return true;
				}
			}
		}
		
		// If we reached this point then loading failed
		UnloadCuda();
	}
	
	return false;
}

void FCUDAModule::UnloadCuda()
{
	// Close shared library for the CUDA Driver API if it is currently loaded
	if (DriverLibrary != nullptr)
	{
		CloseCudaLibrary(DriverLibrary);
		DriverLibrary = nullptr;
	}
	
	// Zero-out our list of function pointers
	FMemory::Memset(&DriverApiPtrs, 0, sizeof(CUDA_DRIVER_API_FUNCTION_LIST));
}

bool FCUDAModule::IsRHISelectedDevice(CUdevice cuDevice) {
	FString RHIName = GDynamicRHI->GetName();

#if PLATFORM_SUPPORTS_CUDA
    // VULKAN
    if(RHIName == TEXT("Vulkan"))
    {
		uint8 deviceUUID[16];

		// We find the device that the RHI has selected for us so that later we can create a CUDA context on this device.
		FVulkanDynamicRHI *vkDynamicRHI = static_cast<FVulkanDynamicRHI *>(GDynamicRHI);
		FMemory::Memcpy(deviceUUID, vkDynamicRHI->GetDevice()->GetDeviceIdProperties().deviceUUID, 16);	

		// Get the device UUID so we can compare this with what the RHI selected.
		CUuuid cudaDeviceUUID;
		CUresult getUuidErr = DriverApiPtrs.cuDeviceGetUuid(&cudaDeviceUUID, cuDevice);
		if(getUuidErr != CUDA_SUCCESS)
		{
			UE_LOG(LogCUDA, Warning, TEXT("Could not get CUDA device UUID at device %d."), cuDevice);
			return false;
		}

		// Compare the CUDA device UUID with RHI selected UUID.
		return FMemory::Memcmp(&cudaDeviceUUID, deviceUUID, 16) == 0;
    }
	else
#if PLATFORM_WINDOWS
    // DX11
    if(RHIName == TEXT("D3D11"))
    {
		char* DeviceLuid = new char[64];
		unsigned int DeviceNodeMask;
		CUresult getLuidErr = DriverApiPtrs.cuDeviceGetLuid(DeviceLuid, &DeviceNodeMask, cuDevice);
		if(getLuidErr != CUDA_SUCCESS)
		{
			UE_LOG(LogCUDA, Warning, TEXT("Could not get CUDA device LUID at device %d."), cuDevice);
			return false;
		}

		ID3D11Device *NativeD3D11Device = static_cast<ID3D11Device *>(GDynamicRHI->RHIGetNativeDevice());

		TRefCountPtr<IDXGIDevice> DXGIDevice;
		
		HRESULT Result = NativeD3D11Device->QueryInterface(__uuidof(IDXGIDevice), (void**)DXGIDevice.GetInitReference());
		if(FAILED(Result)) {
			UE_LOG(LogCUDA, Error, TEXT("Failed to get DXGIDevice when trying to resolve to CUDA device at %s:%u \n with error %u"), ANSI_TO_TCHAR(__FILE__), __LINE__, Result);
			return false;
		}

		//VERIFYD3D11RESULT_EX(GDynamicRHI->GetDevice()->QueryInterface(IID_IDXGIDevice, (void**)DXGIDevice.GetInitReference()), GDynamicRHI->GetDevice());

		TRefCountPtr<IDXGIAdapter> DXGIAdapter;
		DXGIDevice->GetAdapter((IDXGIAdapter**)DXGIAdapter.GetInitReference());

		DXGI_ADAPTER_DESC AdapterDesc;
		DXGIAdapter->GetDesc(&AdapterDesc);

		LUID AdapterLUID = AdapterDesc.AdapterLuid;
	
		return ((memcmp(&AdapterLUID.LowPart, DeviceLuid,
					sizeof(AdapterLUID.LowPart)) == 0) &&
			(memcmp(&AdapterLUID.HighPart,
					DeviceLuid + sizeof(AdapterLUID.LowPart),
					sizeof(AdapterLUID.HighPart)) == 0));
    }
    // DX12
    else if(RHIName == TEXT("D3D12"))
	{
		char* DeviceLuid = new char[64];
		unsigned int DeviceNodeMask;
		CUresult getLuidErr = DriverApiPtrs.cuDeviceGetLuid(DeviceLuid, &DeviceNodeMask, cuDevice);
		if(getLuidErr != CUDA_SUCCESS)
		{
			UE_LOG(LogCUDA, Warning, TEXT("Could not get CUDA device LUID at device %d."), cuDevice);
			return false;
		}

		//FD3D12DynamicRHI *D3D12DynamicRHI = static_cast<FD3D12DynamicRHI *>(GDynamicRHI);
		//FD3D12Device* D3D12Device = static_cast<FD3D12Device>(D3D12DynamicRHI->GetDevice();
		ID3D12Device* NativeD3D12Device = static_cast<ID3D12Device *>(GDynamicRHI->RHIGetNativeDevice());

		LUID AdapterLUID = NativeD3D12Device->GetAdapterLuid();

		return ((memcmp(&AdapterLUID.LowPart, DeviceLuid,
					sizeof(AdapterLUID.LowPart)) == 0) &&
			(memcmp(&AdapterLUID.HighPart,
					DeviceLuid + sizeof(AdapterLUID.LowPart),
					sizeof(AdapterLUID.HighPart)) == 0));
	}
	else
#endif //PLATFORM_WINDOWS
#endif //PLATFORM_SUPPORTS_CUDA
	{
		UE_LOG(LogCUDA, Fatal, TEXT("Unsupported RHI or Platform for CUDA."));
	}

	return false;
}

void FCUDAModule::InitCuda()
{
	// Initialise to rhiDeviceIndex -1, which is an invalid device index, if it remains -1 we will know no device was found.
	rhiDeviceIndex = -1;

#if PLATFORM_SUPPORTS_CUDA
	// Initialise CUDA API
	{
		UE_LOG(LogCUDA, Display, TEXT("Initialising CUDA API..."));
		
		CUresult err = DriverApiPtrs.cuInit(0);
		if(err == CUDA_SUCCESS) 
		{
			UE_LOG(LogCUDA, Display, TEXT("CUDA API initialised successfully."));
		}
		else 
		{
			UE_LOG(LogCUDA, Fatal, TEXT("CUDA API failed to initialise."));
		}
	}

	// Find out how many graphics devices there are so we find that one that matches the one the RHI selected.
	int device_count = 0;
	CUresult deviceCountErr = DriverApiPtrs.cuDeviceGetCount(&device_count);
	if(deviceCountErr == CUDA_SUCCESS) 
	{
		UE_LOG(LogCUDA, Display, TEXT("Found %d CUDA capable devices."), device_count);
	}
	else 
	{
		UE_LOG(LogCUDA, Error, TEXT("Could not count how many graphics devices there are using CUDA."));
	}

	if (device_count == 0)
	{
		UE_LOG(LogCUDA, Error, TEXT("There are no available device(s) that support CUDA. If that is untrue check CUDA is installed."));
		return;
	}
	
	CUdevice  cuDevice;
	CUcontext cudaContext;
	
	// Find the GPU device that is selected by the RHI.
	for (int current_device = 0; current_device < device_count; ++current_device)
	{
		// Get the current CUDA device.
		CUresult getDeviceErr = DriverApiPtrs.cuDeviceGet(&cuDevice, current_device);
		if(getDeviceErr != CUDA_SUCCESS) 
		{
			UE_LOG(LogCUDA, Warning, TEXT("Could not get CUDA device at device %d."), current_device);
			continue;
		}
		
		// Get device name for logging purposes.
		char deviceNameArr[256];
		CUresult getNameErr = DriverApiPtrs.cuDeviceGetName(deviceNameArr, 256, cuDevice);
		FString deviceName;
		if(getNameErr == CUDA_SUCCESS) 
		{
			deviceName = FString(UTF8_TO_TCHAR(deviceNameArr));
			UE_LOG(LogCUDA, Display, TEXT("Found device %d called \"%s\"."), current_device, *deviceName);
		}
		else 
		{
			UE_LOG(LogCUDA, Warning, TEXT("Could not get name of CUDA device %d."), current_device);
		}
		
		// Compare the CUDA device with current RHI selected device.		
		if (this->IsRHISelectedDevice(cuDevice))
		{
			UE_LOG(LogCUDA, Display, TEXT("Attempting to create CUDA context on GPU Device %d..."), current_device);
			
			CUresult createCtxErr = DriverApiPtrs.cuCtxCreate(&cudaContext, 0, current_device);
			if(createCtxErr == CUDA_SUCCESS)
			{
				UE_LOG(LogCUDA, Display, TEXT("Created CUDA context on device \"%s\"."), *deviceName);
				contextMap.Add(current_device, cudaContext);
				rhiDeviceIndex = current_device;
				break;
			}
			else
			{
				UE_LOG(LogCUDA, Error, TEXT("CUDA module could not create CUDA context on RHI device \"%s\"."), *deviceName);
				return;
			}
		}
		else
		{
			UE_LOG(LogCUDA, Verbose, TEXT("CUDA device %d \"%s\" does not match RHI device."), current_device, *deviceName);
		}
	}

	if (rhiDeviceIndex == -1)
	{
		UE_LOG(LogCUDA, Error, TEXT("CUDA module failed to create a CUDA context on the RHI selected device."));
		return;
	}
	else
#else

	UE_LOG(LogCUDA, Error, TEXT("CUDA attemped to be initialized on unsupported Platform (Currently supported platforms are Win32, Win64, Linux and LinuxAArch64)."));

#endif //PLATFORM_SUPPORTS_CUDA

	{
		OnPostCUDAInit.Broadcast();
	}
}

CUcontext FCUDAModule::GetCudaContext()
{
	if (IsAvailable() == false)
	{
		UE_LOG(LogCUDA, Fatal, TEXT("You have requested a CUDA context when the CUDA Driver API is not loaded."));
	}
	
	if(rhiDeviceIndex == -1)
	{
		UE_LOG(LogCUDA, Fatal, TEXT("You have requested a CUDA context when the RHI selected device does not have a CUDA context, did initialisation fail?"));
	}
	
	return contextMap[rhiDeviceIndex];
}

CUcontext FCUDAModule::GetCudaContextForDevice(int DeviceIndex)
{
	if (IsAvailable() == false)
	{
		UE_LOG(LogCUDA, Fatal, TEXT("You have requested a CUDA context when the CUDA Driver API is not loaded."));
	}
	
	if(!contextMap.Contains(DeviceIndex))
	{
		CUcontext cudaContext;
		CUresult createCtxErr = DriverApiPtrs.cuCtxCreate(&cudaContext, 0, DeviceIndex);
		if(createCtxErr == CUDA_SUCCESS)
		{
			UE_LOG(LogCUDA, Display, TEXT("Created a new CUDA context on device %d on request."), DeviceIndex);
			contextMap.Add(DeviceIndex, cudaContext);
		}
		else
		{
			UE_LOG(LogCUDA, Error, TEXT("Failed to create a CUDA context on device %d on request."), DeviceIndex);
		}
	}
	
	return contextMap[DeviceIndex];
}

IMPLEMENT_MODULE(FCUDAModule, CUDA);
