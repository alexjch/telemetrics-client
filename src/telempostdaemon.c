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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <dirent.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <sys/signalfd.h>

#include "log.h"
#include "util.h"
#include "spool.h"
#include "iorecord.h"
#include "retention.h"
#include "telempostdaemon.h"

static void set_pollfd(TelemPostDaemon *daemon, int fd, enum fdindex i, short events)
{
        assert(daemon);
        assert(fd);

        daemon->pollfds[i].fd = fd;
        daemon->pollfds[i].events = events;
        daemon->pollfds[i].revents = 0;
}

static void initialize_signals(TelemPostDaemon *daemon)
{
        int sigfd;
        sigset_t mask;

        sigemptyset(&mask);

        if (sigaddset(&mask, SIGHUP) != 0) {
                telem_perror("Error adding signal SIGHUP to mask");
                exit(EXIT_FAILURE);
        }

        if (sigaddset(&mask, SIGINT) != 0) {
                telem_perror("Error adding signal SIGINT to mask");
                exit(EXIT_FAILURE);
        }

        if (sigaddset(&mask, SIGTERM) != 0) {
                telem_perror("Error adding signal SIGTERM to mask");
                exit(EXIT_FAILURE);
        }

        if (sigaddset(&mask, SIGPIPE) != 0) {
                telem_perror("Error adding signal SIGPIPE to mask");
                exit(EXIT_FAILURE);
        }

        if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
                telem_perror("Error changing signal mask with SIG_BLOCK");
                exit(EXIT_FAILURE);
        }

        sigfd = signalfd(-1, &mask, 0);
        if (sigfd == -1) {
                telem_perror("Error creating the signalfd");
                exit(EXIT_FAILURE);
        }

        daemon->sfd = sigfd;

        set_pollfd(daemon, sigfd, signlfd, POLLIN);
}

static void initialize_rate_limit(TelemPostDaemon *daemon)
{
        for (int i = 0; i < TM_RATE_LIMIT_SLOTS; i++) {
                daemon->record_burst_array[i] = 0;
                daemon->byte_burst_array[i] = 0;
        }
        daemon->rate_limit_enabled = rate_limit_enabled_config();
        daemon->record_burst_limit = record_burst_limit_config();
        daemon->record_window_length = record_window_length_config();
        daemon->byte_burst_limit = byte_burst_limit_config();
        daemon->byte_window_length = byte_window_length_config();
        daemon->rate_limit_strategy = rate_limit_strategy_config();
}

static void initialize_record_delivery(TelemPostDaemon *daemon)
{
        daemon->record_retention_enabled = record_retention_enabled_config();
        daemon->record_server_delivery_enabled = record_server_delivery_enabled_config();
}

void initialize_daemon(TelemPostDaemon *daemon)
{
        assert(daemon);

        daemon->bypass_http_post_ts = 0;
        daemon->is_spool_valid = is_spool_valid();
        daemon->record_journal = open_journal(JOURNAL_PATH);
        /* Register record retention delete action as a callback to prune entry */
        if (daemon->record_journal != NULL && daemon->record_retention_enabled) {
                daemon->record_journal->prune_entry_callback = &delete_record_by_id;
        }
        daemon->fd = inotify_init();
        if (daemon->fd < 0) {
                perror("Error initializing inotify");
        }
        daemon->wd = inotify_add_watch(daemon->fd, DEFAULT_STAGE_DIR, IN_CLOSE_WRITE);

        initialize_signals(daemon);
        set_pollfd(daemon, daemon->fd, watchfd, POLLIN);

        initialize_rate_limit(daemon);
        initialize_record_delivery(daemon);
        daemon->current_spool_size = 0;
}

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
        telem_log(LOG_DEBUG, "Received data:\n%s\n", ptr);
        return size * nmemb;
}

