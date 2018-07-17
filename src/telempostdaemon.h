/*
 * This program is part of the Clear Linux Project
 *
 * Copyright 2015 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms and conditions of the GNU Lesser General Public License, as
 * published by the Free Software Foundation; either version 2.1 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 */

#define EVENT_SIZE sizeof(struct inotify_event)
#define BUFFER_LEN 1024 * (EVENT_SIZE + 16)
#define NFDS 2
#define TM_RATE_LIMIT_SLOTS (1 /*h*/ * 60 /*m*/)
#define TM_RECORD_COUNTER (1)

#include <poll.h>
#include <stdbool.h>
#include <sys/inotify.h>

#include "common.h"
#include "journal/journal.h"
#include "configuration.h"

enum fdindex {signlfd, watchfd};

typedef struct TelemPostDaemon {
        int fd;
        int wd;
        int sfd;
        char event_buffer[BUFFER_LEN];
        struct pollfd pollfds[NFDS];
        /* Telemetry Journal*/
        TelemJournal *record_journal;
        /* Time of last failed post */
        time_t bypass_http_post_ts;
        /* Rate limit record and byte arrays */
        size_t record_burst_array[TM_RATE_LIMIT_SLOTS];
        size_t byte_burst_array[TM_RATE_LIMIT_SLOTS];
        /* Rate Limit Configurations */
        bool rate_limit_enabled;
        int64_t record_burst_limit;
        int record_window_length;
        int64_t byte_burst_limit;
        int byte_window_length;
        const char *rate_limit_strategy;
        /* Spool configuration */
        bool is_spool_valid;
        long current_spool_size;
        /* Record local copy and delivery  */
        bool record_retention_enabled;
        bool record_server_delivery_enabled;
        char *machine_id_override;
} TelemPostDaemon;

/**
 * Initializes daemon
 *
 * @param daemon a pointer to telemetry post daemon
 */
void initialize_daemon(TelemPostDaemon *daemon);

/**
 * Starts daemon
 *
 * @param daemon a pointer to telemetry post daemon
 */
void run_daemon(TelemPostDaemon *daemon);

/**
 * Cleans up inotify descriptors
 *
 * @param daemon a pointer to telemetry post daemon
 */
void close_daemon(TelemPostDaemon *daemon);

/**
 * Processed record written on disk
 *
 * @param filename a pointor to record on disk
 * @param daemon post to telemetry post daemon
 */
bool process_staged_record(char *filename, TelemPostDaemon *daemon);

/**
 * Scans staging directory to process files that were
 * missed by file watcher
 *
 * @param daemon a pointer to telemetry post daemon
 */
void staging_records_loop(TelemPostDaemon *daemon);

/**
 *
 *
 *
 */
bool post_record_http(char *headers[], char *body);


static inline bool inside_direct_spool_window(TelemPostDaemon *daemon, time_t current_time)
{
        return (current_time < daemon->bypass_http_post_ts + 1800) ? true : false;
}

static inline void start_network_bypass(TelemPostDaemon *daemon)
{
        daemon->bypass_http_post_ts = time(NULL);
}

static inline bool burst_limit_enabled(int64_t burst_limit)
{
        return (burst_limit > -1) ? true : false;
}

static inline bool rate_limit_check(int current_minute, int64_t burst_limit, int
                                    window_length, size_t *array, size_t incValue)
{
        size_t count = 0;

        /* Variable to determine last minute allowed in burst window */
        int window_start = (TM_RATE_LIMIT_SLOTS + (current_minute - window_length + 1))
                           % TM_RATE_LIMIT_SLOTS;

        /* The modulo is not placed in the for loop inself, because if
         * the current minute is less than the window start (from wrapping)
         * the for loop will never be entered.
         */

        /* Counts all elements within burst_window in array */
        for (int i = window_start; i < (window_start + window_length); i++) {
                count += array[i % TM_RATE_LIMIT_SLOTS];

                if ((array[i % TM_RATE_LIMIT_SLOTS] + incValue) >= SIZE_MAX) {
                        /* Exceeds maximum size array can hold */
                        return false;
                }
        }
        /* Include current record being processed to count */
        count += incValue;

        /* Determines whether the count has exceeded the limit or not */
        return (count > burst_limit) ? false : true;
}

static inline bool spool_strategy_selected(TelemPostDaemon *daemon)
{
        /* Performs action depending on strategy choosen */
        return (strcmp(daemon->rate_limit_strategy, "spool") == 0) ? true : false;
}

static inline void rate_limit_update(int current_minute, int window_length, size_t *array,
                                     size_t incValue)
{
        /* Variable to determine the amount of zero'd out spots needed */
        int blank_slots = (TM_RATE_LIMIT_SLOTS - window_length);

        /* Update specified array depending on increment value */
        array[current_minute] += incValue;

        /* Zero out expired records for record array */
        for (int i = current_minute + 1; i < (blank_slots + current_minute + 1); i++) {
                array[i % TM_RATE_LIMIT_SLOTS] = 0;
        }
}

/* vi: set ts=8 sw=8 sts=4 et tw=80 cino=(0: */
