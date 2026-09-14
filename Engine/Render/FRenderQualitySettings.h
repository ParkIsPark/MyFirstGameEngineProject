#pragma once

#include "FRenderQuality.h"

#include <iosfwd>

class FIniFile;

FRenderQuality ReadRenderQuality(
    const FIniFile& ini, const char* section, const FRenderQuality& defaults);
void WriteRenderQuality(
    std::ostream& output, const char* section, const FRenderQuality& quality);
