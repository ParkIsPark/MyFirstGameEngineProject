#pragma once
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// FArchive (P2) — bidirectional serialization interface.
//
// One Serialize(FArchive&) method per type handles BOTH save and load: each
// ar.Field(key, value) writes when saving and reads when loading, so the two
// directions can never drift. The text archives below operate on one object's
// flat "key = value" field set; the .world reader/writer (P6) drives the
// section structure and hands each object its own archive.
//
// A future editor archive (P8) adds an Edit(UI) mode by subclassing FArchive
// and rendering ImGui widgets in Field()/Color() — the core stays GL-free.
// ---------------------------------------------------------------------------
class FArchive
{
public:
    virtual ~FArchive() = default;
    virtual bool IsLoading() const = 0;

    virtual void Field(const char* key, float& v)       = 0;
    virtual void Field(const char* key, int& v)         = 0;
    virtual void Field(const char* key, bool& v)        = 0;
    virtual void Field(const char* key, glm::vec2& v)   = 0;
    virtual void Field(const char* key, glm::vec3& v)   = 0;
    virtual void Field(const char* key, std::string& v) = 0;

    // Semantic hint: a color-valued vec3. Base treats it as a plain field; the
    // editor archive renders a color picker.
    virtual void Color(const char* key, glm::vec3& v) { Field(key, v); }
};

// ----- Save: accumulates "key = value" lines -------------------------------
class FSaveArchive : public FArchive
{
public:
    bool IsLoading() const override { return false; }
    void Field(const char* key, float& v)       override;
    void Field(const char* key, int& v)         override;
    void Field(const char* key, bool& v)        override;
    void Field(const char* key, glm::vec2& v)   override;
    void Field(const char* key, glm::vec3& v)   override;
    void Field(const char* key, std::string& v) override;

    const std::string& str() const { return out_; }

private:
    std::string out_;
};

// ----- Load: parses "key = value" lines, reads on demand (robust) ----------
class FLoadArchive : public FArchive
{
public:
    explicit FLoadArchive(const std::string& block);
    bool IsLoading() const override { return true; }
    void Field(const char* key, float& v)       override;
    void Field(const char* key, int& v)         override;
    void Field(const char* key, bool& v)        override;
    void Field(const char* key, glm::vec2& v)   override;
    void Field(const char* key, glm::vec3& v)   override;
    void Field(const char* key, std::string& v) override;

private:
    std::unordered_map<std::string, std::string> kv_;
};

// ---------------------------------------------------------------------------
// TFactory — TypeName string -> instance, for load-time construction.
// Each type self-registers via REGISTER_*; Create returns nullptr on unknown
// name (robust). Meyers-singleton table avoids static-init-order issues.
// ---------------------------------------------------------------------------
template <class Base>
class TFactory
{
public:
    using Creator = Base* (*)();
    static void  Register(const std::string& name, Creator c) { table()[name] = c; }
    static Base* Create(const std::string& name)
    {
        auto it = table().find(name);
        return it == table().end() ? nullptr : it->second();
    }
private:
    static std::unordered_map<std::string, Creator>& table()
    {
        static std::unordered_map<std::string, Creator> t;
        return t;
    }
};

class USceneComponent;
class AActor;
using FComponentFactory = TFactory<USceneComponent>;
using FActorFactory     = TFactory<AActor>;

#define REGISTER_COMPONENT(NAME, CLASS)                                       \
    namespace {                                                               \
        struct CLASS##_Reg {                                                  \
            CLASS##_Reg() { FComponentFactory::Register(                      \
                NAME, []() -> USceneComponent* { return new CLASS(); }); }    \
        } g_##CLASS##_Reg;                                                    \
    }

#define REGISTER_ACTOR(NAME, CLASS)                                           \
    namespace {                                                               \
        struct CLASS##_AReg {                                                 \
            CLASS##_AReg() { FActorFactory::Register(                         \
                NAME, []() -> AActor* { return new CLASS(); }); }             \
        } g_##CLASS##_AReg;                                                   \
    }
