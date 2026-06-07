#include "DistrhoUI.hpp"
#include "VSlider.hpp"
#include "ParamInfo.h"
#include <cmath>

START_NAMESPACE_DISTRHO

// With DISTRHO_UI_USE_NANOVG 1, UI is-a NanoTopLevelWidget, so all NanoVG
// drawing methods (beginPath/rect/fillColor/fontSize/textAlign/text/...) and
// loadSharedResources() are available directly on the UI, and the draw entry
// point is onNanoDisplay() (inherited from NanoVG via NanoTopLevelWidget).
class SimpleLFOFilterUI : public UI, public VSlider::Callback {
public:
    SimpleLFOFilterUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT) {
        // Registers the built-in DejaVu Sans font (NANOVG_DEJAVU_SANS_TTF).
        // Being the first/only registered font, it becomes the default face,
        // so text() works without an explicit fontFace() (matches DPF examples).
        loadSharedResources();

        const uint sliderW = 60;
        const uint sliderH = 200;
        const uint spacing  = 40;
        const uint topPad   = 90;   // room for title + per-slider label
        const uint totalW   = kParamCount * sliderW + (kParamCount - 1) * spacing;
        const uint startX   = (DISTRHO_UI_DEFAULT_WIDTH - totalW) / 2;

        for (uint i = 0; i < kParamCount; ++i) {
            fSliders[i] = new VSlider(this);
            fSliders[i]->setId(i);
            fSliders[i]->setCallback(this);
            fSliders[i]->setAbsolutePos(startX + i * (sliderW + spacing), topPad);
            fSliders[i]->setSize(sliderW, sliderH);
            fSliders[i]->setValue(normalize(i, kParams[i].def), false);
        }
    }

    ~SimpleLFOFilterUI() override {
        // DGL parents do NOT delete their sub-widgets (Widget::PrivateData only
        // clears the child-pointer list), so we own these and must free them.
        // No double-free risk.
        for (uint i = 0; i < kParamCount; ++i)
            delete fSliders[i];
    }

protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index < kParamCount)
            fSliders[index]->setValue(normalize(index, value), false);
    }

    void onNanoDisplay() override {
        const float w = getWidth();

        beginPath();
        rect(0.0f, 0.0f, w, getHeight());
        fillColor(Color(0.16f, 0.16f, 0.18f));
        fill();

        fontSize(15.0f);
        fillColor(Color(1.0f, 1.0f, 1.0f));
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(w * 0.5f, 12.0f, "Peak Filter with LFO Modulation and Gain Control", nullptr);

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        for (uint i = 0; i < kParamCount; ++i) {
            const float cx = fSliders[i]->getAbsoluteX() + fSliders[i]->getWidth() * 0.5f;
            text(cx, fSliders[i]->getAbsoluteY() - 6.0f, kParams[i].name, nullptr);
        }
    }

    void sliderDragStarted(VSlider* s) override { editParameter(s->getId(), true); }
    void sliderDragFinished(VSlider* s) override { editParameter(s->getId(), false); }
    void sliderValueChanged(VSlider* s, float normalized) override {
        setParameterValue(s->getId(), denormalize(s->getId(), normalized));
    }

private:
    VSlider* fSliders[kParamCount];

    static float normalize(uint i, float value) {
        const ParamDescriptor& d = kParams[i];
        if (d.logarithmic && d.min > 0.0f && value > 0.0f)
            return (float) (std::log(value / d.min) / std::log((double) d.max / d.min));
        return (value - d.min) / (d.max - d.min);
    }
    static float denormalize(uint i, float n) {
        const ParamDescriptor& d = kParams[i];
        if (d.logarithmic && d.min > 0.0f)
            return (float) (d.min * std::pow((double) d.max / d.min, (double) n));
        return d.min + n * (d.max - d.min);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleLFOFilterUI)
};

UI* createUI() { return new SimpleLFOFilterUI(); }

END_NAMESPACE_DISTRHO
