#pragma once

namespace litemark {

struct PointF {
    float x = 0.0F;
    float y = 0.0F;
};

struct RectF {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

class Painter {
public:
    virtual ~Painter() = default;
    virtual void SetClip(const RectF&) = 0;
};

}  // namespace litemark
