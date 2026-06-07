#include "VSlider.hpp"
#include <algorithm>

START_NAMESPACE_DGL

VSlider::VSlider(Widget* parent)
    : NanoSubWidget(parent) {}

void VSlider::setValue(float normalized, bool sendCallback) {
    normalized = std::min(1.0f, std::max(0.0f, normalized));
    if (d_isEqual(normalized, fValue))
        return;
    fValue = normalized;
    if (sendCallback && fCallback != nullptr)
        fCallback->sliderValueChanged(this, fValue);
    repaint();
}

float VSlider::valueFromY(double y) const {
    const double h = static_cast<double>(getHeight());
    if (h <= 0.0) return fValue;
    double v = 1.0 - (y / h);   // top of track = 1.0, bottom = 0.0
    return static_cast<float>(std::min(1.0, std::max(0.0, v)));
}

void VSlider::onNanoDisplay() {
    const float w = getWidth();
    const float h = getHeight();
    const float trackW = 6.0f;
    const float trackX = (w - trackW) * 0.5f;

    // track
    beginPath();
    rect(trackX, 0.0f, trackW, h);
    fillColor(Color(0.20f, 0.20f, 0.20f));
    fill();

    // filled portion below the thumb
    const float thumbY = (1.0f - fValue) * h;
    beginPath();
    rect(trackX, thumbY, trackW, h - thumbY);
    fillColor(Color(0.55f, 0.35f, 0.10f));
    fill();

    // thumb (darkorange, matching the JUCE original)
    const float thumbH = 14.0f;
    beginPath();
    rect(0.0f, std::max(0.0f, thumbY - thumbH * 0.5f), w, thumbH);
    fillColor(Color(1.0f, 0.55f, 0.0f));
    fill();
}

bool VSlider::onMouse(const MouseEvent& ev) {
    // ev.pos is in widget-relative (local) coordinates.
    if (ev.button != 1)
        return false;
    if (ev.press && contains(ev.pos)) {
        fDragging = true;
        if (fCallback != nullptr) fCallback->sliderDragStarted(this);
        setValue(valueFromY(ev.pos.getY()), true);
        return true;
    }
    if (!ev.press && fDragging) {
        fDragging = false;
        if (fCallback != nullptr) fCallback->sliderDragFinished(this);
        return true;
    }
    return false;
}

bool VSlider::onMotion(const MotionEvent& ev) {
    // ev.pos is in widget-relative (local) coordinates.
    if (!fDragging)
        return false;
    setValue(valueFromY(ev.pos.getY()), true);
    return true;
}

END_NAMESPACE_DGL
