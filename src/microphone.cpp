#include "microphone.h"

#include <mmdeviceapi.h>
#include <objbase.h>

#include <cmath>

namespace
{
const CLSID kMMDeviceEnumeratorClass = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const IID kMMDeviceEnumeratorInterface = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
}

Microphone::Microphone()
    : handle_(NULL)
    , level_(0.0f)
    , muted_(false)
    , running_(false)
    , lastData_(0)
{
    ZeroMemory(headers_, sizeof(headers_));
    ZeroMemory(buffers_, sizeof(buffers_));
}

Microphone::~Microphone()
{
    Stop();
}

bool Microphone::Start()
{
    if (handle_ != NULL)
    {
        return true;
    }

    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = 44100;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    MMRESULT result = waveInOpen(
        &handle_,
        WAVE_MAPPER,
        &format,
        reinterpret_cast<DWORD_PTR>(&Microphone::WaveProc),
        reinterpret_cast<DWORD_PTR>(this),
        CALLBACK_FUNCTION);

    if (result != MMSYSERR_NOERROR)
    {
        handle_ = NULL;
        return false;
    }

    running_.store(true);

    for (int i = 0; i < BufferCount; ++i)
    {
        buffers_[i] = new short[SamplesPerBuffer];
        ZeroMemory(&headers_[i], sizeof(WAVEHDR));
        headers_[i].lpData = reinterpret_cast<LPSTR>(buffers_[i]);
        headers_[i].dwBufferLength = SamplesPerBuffer * sizeof(short);

        waveInPrepareHeader(handle_, &headers_[i], sizeof(WAVEHDR));
        waveInAddBuffer(handle_, &headers_[i], sizeof(WAVEHDR));
    }

    waveInStart(handle_);
    lastData_.store(GetTickCount64());
    return true;
}

void Microphone::Stop()
{
    if (handle_ == NULL)
    {
        return;
    }

    running_.store(false);
    waveInStop(handle_);
    waveInReset(handle_);

    for (int i = 0; i < BufferCount; ++i)
    {
        if (buffers_[i] != NULL)
        {
            waveInUnprepareHeader(handle_, &headers_[i], sizeof(WAVEHDR));
            delete[] buffers_[i];
            buffers_[i] = NULL;
        }
    }

    waveInClose(handle_);
    handle_ = NULL;
    level_.store(0.0f);
    lastData_.store(0);
}

void Microphone::SetMuted(bool muted)
{
    muted_.store(muted);
}

float Microphone::Level() const
{
    return muted_.load() ? 0.0f : level_.load();
}

bool Microphone::IsRunning() const
{
    return running_.load();
}

bool Microphone::Stalled(ULONGLONG now, ULONGLONG timeout) const
{
    const ULONGLONG last = lastData_.load();
    return last != 0 && now > last && now - last > timeout;
}

std::wstring Microphone::DefaultDeviceId()
{
    std::wstring id;
    IMMDeviceEnumerator* enumerator = NULL;
    if (SUCCEEDED(CoCreateInstance(
            kMMDeviceEnumeratorClass,
            NULL,
            CLSCTX_ALL,
            kMMDeviceEnumeratorInterface,
            reinterpret_cast<void**>(&enumerator))) &&
        enumerator != NULL)
    {
        auto appendRole = [&](ERole role) {
            IMMDevice* device = NULL;
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, role, &device)) && device != NULL)
            {
                LPWSTR value = NULL;
                if (SUCCEEDED(device->GetId(&value)) && value != NULL)
                {
                    id.append(value);
                    CoTaskMemFree(value);
                }
                device->Release();
            }
            id.push_back(L'|');
        };

        appendRole(eConsole);
        appendRole(eCommunications);
        enumerator->Release();
    }

    return id;
}

void CALLBACK Microphone::WaveProc(HWAVEIN handle, UINT message, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2)
{
    (void)handle;
    (void)param2;

    if (message != WIM_DATA)
    {
        return;
    }

    Microphone* microphone = reinterpret_cast<Microphone*>(instance);
    if (microphone != NULL)
    {
        microphone->OnData(reinterpret_cast<WAVEHDR*>(param1));
    }
}

void Microphone::OnData(WAVEHDR* header)
{
    if (!running_.load() || header == NULL)
    {
        return;
    }

    lastData_.store(GetTickCount64());

    const DWORD bytes = header->dwBytesRecorded;
    if (bytes >= sizeof(short))
    {
        const short* samples = reinterpret_cast<const short*>(header->lpData);
        const size_t count = bytes / sizeof(short);
        double sum = 0.0;

        for (size_t i = 0; i < count; ++i)
        {
            const double value = static_cast<double>(samples[i]) / 32768.0;
            sum += value * value;
        }

        level_.store(static_cast<float>(std::sqrt(sum / static_cast<double>(count))));
    }

    if (running_.load())
    {
        waveInAddBuffer(handle_, header, sizeof(WAVEHDR));
    }
}