bool post_record_http(char *headers[], char *body)
{
        CURL *curl;
        int res = 0;
        char *content = "Content-Type: application/text";
        struct curl_slist *custom_headers = NULL;
        char errorbuf[CURL_ERROR_SIZE];
        long http_response = 0;
        const char *cert_file = get_cainfo_config();
        const char *tid_header = get_tidheader_config();

        // Initialize the libcurl global environment once per POST. This lets us
        // clean up the environment after each POST so that when the daemon is
        // sitting idle, it will be consuming as little memory as possible.
        curl_global_init(CURL_GLOBAL_ALL);

        curl = curl_easy_init();
        if (!curl) {
                telem_log(LOG_ERR, "curl_easy_init(): Unable to start libcurl"
                          " easy session, exiting\n");
                exit(EXIT_FAILURE);
                /* TODO: check if memory needs to be released */
        }

        // Errors for any curl_easy_* functions will store nice error messages
        // in errorbuf, so send log messages with errorbuf contents
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorbuf);

        curl_easy_setopt(curl, CURLOPT_URL, server_addr_config());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_POST, 1);
#ifdef DEBUG
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
#endif

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

        for (int i = 0; i < NUM_HEADERS; i++) {
                custom_headers = curl_slist_append(custom_headers, headers[i]);
        }
        custom_headers = curl_slist_append(custom_headers, tid_header);
        // This should be set by probes/libtelemetry in the future
        custom_headers = curl_slist_append(custom_headers, content);

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, custom_headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(body));
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);

        if (strlen(cert_file) > 0) {
                if (access(cert_file, F_OK) != -1) {
                        curl_easy_setopt(curl, CURLOPT_CAINFO, cert_file);
                        telem_log(LOG_INFO, "cafile was set to %s\n", cert_file);
                }
        }

        telem_log(LOG_DEBUG, "Executing curl operation...\n");
        errorbuf[0] = 0;
        res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response);

        if (res) {
                size_t len = strlen(errorbuf);
                if (len) {
                        telem_log(LOG_ERR, "Failed sending record: %s%s", errorbuf,
                                  ((errorbuf[len - 1] != '\n') ? "\n" : ""));
                } else {
                        telem_log(LOG_ERR, "Failed sending record: %s\n",
                                  curl_easy_strerror(res));
                }
        } else if (http_response != 201 && http_response != 200) {
                /*  201 means the record was  successfully created
                 *  200 is a generic "ok".
                 */
                telem_log(LOG_ERR, "Encountered error %ld on the server\n",
                          http_response);
                // We treat HTTP error codes the same as libcurl errors
                res = 1;
        } else {
                telem_log(LOG_INFO, "Record sent successfully\n");
        }

        curl_slist_free_all(custom_headers);
        curl_easy_cleanup(curl);

        curl_global_cleanup();

        return res ? false : true;
}

static void save_local_copy(TelemPostDaemon *daemon, char *body)
{
        int ret = 0;
        char *tmpbuf = NULL;
        FILE *tmpfile = NULL;

        if (daemon == NULL || daemon->record_journal == NULL ||
            daemon->record_journal->latest_record_id == NULL) {
                return;
        }

        ret = asprintf(&tmpbuf, "%s/%s", RECORD_RETENTION_DIR,
                       daemon->record_journal->latest_record_id);
        if (ret == -1) {
                telem_log(LOG_ERR, "Failed to allocate memory for record full path, aborting\n");
                return;
        }

        tmpfile = fopen(tmpbuf, "w");
        if (!tmpfile) {
                telem_perror("Error opening local record copy temp file");
                goto save_err;
        }

        // Save body
        fprintf(tmpfile, "%s\n", body);
        fclose(tmpfile);

save_err:
        free(tmpbuf);
        return;
}

