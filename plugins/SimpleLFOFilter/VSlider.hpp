#pragma once
#include "NanoVG.hpp"

START_NAMESPACE_DGL

// A vertical slider drawn with NanoVG. Value is normalized [0,1]; the owner maps
// it to a parameter range. Reports drag start/change/end through a callback so
// the owner can call editParameter()/setParameterValue().
class VSlider : public NanoSubWidget {
public:
    class Callback {
    public:
        virtual ~Callback() {}
        virtual void sliderDragStarted(VSlider* slider) = 0;
        virtual void sliderValueChanged(VSlider* slider, float normalized) = 0;
        virtual void sliderDragFinished(VSlider* slider) = 0;
    };

    explicit VSlider(Widget* parent);   // pass the UI (a TopLevelWidget is-a Widget)

    void  setId(uint id) noexcept { fId = id; }
    uint  getId() const noexcept  { return fId; }

    void  setValue(float normalized, bool sendCallback);
    float getValue() const noexcept { return fValue; }

    void  setCallback(Callback* cb) noexcept { fCallback = cb; }

protected:
    void onNanoDisplay() override;
    bool onMouse(const MouseEvent& ev) override;
    bool onMotion(const MotionEvent& ev) override;

private:
    Callback* fCallback = nullptr;
    uint  fId    = 0;
    float fValue = 0.0f;   // normalized [0,1]
    bool  fDragging = false;

    float valueFromY(double y) const;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VSlider)
};

END_NAMESPACE_DGL
