#include "runtime_component.h"

void runtime_component_step(runtime_component_t* component)
{
    if (component == 0 || component->step == 0) {
        return;
    }

    component->step(component);
}
