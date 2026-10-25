#include "avatar.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
const float kGate = 0.018f;
const float kFullScale = 0.08f;
const float kHoldMilliseconds = 140.0f;
const float kLift = 10.0f;
const float kHopPixels = 30.0f;
const float kHopMilliseconds = 300.0f;
const float kStiffness = 260.0f;
const float kDamping = 8.0f;
const float kKick = 140.0f;
const float kStretch = 0.0002f;
const float kStretchLimit = 0.04f;
const float kIdlePixels = 2.2f;
const float kIdleMilliseconds = 3900.0f;
const float kDesignWidth = 600.0f;
const float kDesignHeight = 780.0f;
const float kArtTop = 10.0f;
const float kStageBottom = 736.0f;
const float kArtScale = 0.95f;
const float kPi = 3.14159265358979323846f;

float Clamp(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

std::unique_ptr<Gdiplus::Image> LoadImageFile(const std::wstring& path)
{
    std::unique_ptr<Gdiplus::Image> image(Gdiplus::Image::FromFile(path.c_str()));
    if (!image || image->GetLastStatus() != Gdiplus::Ok)
    {
        return std::unique_ptr<Gdiplus::Image>();
    }
    return image;
}
}

Avatar::Avatar(const std::wstring& baseDirectory, int width, int height)
    : width_(width)
    , height_(height)
    , rms_(0.0f)
    , voice_(0.0f)
    , bounceY_(0.0f)
    , bounceV_(0.0f)
    , hop_(0.0f)
    , stretch_(0.0f)
    , talkHoldUntil_(0.0)
    , nextBlinkAt_(2500.0)
    , blinkUntil_(0.0)
    , talking_(false)
    , blinking_(false)
{
    LoadLayers(baseDirectory);
}

Avatar::~Avatar()
{
    layers_.clear();
}

void Avatar::LoadLayers(const std::wstring& baseDirectory)
{
    struct Definition
    {
        const wchar_t* file;
        float x;
        float y;
        float width;
        float height;
        int z;
        LayerMode mode;
        bool strip;
    };

    static const Definition definitions[] = {
        { L"boykisser-body.png", 149.0f, 487.0f, 323.0f, 326.0f, 0, Always, false },
        { L"boykisser-head.png", 5.0f, 10.0f, 589.0f, 513.0f, 1, Always, false },
        { L"boykisser-nose.png", 282.0f, 355.0f, 35.0f, 12.0f, 2, Always, false },
        { L"boykisser-mouth-closed.png", 235.0f, 385.0f, 129.0f, 28.0f, 2, Silent, false },
        { L"boykisser-mouth-sprite.png", 235.0f, 375.0f, 129.0f, 112.0f, 2, Talking, true },
        { L"boykisser-eyes-closed.png", 114.0f, 305.0f, 371.0f, 46.0f, 2, Blink, false },
        { L"boykisser-eyes-open.png", 114.0f, 270.0f, 371.0f, 123.0f, 2, Open, false },
        { L"boykisser-blush.png", 86.0f, 395.0f, 390.0f, 35.0f, 2, Talking, false },
        { L"boykisser-hair.png", 180.0f, 87.0f, 235.0f, 132.0f, 3, Always, false },
    };

    for (const Definition& definition : definitions)
    {
        Layer layer;
        layer.path = baseDirectory + L"\\assets\\" + definition.file;
        layer.x = definition.x;
        layer.y = definition.y;
        layer.width = definition.width;
        layer.height = definition.height;
        layer.z = definition.z;
        layer.mode = definition.mode;
        layer.strip = definition.strip;
        layer.image = LoadImageFile(layer.path);
        layers_.push_back(std::move(layer));
    }

    std::stable_sort(layers_.begin(), layers_.end(), [](const Layer& left, const Layer& right) {
        return left.z < right.z;
    });
}

void Avatar::Resize(int width, int height)
{
    width_ = width;
    height_ = height;
}

bool Avatar::IsTalking() const
{
    return talking_;
}

