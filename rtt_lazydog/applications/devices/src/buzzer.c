#include <rtthread.h>
#include <rtdevice.h>
#include "app_config.h"
#include "buzzer.h"

#define DBG_TAG "buzzer"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define BUZZER_THREAD_NAME       "buzzer"
#define BUZZER_THREAD_STACK      768
#define BUZZER_THREAD_PRIORITY   22
#define BUZZER_THREAD_TICK       10

static struct rt_mutex buzzer_lock;
static rt_bool_t buzzer_ready = RT_FALSE;
static buzzer_pattern_t buzzer_pattern = BUZZER_PATTERN_OFF;
static rt_tick_t buzzer_pattern_since = 0;

const char *buzzer_pattern_name(buzzer_pattern_t pattern)
{
    switch (pattern)
    {
    case BUZZER_PATTERN_OFF:
        return "off";
    case BUZZER_PATTERN_ON:
        return "on";
    case BUZZER_PATTERN_TURN:
        return "turn";
    case BUZZER_PATTERN_FALL:
        return "fall";
    case BUZZER_PATTERN_WARN:
        return "warn";
    default:
        return "unknown";
    }
}

static void buzzer_write(rt_bool_t active)
{
#if APP_BUZZER_ACTIVE_HIGH
    rt_pin_write(APP_BUZZER_PIN, active ? PIN_HIGH : PIN_LOW);
#else
    rt_pin_write(APP_BUZZER_PIN, active ? PIN_LOW : PIN_HIGH);
#endif
}

static rt_bool_t buzzer_period_active(rt_tick_t now,
                                      rt_tick_t since,
                                      rt_uint32_t period_ms,
                                      rt_uint32_t on_ms)
{
    rt_tick_t elapsed_ticks;
    rt_uint32_t elapsed_ms;

    if ((period_ms == 0U) || (on_ms == 0U))
    {
        return RT_FALSE;
    }

    elapsed_ticks = now - since;
    elapsed_ms = ((rt_uint32_t)elapsed_ticks * 1000U) / RT_TICK_PER_SECOND;
    return ((elapsed_ms % period_ms) < on_ms) ? RT_TRUE : RT_FALSE;
}

static void buzzer_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (1)
    {
        buzzer_pattern_t pattern;
        rt_tick_t since;
        rt_bool_t active = RT_FALSE;

        rt_mutex_take(&buzzer_lock, RT_WAITING_FOREVER);
        pattern = buzzer_pattern;
        since = buzzer_pattern_since;
        rt_mutex_release(&buzzer_lock);

        switch (pattern)
        {
        case BUZZER_PATTERN_ON:
            active = RT_TRUE;
            break;
        case BUZZER_PATTERN_TURN:
            active = buzzer_period_active(rt_tick_get(),
                                          since,
                                          APP_BUZZER_TURN_PERIOD_MS,
                                          APP_BUZZER_TURN_ON_MS);
            break;
        case BUZZER_PATTERN_FALL:
            active = buzzer_period_active(rt_tick_get(),
                                          since,
                                          APP_BUZZER_FALL_PERIOD_MS,
                                          APP_BUZZER_FALL_ON_MS);
            break;
        case BUZZER_PATTERN_WARN:
            active = buzzer_period_active(rt_tick_get(),
                                          since,
                                          APP_BUZZER_WARN_PERIOD_MS,
                                          APP_BUZZER_WARN_ON_MS);
            break;
        case BUZZER_PATTERN_OFF:
        default:
            active = RT_FALSE;
            break;
        }

        buzzer_write(active);
        rt_thread_mdelay(APP_BUZZER_POLL_MS);
    }
}

rt_err_t buzzer_set_pattern(buzzer_pattern_t pattern)
{
    if (!buzzer_ready)
    {
        return -RT_ERROR;
    }

    if ((pattern < BUZZER_PATTERN_OFF) || (pattern > BUZZER_PATTERN_WARN))
    {
        return -RT_EINVAL;
    }

    rt_mutex_take(&buzzer_lock, RT_WAITING_FOREVER);
    if (pattern != buzzer_pattern)
    {
        buzzer_pattern = pattern;
        buzzer_pattern_since = rt_tick_get();
    }
    rt_mutex_release(&buzzer_lock);

    return RT_EOK;
}


int buzzer_init(void)
{
    rt_err_t result;
    rt_thread_t thread;

    if (buzzer_ready)
    {
        return RT_EOK;
    }

    result = rt_mutex_init(&buzzer_lock, "buzzlk", RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        LOG_E("buzzer lock init failed");
        return result;
    }

    rt_pin_mode(APP_BUZZER_PIN, PIN_MODE_OUTPUT);
    buzzer_write(RT_FALSE);
    buzzer_pattern = BUZZER_PATTERN_OFF;
    buzzer_pattern_since = rt_tick_get();

    thread = rt_thread_create(BUZZER_THREAD_NAME,
                              buzzer_thread_entry,
                              RT_NULL,
                              BUZZER_THREAD_STACK,
                              BUZZER_THREAD_PRIORITY,
                              BUZZER_THREAD_TICK);
    if (thread == RT_NULL)
    {
        LOG_E("buzzer thread create failed");
        rt_mutex_detach(&buzzer_lock);
        buzzer_write(RT_FALSE);
        return -RT_ERROR;
    }

    buzzer_ready = RT_TRUE;
    rt_thread_startup(thread);
    return RT_EOK;
}
