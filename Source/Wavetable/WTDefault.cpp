#include "WTDefault.h"
#include "WTLoader.h"

namespace wavetable
{

std::shared_ptr<WavetableData> createDefaultWavetable()
{
    WavetableLoader loader;
    return loader.generateDefault();
}

} // namespace wavetable
