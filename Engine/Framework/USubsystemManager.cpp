#include "USubsystemManager.h"
#include "USubsystem.h"

USubsystemManager::~USubsystemManager()
{
    ShutdownAll();
    // delete in reverse registration order (mirror of construction)
    for (auto it = subs_.rbegin(); it != subs_.rend(); ++it)
        delete *it;
    subs_.clear();
}

void USubsystemManager::Register(USubsystem* sub)
{
    if (sub) subs_.push_back(sub);
}

void USubsystemManager::InitAll()
{
    for (USubsystem* s : subs_) s->Init();
}

void USubsystemManager::TickAll(float dt)
{
    for (USubsystem* s : subs_) s->Tick(dt);
}

void USubsystemManager::ShutdownAll()
{
    if (shutdownDone_) return;
    shutdownDone_ = true;
    for (auto it = subs_.rbegin(); it != subs_.rend(); ++it)
        (*it)->Shutdown();
}
