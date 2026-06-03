#include "latency_monitor.h"

void latency_monitor_init(LatencyMonitor *monitor) {
    size_t i;
    if (monitor == 0) {
        return;
    }

    for (i = 0; i < 30; ++i) {
        monitor->frame_timestamps[i] = 0.0;
    }
    monitor->count = 0;
    monitor->write_index = 0;
}

void latency_monitor_add_frame(LatencyMonitor *monitor, double now_seconds) {
    if (monitor == 0) {
        return;
    }

    monitor->frame_timestamps[monitor->write_index] = now_seconds;
    monitor->write_index = (monitor->write_index + 1U) % 30U;
    if (monitor->count < 30U) {
        monitor->count += 1U;
    }
}

double latency_monitor_get_fps(const LatencyMonitor *monitor) {
    size_t oldest_index;
    size_t newest_index;
    double oldest;
    double newest;
    double duration;

    if (monitor == 0 || monitor->count < 2U) {
        return 0.0;
    }

    newest_index = (monitor->write_index + 29U) % 30U;
    oldest_index = (monitor->write_index + 30U - monitor->count) % 30U;
    newest = monitor->frame_timestamps[newest_index];
    oldest = monitor->frame_timestamps[oldest_index];
    duration = newest - oldest;

    if (duration <= 0.0) {
        return 0.0;
    }

    return (double)(monitor->count - 1U) / duration;
}

double latency_monitor_get_estimated_latency_ms(const LatencyMonitor *monitor) {
    double fps = latency_monitor_get_fps(monitor);
    if (fps <= 0.0) {
        return 0.0;
    }
    return (1.0 / fps) * 1000.0 * 2.0;
}
