#pragma once

#include <juce_core/juce_core.h>
#include <opencv2/videoio.hpp>

#if JUCE_WINDOWS
#include <windows.h>
#include <dshow.h>
#pragma comment(lib, "strmiids.lib")
#endif

/**
 * A non-blocking, thread-safe singleton to get a list of available webcams.
 * Uses native Windows DirectShow for friendly names, falling back to OpenCV for other platforms.
 */
class CameraEnumerator : private juce::Thread
{
public:
    static CameraEnumerator& getInstance()
    {
        static CameraEnumerator instance;
        return instance;
    }

    juce::StringArray getAvailableCameraNames()
    {
        const juce::ScopedLock lock(listLock);
        return availableCameraNames;
    }

    void rescan()
    {
        {
            const juce::ScopedLock lock(listLock);
            if (isThreadRunning())
            {
                return; // Already scanning
            }
            availableCameraNames.clear();
            availableCameraNames.add("Scanning for cameras...");
        }
        startThread();
    }

private:
    CameraEnumerator() : juce::Thread("Camera Enumerator")
    {
        startThread();
    }
    ~CameraEnumerator() { stopThread(4000); }

    void run() override
    {
        juce::Logger::writeToLog("[CameraEnumerator] Starting background camera scan...");
        {
            const juce::ScopedLock lock(listLock);
            availableCameraNames.add("Scanning for cameras...");
        }

        juce::StringArray foundCameras;

#if JUCE_WINDOWS
        // --- Native Windows DirectShow Enumeration ---
        CoInitialize(NULL);
        ICreateDevEnum* pSysDevEnum = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&pSysDevEnum);

        if (SUCCEEDED(hr))
        {
            IEnumMoniker* pEnum = nullptr;
            hr = pSysDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);

            if (hr == S_OK)
            {
                IMoniker* pMoniker = nullptr;
                while (pEnum->Next(1, &pMoniker, NULL) == S_OK)
                {
                    IPropertyBag* pPropBag;
                    hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pPropBag);
                    if (SUCCEEDED(hr))
                    {
                        VARIANT varName;
                        VariantInit(&varName);
                        hr = pPropBag->Read(L"FriendlyName", &varName, 0);
                        if (SUCCEEDED(hr))
                        {
                            juce::String cameraName(varName.bstrVal);
                            foundCameras.add(cameraName);
                            VariantClear(&varName);
                        }
                        pPropBag->Release();
                    }
                    pMoniker->Release();
                }
                pEnum->Release();
            }
            pSysDevEnum->Release();
        }
        CoUninitialize();
#else
        // --- Fallback for non-Windows platforms ---
        for (int i = 0; i < 10; ++i)
        {
            cv::VideoCapture cap(i);
            if (cap.isOpened())
            {
                juce::String name = "Camera " + juce::String(i);
                int width = (int)cap.get(cv::CAP_PROP_FRAME_WIDTH);
                int height = (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT);
                if (width > 0 && height > 0)
                {
                    name += " (" + juce::String(width) + "x" + juce::String(height) + ")";
                }
                foundCameras.add(name);
                cap.release();
            }
        }
#endif

        // Atomically swap the completed list into place.
        {
            const juce::ScopedLock lock(listLock);
            if (foundCameras.isEmpty())
            {
                availableCameraNames.clear();
                availableCameraNames.add("No cameras found");
            }
            else
            {
                availableCameraNames = foundCameras;
            }
        }
        
        juce::Logger::writeToLog("[CameraEnumerator] Scan complete. Found " + juce::String(foundCameras.size()) + " cameras.");
    }

    juce::CriticalSection listLock;
    juce::StringArray availableCameraNames;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CameraEnumerator)
};
