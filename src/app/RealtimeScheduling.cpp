// pthread_setaffinity_np / pthread_setname_np are GNU extensions; the project is
// built with -std=c++17 (extensions off), so request them explicitly here.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "stablecops/app/RealtimeScheduling.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>

#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>

namespace stablecops::app {

namespace {

void warn(const char* what, int err) {
    std::cerr << "realtime: " << what << " failed (" << std::strerror(err)
              << "); continuing at normal priority. Grant CAP_SYS_NICE or raise "
                 "the rtprio ulimit (e.g. limits.conf '<user> - rtprio 99', "
                 "'<user> - memlock unlimited') to enable it.\n";
}

// Touch a chunk of stack so its pages are mapped before the cyclic loop starts,
// avoiding a first-touch page fault mid-cycle.
void prefaultStack() {
    constexpr int kStackPrefaultBytes = 64 * 1024;
    constexpr int kPageSize = 4096;
    volatile unsigned char buffer[kStackPrefaultBytes];
    volatile unsigned char* cursor = buffer;
    for (int offset = 0; offset < kStackPrefaultBytes; offset += kPageSize) {
        *(cursor + offset) = 0;
    }
}

}  // namespace

void applyRealtimeScheduling(const RtConfig& rt, const char* thread_name) {
    if (thread_name != nullptr) {
        // Truncated to 15 chars by the kernel; ignore failure (debug aid only).
        pthread_setname_np(pthread_self(), thread_name);
    }

    if (!rt.enabled) {
        return;
    }

    if (rt.lock_memory) {
        if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
            warn("mlockall", errno);
        } else {
            prefaultStack();
        }
    }

    sched_param param{};
    param.sched_priority = rt.priority;
    const int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    if (rc != 0) {
        warn("pthread_setschedparam(SCHED_FIFO)", rc);
    }

    if (rt.cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(rt.cpu, &set);
        const int arc =
            pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
        if (arc != 0) {
            warn("pthread_setaffinity_np", arc);
        }
    }
}

}  // namespace stablecops::app
