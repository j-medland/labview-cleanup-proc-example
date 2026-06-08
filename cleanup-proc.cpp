// This example is for MSVC compilation only
// No attempt has been made to make it cross platform or work with other compilers

#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <type_traits>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// LabVIEW Manager Functions Error Type
using LV_MgErr_t = int32_t;

// Error Codes
// https://www.ni.com/docs/en-US/bundle/labview/page/labview-manager-function-errors.html
const LV_MgErr_t LV_ERR_noError = 0;
const LV_MgErr_t LV_ERR_bogusError = 42;

// Generic LabVIEW Refs type
using LV_MagicCookie_t = uint32_t;
using LV_InstanceDataPtr_t = void *;
using LV_InstanceDataHandle_t = LV_InstanceDataPtr_t *;

// RTCleanupProc - function prototype for the callback

using LV_CleanupProcFnPtr_t = std::add_pointer_t<LV_MgErr_t(void *)>;
using LV_CleanupProcMode_t = int32_t;
enum class LV_CleanupProcModes : LV_CleanupProcMode_t
{
    OnRemove = 0,
    OnExit = 1,
    OnIdle = 2,
    AfterReset = 3
};

// RTSetCleanupProc - LabIVEW exported function to register a cleanup handler
// not documented but used in https://github.com/ni/grpc-labview/blob/51c1ef2dbeb9f20ca8e8d19bfa0420c385280a25/src/lv_interop.cc#L20
// LV_MgErr_t RTSetCleanupProc(LV_CleanupProcFnPtr_t, void*, LV_CleanupProcMode_t);
using LV_RTSetCleanupProcPtr_t = std::add_pointer_t<LV_MgErr_t(LV_CleanupProcFnPtr_t, void *, LV_CleanupProcMode_t)>;

static LV_RTSetCleanupProcPtr_t RTSetCleanupProcImp = nullptr;

LV_MgErr_t RTSetCleanupProc(LV_CleanupProcFnPtr_t fn, void *data, LV_CleanupProcModes mode)
{
    return RTSetCleanupProcImp ? RTSetCleanupProcImp(fn, data, static_cast<LV_CleanupProcMode_t>(mode)) : LV_ERR_bogusError;
}

// use LabVIEW's DbgPrintf as well for debug printing
using LV_DbgPrintfFnPtr_t = std::add_pointer_t<LV_MgErr_t(const char *, ...)>;
static LV_DbgPrintfFnPtr_t DbgPrintfImp = nullptr;

template<typename ...Args>
LV_MgErr_t DbgPrintf(const char *fmt, Args... args)
{
    if (DbgPrintfImp == nullptr)
    {
        return LV_ERR_bogusError;
    }

    DbgPrintfImp(fmt, args...);
    return LV_ERR_noError;
}

// DLL Entry Point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {

        HMODULE module = nullptr;

        // try loading from the normal runtimes
        static const char *const runtimes[] = {"LabVIEW.exe", "lvffrt.dll", "lvrt.dll"};

        // work through the list of runtimes
        for (const auto &runtime : runtimes)
        {
            module = GetModuleHandleA(runtime);
            if (module)
                break;
        }

        if (!module)
        {
            return false;
        }

        RTSetCleanupProcImp = reinterpret_cast<LV_RTSetCleanupProcPtr_t>(GetProcAddress(module, "RTSetCleanupProc"));

        // DbgPrintf
        DbgPrintfImp = reinterpret_cast<LV_DbgPrintfFnPtr_t>(GetProcAddress(module, "DbgPrintf"));
    }
    break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return true;
}

/**********************************************************************************************************/
//
//      Example Usage - Use an object with a registered cleanup procedure
//
/**********************************************************************************************************/

#include <unordered_map>

class MyObject;

// create a place to store object pointers that will work on 64-bit systems
// no cross thread protection provided! use a shared-mutex in real life

// we could just clean up the whole unordered_map when exiting but having
// each object clean itself up is more fun!
static std::unordered_map <LV_MagicCookie_t, MyObject*> object_map; 
static LV_MagicCookie_t next_handle = 1;

class MyObject
{
private:
    int32_t m_internal_value;
    static LV_MgErr_t cleanup(void *data_ptr)
    {
        auto self = reinterpret_cast<MyObject *>(data_ptr);

        DbgPrintf(">> Auto Cleanup of object with internal value %d", self->m_internal_value);

        delete self;

        return LV_ERR_noError;
    }

public:
    MyObject(int32_t val) : m_internal_value(val)
    {
        // register a clean-up handler - use this as the data cleanup handler
        RTSetCleanupProc(cleanup, this, LV_CleanupProcModes::OnIdle);
        DbgPrintf("Register cleanup proc for object with internal value %d", m_internal_value);
    }
    ~MyObject()
    {
        DbgPrintf("Destructor called for object internal value %d", m_internal_value);
        // deregister clean-up handler
        RTSetCleanupProc(cleanup, this, LV_CleanupProcModes::OnRemove);
        DbgPrintf("Deregister cleanup proc for object internal value %d", m_internal_value);
    }
};

extern "C"
{

    __declspec(dllexport) LV_MgErr_t example_create_object(int32_t value, LV_MagicCookie_t* key)
    {
        try
        {
            *key = next_handle++;
            object_map.insert(std::make_pair(*key, new MyObject(value)));
            return LV_ERR_noError;
        }
        catch (...)
        {
            return LV_ERR_bogusError;
        }
    }

    __declspec(dllexport) LV_MgErr_t explicitly_destroy_object(LV_MagicCookie_t* key)
    {
        try
        {
            auto it = object_map.find(*key);

            if(it != object_map.end()){
                delete it->second;
                object_map.erase(*key); 
            }

            return LV_ERR_noError;
        }
        catch (...)
        {
            return LV_ERR_bogusError;
        }
    }
}