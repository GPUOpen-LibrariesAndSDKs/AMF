#//include "runtime/include/core/PerformanceMonitor.h"
#include "public/common/AMFFactory.h"
void EnableAMFProfiling(bool bEnable)
{
    if(bEnable)
    {
        //amf::AMFPerformanceMonitorLogger::Get().SetProfilingFolder(L".\\");
        //amf::AMFPerformanceMonitorLogger::Get().StartMonitoring();
        g_AMFFactory.GetDebug()->EnablePerformanceMonitor(true);
    }
    else
    {
        //amf::AMFPerformanceMonitorLogger::Get().StopMonitoring();
        g_AMFFactory.GetDebug()->EnablePerformanceMonitor(false);
    }
}