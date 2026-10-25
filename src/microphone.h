#pragma once

#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <mutex>
#include <string>

class Microphone
{
public:
    Microphone();
    ~Microphone();

    bool Start(UINT_PTR deviceId = WAVE_MAPPER);
    void Stop();
    void SetMuted(bool muted);
    float Level() const;
    bool IsRunning() const;
    bool Stalled(ULONGLONG now, ULONGLONG timeout) const;
    static std::wstring DefaultDeviceId();
    static UINT_PTR DefaultWaveInDeviceId();

private:
    static void CALLBACK WaveProc(HWAVEIN handle, UINT message, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2);
    void OnData(WAVEHDR* header);

    static const int BufferCount = 4;
    static const int SamplesPerBuffer = 2048;

    HWAVEIN handle_;
    WAVEHDR headers_[BufferCount];
    short* buffers_[BufferCount];
    std::atomic<float> level_;
    std::atomic<bool> muted_;
    std::atomic<bool> running_;
    std::atomic<ULONGLONG> lastData_;
    std::mutex mutex_;
};
