#pragma once

#include "WavetableData.h"

namespace wavetable
{

// Genera la wavetable por defecto al arrancar el plugin.
// Se puede llamar desde el constructor del plugin (una vez).
std::shared_ptr<WavetableData> createDefaultWavetable();

} // namespace wavetable