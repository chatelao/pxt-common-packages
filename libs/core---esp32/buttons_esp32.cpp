#include "pxt.h"

namespace pxt {

struct ComponentEntry {
    int id;
    CodalComponent *comp;
    ComponentEntry *next;
};

static ComponentEntry *componentsHead = NULL;

CodalComponent *lookupComponent(int id) {
    for (ComponentEntry *e = componentsHead; e; e = e->next) {
        if (e->id == id)
            return e->comp;
    }
    return NULL;
}

static void registerComponent(int id, CodalComponent *comp) {
    ComponentEntry *e = new ComponentEntry();
    e->id = id;
    e->comp = comp;
    e->next = componentsHead;
    componentsHead = e;
}

Button::Button(ZPin &p, int _id, int eventConfig, ButtonPolarity polarity, PullMode pull)
    : AbstractButton(_id), pin(&p), activeLow(polarity == ACTIVE_LOW), lastState(false) {
    pin->setPull(pull);
    registerComponent(_id, this);
}

bool Button::isPressed() {
    int v = pin->getDigitalValue();
    return activeLow ? (v == 0) : (v != 0);
}

bool Button::wasPressed() {
    bool cur = isPressed();
    bool ret = cur && !lastState;
    lastState = cur;
    return ret;
}

MultiButton::MultiButton(int _idA, int _idB, int _id)
    : AbstractButton(_id), idA(_idA), idB(_idB), lastState(false) {
    registerComponent(_id, this);
}

bool MultiButton::isPressed() {
    auto a = (AbstractButton *)lookupComponent(idA);
    auto b = (AbstractButton *)lookupComponent(idB);
    return a && b && a->isPressed() && b->isPressed();
}

bool MultiButton::wasPressed() {
    bool cur = isPressed();
    bool ret = cur && !lastState;
    lastState = cur;
    return ret;
}

} // namespace pxt
