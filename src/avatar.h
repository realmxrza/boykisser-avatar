#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <memory>
#include <string>
#include <vector>

class Avatar
{
public:
    Avatar(const std::wstring& baseDirectory, int width, int height);
    ~Avatar();

    void Resize(int width, int height);
    void Update(float microphoneLevel, bool muted, double nowMilliseconds, double deltaSeconds);
    void Draw(Gdiplus::Graphics& graphics, double nowMilliseconds);
    bool IsTalking() const;

private:
    enum LayerMode
    {
        Always,
        Silent,
        Talking,
        Blink,
        Open
    };

    struct Layer
    {
        std::wstring path;
        float x;
        float y;
        float width;
        float height;
        int z;
        LayerMode mode;
        bool strip;
        std::unique_ptr<Gdiplus::Image> image;
    };

    void LoadLayers(const std::wstring& baseDirectory);
    bool IsVisible(const Layer& layer) const;

    std::vector<Layer> layers_;
    int width_;
    int height_;
    float rms_;
    float voice_;
    float bounceY_;
    float bounceV_;
    float hop_;
    float stretch_;
    double talkHoldUntil_;
    double nextBlinkAt_;
    double blinkUntil_;
    bool talking_;
    bool blinking_;
};
