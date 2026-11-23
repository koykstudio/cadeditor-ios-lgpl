// KoykStudio stub for building planegcs outside FreeCAD: its debug logging and timers do nothing here.
#pragma once
#include <chrono>
#include <string>
namespace Base
{
struct ConsoleStub
{
    template<class... T>
    void log(const T&...)
    {}
    template<class... T>
    void warning(const T&...)
    {}
    template<class... T>
    void error(const T&...)
    {}
};
inline ConsoleStub& Console()
{
    static ConsoleStub c;
    return c;
}
struct TimeElapsed
{
    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    static double diffTimeF(const TimeElapsed& a, const TimeElapsed& b)
    {
        return std::chrono::duration<double>(b.t - a.t).count();
    }
};
}  // namespace Base
