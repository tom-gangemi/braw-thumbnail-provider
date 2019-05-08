/* THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
 * ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
 * PARTICULAR PURPOSE.
 */

#define ENABLE_CONSOLE_OUT 0

#include <shlwapi.h>
#include <Wincrypt.h>   // For CryptStringToBinary.
#include <thumbcache.h> // For IThumbnailProvider.
#include <wincodec.h>   // Windows Imaging Codecs
#include <msxml6.h>
#include <new>
#include <cstdint>
#include <gdiplus.h>

#include <stdio.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <comdef.h>
#include <string>
#include <tchar.h>
#include <vector>

#pragma comment (lib,"Gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "msxml6.lib")

#include "BlackmagicRawAPIDispatch.h"
#include "BRawThumbnailProvider.h"

static const BlackmagicRawResourceFormat s_resourceFormat = blackmagicRawResourceFormatBGRAU8;

std::wstring getModuleDirectory() {
	DWORD type = REG_SZ;
	LPCWSTR sPath = L"Software\\Classes\\CLSID\\" SZ_CLSID_BRAWTHUMBHANDLER L"\\InProcServer32";

	HKEY hKey;

	LSTATUS nResult = RegOpenKeyEx(
		HKEY_CURRENT_USER, sPath,
		0, KEY_READ | KEY_WOW64_64KEY, &hKey
	);

	TCHAR value[1024] = _T("");
	DWORD dwvalue = 1024;

	LONG nError = RegQueryValueEx(
		hKey, L"",
		NULL, &type, (LPBYTE)& value, &dwvalue
	);

	std::wstring arr_w(value);

	std::size_t found = arr_w.find_last_of(L"/\\");
	return arr_w.substr(0, found);
}

class CameraCodecCallback : public IBlackmagicRawCallback
{
public:
	explicit CameraCodecCallback() = default;
	virtual ~CameraCodecCallback() = default;

	std::unique_ptr<Gdiplus::Bitmap> outBitmap;

	virtual void ReadComplete(IBlackmagicRawJob* readJob, HRESULT result, IBlackmagicRawFrame* frame)
	{
		IBlackmagicRawJob* decodeAndProcessJob = nullptr;

		if (result == S_OK)
			frame->SetResourceFormat(s_resourceFormat);

		if (result == S_OK)
			result = frame->CreateJobDecodeAndProcessFrame(nullptr, nullptr, &decodeAndProcessJob);

		if (result == S_OK)
			result = decodeAndProcessJob->Submit();

		if (result != S_OK)
		{
			if (decodeAndProcessJob)
				decodeAndProcessJob->Release();
		}

		readJob->Release();
	}

	virtual void ProcessComplete(IBlackmagicRawJob* job, HRESULT result, IBlackmagicRawProcessedImage* processedImage)
	{
		unsigned int width = 0;
		unsigned int height = 0;
		void* imageData = nullptr;

		if (result == S_OK)
			result = processedImage->GetWidth(&width);

		if (result == S_OK)
			result = processedImage->GetHeight(&height);

		if (result == S_OK)
			result = processedImage->GetResource(&imageData);

		if (result == S_OK) {
			outBitmap = std::unique_ptr<Gdiplus::Bitmap>(
				new Gdiplus::Bitmap(width, height, 4 * width, PixelFormat32bppRGB, (BYTE*)imageData)
			);
		}

		job->Release();
	}

	virtual void DecodeComplete(IBlackmagicRawJob*, HRESULT) {}
	virtual void TrimProgress(IBlackmagicRawJob*, float) {}
	virtual void TrimComplete(IBlackmagicRawJob*, HRESULT) {}
	virtual void SidecarMetadataParseWarning(IBlackmagicRawClip*, BSTR, uint32_t, BSTR) {}
	virtual void SidecarMetadataParseError(IBlackmagicRawClip*, BSTR, uint32_t, BSTR) {}

	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, LPVOID*)
	{
		return E_NOTIMPL;
	}

	virtual ULONG STDMETHODCALLTYPE AddRef(void)
	{
		return 0;
	}

	virtual ULONG STDMETHODCALLTYPE Release(void)
	{
		return 0;
	}

};


