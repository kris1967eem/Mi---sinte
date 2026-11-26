#include "DefaultWavetable.h"
#include "WavetableLoader.h"

namespace wavetable
{

std::shared_ptr<WavetableData> createDefaultWavetable()
{
    WavetableLoader loader;
    return loader.generateDefault();
}

} // namespace wavetable
