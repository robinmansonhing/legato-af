 /**
  * This is component 2 for the multi-component logging unit test.
  *
  * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
  */

#ifdef LE_COMPONENT_NAME
# undef LE_COMPONENT_NAME
#endif

#ifdef LE_LOG_SESSION
# undef LE_LOG_SESSION
#endif

#ifdef LE_LOG_LEVEL_FILTER_PTR
# undef LE_LOG_LEVEL_FILTER_PTR
#endif

#define LE_COMPONENT_NAME       Comp_2
#define LE_LOG_SESSION          Comp_2_LogSession
#define LE_LOG_LEVEL_FILTER_PTR Comp_2_LogLevelFilterPtr


#include "legato.h"
#include "component2.h"


//--------------------------------------------------------------------------------------------------
/**
 * Initialize the component.
 */
//--------------------------------------------------------------------------------------------------
void comp2_Init(void)
{
}


//--------------------------------------------------------------------------------------------------
/**
 * Component code that does some work and logs messages and traces.
 */
//--------------------------------------------------------------------------------------------------
void comp2_Foo(void)
{
    LE_DEBUG("comp2 %d msg", LE_LOG_DEBUG);
    LE_INFO("comp2 %d msg", LE_LOG_INFO);
    LE_WARN("comp2 %d msg", LE_LOG_WARN);
    LE_ERROR("comp2 %d msg", LE_LOG_ERR);
    LE_CRIT("comp2 %d msg", LE_LOG_CRIT);
    LE_EMERG("comp2 %d msg", LE_LOG_EMERG);

    le_log_TraceRef_t trace1 = le_log_GetTraceRef("key 1");
    le_log_TraceRef_t trace2 = le_log_GetTraceRef("key 2");

    LE_TRACE(trace1, "Trace msg in %s", STRINGIZE(LE_COMPONENT_NAME));
    LE_TRACE(trace2, "Trace msg in %s", STRINGIZE(LE_COMPONENT_NAME));
}