class CBrawThumbProvider : public IInitializeWithFile,
                             public IThumbnailProvider
{
public:
    CBrawThumbProvider() : _cRef(1), filePath(NULL), pCout(NULL)
    {
#if ENABLE_CONSOLE_OUT == 1
		AllocConsole();
		freopen_s(&pCout, "conout$", "w", stdout);
		freopen_s(&pCout, "conout$", "w", stderr);
#endif
    }

    virtual ~CBrawThumbProvider()
    {
		if (pCout)
		{
			fclose(pCout);
		}

    }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv)
    {
        static const QITAB qit[] =
        {
            QITABENT(CBrawThumbProvider, IInitializeWithFile),
            QITABENT(CBrawThumbProvider, IThumbnailProvider),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }

    IFACEMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&_cRef);
    }

    IFACEMETHODIMP_(ULONG) Release()
    {
        ULONG cRef = InterlockedDecrement(&_cRef);
        if (!cRef)
        {
            delete this;
        }
        return cRef;
    }

    // IInitializeWithFile
    IFACEMETHODIMP Initialize(LPCWSTR pszFilePath, DWORD grfMode);

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha);

private:
    long _cRef;
	LPCWSTR filePath;
	FILE* pCout;
};

HRESULT CBrawThumbProvider_CreateInstance(REFIID riid, void **ppv)
{
    CBrawThumbProvider *pNew = new (std::nothrow) CBrawThumbProvider();
    HRESULT hr = pNew ? S_OK : E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
    {
        hr = pNew->QueryInterface(riid, ppv);
        pNew->Release();
    }
    return hr;
}

// IInitializeWithFile
IFACEMETHODIMP CBrawThumbProvider::Initialize(LPCWSTR pszFilePath, DWORD)
{
	filePath = pszFilePath;
	fprintf(stdout, "pszFilePath: %S\n", pszFilePath);
    return 1;
}

// IThumbnailProvider
IFACEMETHODIMP CBrawThumbProvider::GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha)
{
	*pdwAlpha = WTSAT_RGB;

	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

	fprintf(stdout, "GetThumbnail FilePath: %S\n", filePath);
	BSTR clipName = SysAllocString(filePath);

	IBlackmagicRawFactory* factory = nullptr;
	IBlackmagicRaw* codec = nullptr;
	IBlackmagicRawClip* clip = nullptr;
	IBlackmagicRawJob* readJob = nullptr;
	HRESULT result = S_OK;

	CameraCodecCallback callback;

	long readFrame = 0;

	std::wstring moduleDirectory = getModuleDirectory();

	do
	{
		BSTR libraryPath = SysAllocStringLen(moduleDirectory.data(), (UINT)moduleDirectory.size());
		factory = CreateBlackmagicRawFactoryInstanceFromPath(libraryPath);
		SysFreeString(libraryPath);

		if (factory == nullptr)
		{
			fprintf(stderr, "Failed to create IBlackmagicRawFactory!\n");
			break;
		}

		result = factory->CreateCodec(&codec);
		if (result != S_OK)
		{
			fprintf(stderr, "Failed to create IBlackmagicRaw!\n");
			break;
		}

		result = codec->OpenClip(clipName, &clip);
		if (result != S_OK)
		{
			fprintf(stderr, "Failed to open IBlackmagicRawClip!\n");
			break;
		}

		result = codec->SetCallback(&callback);
		if (result != S_OK)
		{
			fprintf(stderr, "Failed to set IBlackmagicRawCallback!\n");
			break;
		}

		result = clip->CreateJobReadFrame(readFrame, &readJob);
		if (result != S_OK)
		{
			fprintf(stderr, "Failed to create IBlackmagicRawJob!\n");
			break;
		}

		result = readJob->Submit();
		if (result != S_OK)
		{
			// submit has failed, the ReadComplete callback won't be called, release the job here instead
			readJob->Release();
			fprintf(stderr, "Failed to submit IBlackmagicRawJob!\n");
			break;
		}

		codec->FlushJobs();

	} while (0);

	if (codec != nullptr)
		codec->Release();

	if (factory != nullptr)
		factory->Release();

	SysFreeString(clipName);
	
	if (callback.outBitmap) {
		UINT newSize = cx;
		UINT width = callback.outBitmap->GetWidth();
		UINT height = callback.outBitmap->GetHeight();

		float scalingFactor = 1;
		if (width > height)
			scalingFactor = (float)newSize / (float)width;
		else
			scalingFactor = (float)newSize / (float)height;

		float newWidth = scalingFactor * (float)width;
		float newHeight = scalingFactor * (float)height;

		Gdiplus::Bitmap resizedBitmap((int)newWidth, (int)newHeight);
		Gdiplus::Graphics g(&resizedBitmap);
		g.ScaleTransform(scalingFactor, scalingFactor);
		g.DrawImage(callback.outBitmap.get(), 0, 0);

		result = (&resizedBitmap)->GetHBITMAP(Gdiplus::Color::White, phbmp);		
	}
	else {
		result = E_FAIL;
	}

	if (clip != nullptr)
		clip->Release();

	return result;
}
