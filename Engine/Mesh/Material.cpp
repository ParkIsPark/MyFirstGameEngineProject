#include "Material.h"
#include "FArchive.h"

// Numeric Blinn-Phong fields only. Texture pixels are never serialized here
// (a path reference is added in P3); CPU/GL texture data stays runtime-only.
void Material::Serialize(FArchive& ar)
{
    ar.Color("kd",        kd);
    ar.Color("ks",        ks);
    ar.Color("ka",        ka);
    ar.Field("shininess", shininess);
    ar.Color("km",        km);
    ar.Color("emissive",  emissive);
}