void Avatar::Update(float microphoneLevel, bool muted, double nowMilliseconds, double deltaSeconds)
{
    const float dt = static_cast<float>(deltaSeconds);

    float level = microphoneLevel;
    if (muted)
    {
        level = 0.0f;
    }

    rms_ += (level - rms_) * std::min(1.0f, dt * 30.0f);

    if (muted)
    {
        talking_ = false;
        voice_ = 0.0f;
    }
    else
    {
        if (rms_ > kGate)
        {
            if (!talking_)
            {
                bounceV_ -= kKick;
            }
            talking_ = true;
            talkHoldUntil_ = nowMilliseconds + kHoldMilliseconds;
        }
        else if (talking_ && nowMilliseconds > talkHoldUntil_)
        {
            talking_ = false;
        }

        const float above = std::max(0.0f, rms_ - kGate);
        const float normalized = std::min(1.0f, above / std::max(0.01f, kFullScale - kGate));
        const float target = talking_ ? std::sqrt(normalized) : 0.0f;
        const float tau = target > voice_ ? 0.04f : 0.16f;
        voice_ += (target - voice_) * (1.0f - std::exp(-dt / tau));
    }

    const float targetY = -voice_ * kLift;
    const float acceleration = -kStiffness * (bounceY_ - targetY) - kDamping * bounceV_;
    bounceV_ += acceleration * dt;
    bounceY_ += bounceV_ * dt;

    const float hopAmplitude = voice_ * kHopPixels;
    const float phase = static_cast<float>(nowMilliseconds / kHopMilliseconds) * kPi * 2.0f;
    hop_ = -hopAmplitude * (0.5f - 0.5f * std::cos(phase));
    const float hopVelocity = -hopAmplitude * 0.5f * std::sin(phase) * (kPi * 2.0f / kHopMilliseconds) * 1000.0f;
    stretch_ = Clamp(-(bounceV_ + hopVelocity) * kStretch, -kStretchLimit, kStretchLimit);

    if (!blinking_ && nowMilliseconds >= nextBlinkAt_)
    {
        blinking_ = true;
        blinkUntil_ = nowMilliseconds + 110.0;
    }

    if (blinking_ && nowMilliseconds >= blinkUntil_)
    {
        blinking_ = false;
        nextBlinkAt_ = nowMilliseconds + 2000.0 + (std::rand() % 4500);
    }
}

bool Avatar::IsVisible(const Layer& layer) const
{
    if (layer.mode == Always)
    {
        return true;
    }
    if (layer.mode == Silent)
    {
        return !talking_;
    }
    if (layer.mode == Talking)
    {
        return talking_;
    }
    if (layer.mode == Blink)
    {
        return blinking_;
    }
    return !blinking_;
}

void Avatar::Draw(Gdiplus::Graphics& graphics, double nowMilliseconds)
{
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    const float scale = std::min(width_ / kDesignWidth, height_ / kDesignHeight) * kArtScale;
    const float offsetX = (width_ - kDesignWidth * scale) * 0.5f;
    const float offsetY = height_ - kStageBottom * scale;
    const float viewTop = -offsetY / scale;
    const float stretchX = 1.0f - stretch_ * 0.45f;
    const float stretchY = 1.0f + stretch_;

    const float idle = std::sin(static_cast<float>(nowMilliseconds / kIdleMilliseconds) * kPi * 2.0f)
        * kIdlePixels * (1.0f - std::min(1.0f, voice_ * 4.0f));

    float dy = bounceY_ + hop_ + idle;
    const float headTop = kArtTop + dy - (kStageBottom - kArtTop) * stretch_;
    const float minTop = viewTop + 2.0f;

    if (headTop < minTop)
    {
        dy += minTop - headTop;
    }

    for (const Layer& layer : layers_)
    {
        if (!layer.image || !IsVisible(layer))
        {
            continue;
        }

        const float x = kDesignWidth * 0.5f + (layer.x - kDesignWidth * 0.5f) * stretchX;
        const float y = kStageBottom + (layer.y - kStageBottom) * stretchY + dy;
        const float width = layer.width * stretchX;
        const float height = layer.height * stretchY;

        const Gdiplus::RectF destination(
            offsetX + x * scale,
            offsetY + y * scale,
            width * scale,
            height * scale);

        if (layer.strip)
        {
            const int frame = static_cast<int>(nowMilliseconds / 70.0) % 8;
            graphics.DrawImage(
                layer.image.get(),
                destination,
                static_cast<Gdiplus::REAL>(frame * 129),
                0.0f,
                129.0f,
                112.0f,
                Gdiplus::UnitPixel);
        }
        else
        {
            graphics.DrawImage(
                layer.image.get(),
                destination,
                0.0f,
                0.0f,
                static_cast<Gdiplus::REAL>(layer.image->GetWidth()),
                static_cast<Gdiplus::REAL>(layer.image->GetHeight()),
                Gdiplus::UnitPixel);
        }
    }
}
