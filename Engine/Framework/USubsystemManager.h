#pragma once
#include <vector>

class USubsystem;

// ---------------------------------------------------------------------------
// USubsystemManager — owns a list of USubsystems and drives them as a group.
//   InitAll / TickAll  : registration order
//   ShutdownAll        : reverse order (mirror teardown)
// Takes ownership of every Register()ed subsystem and deletes them (reverse)
// on destruction.
// ---------------------------------------------------------------------------
class USubsystemManager
{
public:
    ~USubsystemManager();

    void Register(USubsystem* sub);   // takes ownership

    void InitAll();
    void TickAll(float dt);
    void ShutdownAll();               // safe to call once; no-op if already done

    // Returns the first registered subsystem of type T, or nullptr.
    template <class T>
    T* Get() const
    {
        for (USubsystem* s : subs_)
            if (T* t = dynamic_cast<T*>(s)) return t;
        return nullptr;
    }

private:
    std::vector<USubsystem*> subs_;
    bool shutdownDone_ = false;
};
