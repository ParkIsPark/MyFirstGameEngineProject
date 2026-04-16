#include "UScene.h"

UScene::UScene()
    : width(1024), height(1024)
{
    outputImage.reserve(width * height * 3);
}

UScene::~UScene()
{
    for (AActor* actor : Actors)
    {
        delete actor->surface;
        delete actor;
    }
}