static void spool_record(TelemPostDaemon *daemon, char *headers[], char *body)
{
        int ret = 0;
        struct stat stat_buf;
        int64_t max_spool_size = 0;
        char *tmpbuf = NULL;
        FILE *tmpfile = NULL;
        int tmpfd = 0;

        if (!daemon->is_spool_valid) {
                /* If spool is not valid, simply drop the record */
                return;
        }

        //check if the size is greater than 1 MB/spool max size
        max_spool_size = spool_max_size_config();
        if (max_spool_size != -1) {
                telem_log(LOG_DEBUG, "Total size of spool dir: %ld\n", daemon->current_spool_size);
                if (daemon->current_spool_size < 0) {
                        telem_log(LOG_ERR, "Error getting spool directory size: %s\n",
                                  strerror(-(int)(daemon->current_spool_size)));
                        return;
                } else if (daemon->current_spool_size >= (max_spool_size * 1024)) {
                        telem_log(LOG_INFO, "Spool dir full, dropping record\n");
                        return;
                }
        }

        // create file with record
        ret = asprintf(&tmpbuf, "%s/XXXXXX", spool_dir_config());
        if (ret == -1) {
                telem_log(LOG_ERR, "Failed to allocate memory for record name, aborting\n");
                exit(EXIT_FAILURE);
        }

        tmpfd = mkstemp(tmpbuf);
        if (tmpfd == -1) {
                telem_perror("Error while creating temp file");
                goto spool_err;
        }

        tmpfile = fdopen(tmpfd, "a");
        if (!tmpfile) {
                telem_perror("Error opening temp file");
                close(tmpfd);
                if (unlink(tmpbuf)) {
                        telem_perror("Error deleting temp file");
                }
                goto spool_err;
        }

        // write headers
        for (int i = 0; i < NUM_HEADERS; i++) {
                fprintf(tmpfile, "%s\n", headers[i]);
        }

        //write body
        fprintf(tmpfile, "%s\n", body);
        fflush(tmpfile);
        fclose(tmpfile);

        /* The stat should not fail here unless it is ENOMEM */
        if (stat(tmpbuf, &stat_buf) == 0) {
                daemon->current_spool_size += (stat_buf.st_blocks * 512);
        }

spool_err:
        free(tmpbuf);
        return;
}

static void save_entry_to_journal(TelemPostDaemon *daemon, time_t t_stamp, char *headers[])
{
        char *classification_value;
        char *event_id_value;

        if (get_header_value(headers[TM_CLASSIFICATION], &classification_value) &&
            get_header_value(headers[TM_EVENT_ID], &event_id_value)) {
                if (new_journal_entry(daemon->record_journal, classification_value, t_stamp, event_id_value) != 0) {
                        telem_log(LOG_INFO, "new_journal_entry in process_record: failed saving record entry\n");
                }
        }
        free(classification_value);
        free(event_id_value);
}

