#ifndef LATENCY_MONITOR_H
#define LATENCY_MONITOR_H

#include <stddef.h>

typedef struct LatencyMonitor {
    double frame_timestamps[30];
    size_t count;
    size_t write_index;
} LatencyMonitor;

void latency_monitor_init(LatencyMonitor *monitor);
void latency_monitor_add_frame(LatencyMonitor *monitor, double now_seconds);
double latency_monitor_get_fps(const LatencyMonitor *monitor);
double latency_monitor_get_estimated_latency_ms(const LatencyMonitor *monitor);

#endif