bool process_staged_record(char *filename, TelemPostDaemon *daemon)
{
        int k;
        bool ret = true;
        char *headers[NUM_HEADERS];
        char *body = NULL;
        int current_minute;
        bool do_spool = false;
        bool record_sent = false;
        time_t temp = time(NULL);
        struct tm *tm_s = localtime(&temp);
        current_minute = tm_s->tm_min;
        /* Checks flags */
        bool record_check_passed = true;
        bool byte_check_passed = true;
        bool record_burst_enabled = true;
        bool byte_burst_enabled = true;

        for (k = 0; k < NUM_HEADERS; k++) {
                headers[k] = NULL;
        }

        /* Load file */
        if ((ret = read_record(filename, headers, &body)) == false) {
                telem_log(LOG_WARNING, "unable to read record\n");
                /**
                 * Not a failure condition, likely record is corrupted
                 * Preferable to drop it than having a record stuck in
                 * the staging directory */
                goto end_processing_file;
        }

        /** Record retention **/
        // Save record in journal
        save_entry_to_journal(daemon, temp, headers);
        // Save local copy
        if (daemon->record_retention_enabled) {
                save_local_copy(daemon, body);
        }

        /* Bail out if server delivery is not enabled */
        if (!daemon->record_server_delivery_enabled) {
#ifdef DEBUG
                telem_log(LOG_WARNING, "record server delivery disabled\n");
#endif
                // Not an error condition
                goto end_processing_file;
        }

        /** Spool policies **/
        if (inside_direct_spool_window(daemon, time(NULL))) {
                telem_log(LOG_INFO, "process_record: delivering directly to spool\n");
                spool_record(daemon, headers, body);
                // Not an error condition
                goto end_processing_file;
        }
        if (daemon->record_window_length == -1 ||
            daemon->byte_window_length == -1) {
                telem_log(LOG_ERR, "Invalid value for window length\n");
                exit(EXIT_FAILURE);
        }
        /* Checks if entirety of rate limiting is enabled */
        if (daemon->rate_limit_enabled) {
                /* Checks whether record and byte bursts are enabled individually */
                record_burst_enabled = burst_limit_enabled(daemon->record_burst_limit);
                byte_burst_enabled = burst_limit_enabled(daemon->byte_burst_limit);

                if (record_burst_enabled) {
                        record_check_passed = rate_limit_check(current_minute,
                                                               daemon->record_burst_limit, daemon->record_window_length,
                                                               daemon->record_burst_array, TM_RECORD_COUNTER);
                }
                if (byte_burst_enabled) {
                        byte_check_passed = rate_limit_check(current_minute,
                                                             daemon->byte_burst_limit, daemon->byte_window_length,
                                                             daemon->byte_burst_array, RECORD_SIZE_LEN);
                }
                /* If both record and byte burst disabled, rate limiting disabled */
                if (!record_burst_enabled && !byte_burst_enabled) {
                        daemon->rate_limit_enabled = false;
                }
        }

        /* Sends record if rate limiting is disabled, or all checks passed */
        if (!daemon->rate_limit_enabled || (record_check_passed && byte_check_passed)) {
                /* Send the record as https post */
                record_sent = post_record_ptr(headers, body);
                /* This is the only error condition in the whole function
                 * if the record is not successfully posted to backend */
                ret = record_sent;
        }
        // Get rate-limit strategy
        do_spool = spool_strategy_selected(daemon);

        // Drop record
        if (!record_sent && !do_spool) {
                // Not an error condition
                goto end_processing_file;
        }
        // Spool Record
        else if (!record_sent && do_spool) {
                start_network_bypass(daemon);
                telem_log(LOG_INFO, "process_record: initializing direct-spool window\n");
                spool_record(daemon, headers, body);
        } else {
                /* Updates rate limiting arrays if record sent */
                if (record_burst_enabled) {
                        rate_limit_update (current_minute, daemon->record_window_length,
                                           daemon->record_burst_array, TM_RECORD_COUNTER);
                }
                if (byte_burst_enabled) {
                        rate_limit_update (current_minute, daemon->byte_window_length,
                                           daemon->byte_burst_array, RECORD_SIZE_LEN);
                }
        }

end_processing_file:
        free(body);

        for (k = 0; k < NUM_HEADERS; k++) {
                free(headers[k]);
        }

        return ret;
}

static int directory_dot_filter(const struct dirent *entry)
{
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
                return 0;
        } else {
                return 1;
        }
}

void staging_records_loop(TelemPostDaemon *daemon)
{
        int ret;
        int numentries;
        struct dirent **namelist;

        numentries = scandir(DEFAULT_STAGE_DIR, &namelist, directory_dot_filter, NULL);

        if (numentries == 0) {
                telem_log(LOG_DEBUG, "No entries in staging\n");
                return;
        } else if (numentries < 0) {
                telem_perror("Error while scanning staging");
                return;
        }

        for (int i = 0; i < numentries; i++) {
                char *record_path;
                telem_log(LOG_DEBUG, "Processing staged record: %s\n",
                          namelist[i]->d_name);
                ret = asprintf(&record_path, "%s/%s", DEFAULT_STAGE_DIR, namelist[i]->d_name);
                if (ret == -1) {
                        telem_log(LOG_ERR, "Failed to allocate memory for staging record full path\n");
                        exit(EXIT_FAILURE);
                }
                if (process_staged_record(record_path, daemon)) {
                        unlink(record_path);
                }
                free(record_path);
        }

        for (int i = 0; i < numentries; i++) {
                free(namelist[i]);
        }
        free(namelist);
}

void run_daemon(TelemPostDaemon *daemon)
{
        int ret;
        int spool_process_time = spool_process_time_config();
        time_t last_spool_run_time = time(NULL);

        assert(daemon);
        assert(daemon->pollfds);
        assert(daemon->pollfds[signlfd].fd);
        assert(daemon->pollfds[watchfd].fd);

        while (1) {

                ret = poll(daemon->pollfds, NFDS, spool_process_time);
                if (ret == -1) {
                        telem_perror("Failed to poll daemon file descriptors");
                        break;
                } else if (ret != 0) {

                        if (daemon->pollfds[signlfd].revents != 0) {
                                struct signalfd_siginfo fdsi;
                                ssize_t s;
                                s = read(daemon->sfd, &fdsi, sizeof(struct signalfd_siginfo));
                                if (s != sizeof(struct signalfd_siginfo)) {
                                        telem_perror("Error while reading from the signal"
                                                     "file descriptor");
                                        exit(EXIT_FAILURE);
                                }

                                if (fdsi.ssi_signo == SIGTERM || fdsi.ssi_signo == SIGINT) {
                                        telem_log(LOG_INFO, "Received either a \
                                                                     SIGINT/SIGTERM signal\n");
                                        break;
                                }
                        } else if (daemon->pollfds[watchfd].revents != 0) {

                                int ret = 0;
                                ssize_t i = 0;
                                ssize_t length = 0;
                                char buffer[BUFFER_LEN];

                                length = read(daemon->fd, buffer, BUFFER_LEN);
                                if (length < 0) {
                                        perror("Reading descriptor returned error");
                                }

                                while (i < length) {
                                        struct inotify_event *event = (struct inotify_event *)&buffer[i];

                                        if (event->len) {
                                                if (event->mask & IN_CLOSE_WRITE && !(event->mask & IN_ISDIR)) {
                                                        char *record_name = NULL;

                                                        /* Retrieve foldername from watch id?  */
                                                        ret = asprintf(&record_name, "%s/%s", DEFAULT_STAGE_DIR, event->name);
                                                        if (ret == -1) {
                                                                telem_log(LOG_ERR, "Failed to allocate memory for record full path, aborting\n");
                                                                exit(EXIT_FAILURE);
                                                        }
                                                        /* Process inotify event */
                                                        if (process_staged_record(record_name, daemon)) {
                                                                unlink(record_name);
                                                        }
                                                        free(record_name);
                                                }
                                        }

                                        i += (ssize_t)EVENT_SIZE + event->len;
                                }
                        }
                }

                /* Check spool  */
                time_t now = time(NULL);
                if (difftime(now, last_spool_run_time) >= spool_process_time) {
                        spool_records_loop(&(daemon->current_spool_size));
                        last_spool_run_time = time(NULL);
                }
                /* Check journal records and prune if needed */
                ret = prune_journal(daemon->record_journal, JOURNAL_TMPDIR);
                if (ret != 0) {
                        telem_log(LOG_WARNING, "Unable to prune journal\n");
                }
        }
}

void close_daemon(TelemPostDaemon *daemon)
{

        if (daemon->fd) {
                if (daemon->wd) {
                        inotify_rm_watch(daemon->fd, daemon->wd);
                }
                close(daemon->fd);
        }

        close_journal(daemon->record_journal);
}

/* vi: set ts=8 sw=8 sts=4 et tw=80 cino=(0: */
