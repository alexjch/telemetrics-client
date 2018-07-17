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

#include <check.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <unistd.h>

#include "configuration.h"
#include "telemdaemon.h"
#include "common.h"

TelemDaemon tdaemon;
static bool post_was_called = false;

char *get_serialized_record(char *headers, char *post_body, size_t *record_size)
{
        size_t totalsize, headersize, payloadsize, offset;
        char *data;

        offset = 0;
        headersize = strlen(headers);
        payloadsize = strlen(post_body);
        totalsize = headersize + payloadsize;

        *record_size = 2 * sizeof(uint32_t) + totalsize + 1;
        data = malloc(*record_size);
        memset(data, 0, *record_size);
        memcpy(data, &totalsize, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        memcpy(data + offset, &headersize, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        memcpy(data + offset, headers, headersize);
        offset += headersize;

        memcpy(data + offset, post_body, payloadsize + 1);
        return data;
}

bool dummy_post(char *headers[], char *body, bool spool)
{
        post_was_called = true;
        return post_was_called;
}

bool (*post_record_ptr)(char *[], char *, bool) = dummy_post;

void setup(void)
{
        char *config_file = ABSTOPSRCDIR "/src/data/example.conf";
        set_config_file(config_file);

        initialize_daemon(&tdaemon);
}

START_TEST(check_daemon_is_initialized)
{
        setup();

        ck_assert(tdaemon.nfds == 0);
        ck_assert(tdaemon.pollfds == NULL);
}
END_TEST

int get_poll_fd(TelemDaemon *tdaemon, nfds_t i)
{
        if (i >= 0 && i < tdaemon->nfds) {
                return tdaemon->pollfds[i].fd;
        }
        return -1;
}

START_TEST(check_add_del_poll_fd)
{
        setup();

        int fd = 1;
        int short events = 1;
        add_pollfd(&tdaemon, fd, events);
        add_pollfd(&tdaemon, ++fd, events);
        add_pollfd(&tdaemon, ++fd, events);
        ck_assert_msg(tdaemon.nfds == 3, "Failed to add pollfd");

        fd = get_poll_fd(&tdaemon, 0);
        ck_assert(fd == 1);
        fd = get_poll_fd(&tdaemon, 1);
        ck_assert(fd == 2);
        fd = get_poll_fd(&tdaemon, 2);
        ck_assert(fd == 3);

        del_pollfd(&tdaemon, 1);
        ck_assert_msg(tdaemon.nfds == 2, "Failed to delete pollfd");
        fd = get_poll_fd(&tdaemon, 1);
        ck_assert(fd == 3);

        del_pollfd(&tdaemon, 1);
        ck_assert_msg(tdaemon.nfds == 1, "Failed to delete pollfd");
        fd = get_poll_fd(&tdaemon, 1);
        ck_assert(fd == -1);

        del_pollfd(&tdaemon, 0);
        ck_assert_msg(tdaemon.nfds == 0, "Failed to delete pollfd");
        fd = get_poll_fd(&tdaemon, 0);
        ck_assert(fd == -1);
}
END_TEST

START_TEST(check_add_remove_client)
{
        setup();

        client *cl, *cl1, *cl2, *cl3;
        cl1 = add_client(&(tdaemon.client_head), 1);
        cl2 = add_client(&(tdaemon.client_head), 2);
        cl3 = add_client(&(tdaemon.client_head), 3);

        int i = 3;
        LIST_FOREACH(cl, &(tdaemon.client_head), client_ptrs) {
                ck_assert_msg((cl->fd == i), "Actual value is %d\n", cl->fd);
                i--;
        }

        remove_client(&(tdaemon.client_head), cl1);
        remove_client(&(tdaemon.client_head), cl2);
        remove_client(&(tdaemon.client_head), cl3);
        //ck_assert(tdaemon.client_head.lh_first == NULL);
        ck_assert_msg(is_client_list_empty(&(tdaemon.client_head)), "Failed to remove clients\n");
}
END_TEST

void set_up_socket_pair(int *client, int *server)
{
        int sv[2];
        int ret;

        ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        ck_assert_msg(ret == 0, "Failed to create socket pair\n");
        ck_assert_msg((fcntl(sv[0], F_SETFL, O_NONBLOCK) == 0),
                      "Failed to set socket to non-blocking\n");
        ck_assert_msg((fcntl(sv[1], F_SETFL, O_NONBLOCK) == 0),
                      "Failed to set socket to non-blocking\n");
        *client = sv[0];
        *server = sv[1];
}

Suite *config_suite(void)
{
        // A suite is comprised of test cases, defined below
        Suite *s = suite_create("daemon");

        // Individual unit tests are added to "test cases"
        TCase *t = tcase_create("daemon");
        tcase_add_test(t, check_add_del_poll_fd);
        tcase_add_test(t, check_daemon_is_initialized);
        tcase_add_test(t, check_add_remove_client);

        suite_add_tcase(s, t);

        return s;
}

int main(void)
{
        Suite *s;
        SRunner *sr;

        s = config_suite();
        sr = srunner_create(s);

        // Use the TAP driver for now, so that each
        // unit test will PASS/FAIL in the log output.
        srunner_set_log(sr, NULL);
        srunner_set_tap(sr, "-");

        srunner_run_all(sr, CK_SILENT);
        // failed = srunner_ntests_failed(sr);
        srunner_free(sr);

        // if you want the TAP driver to report a hard error based
        // on certain conditions (e.g. number of failed tests, etc.),
        // return non-zero here instead.
        return 0;
}

/* vi: set ts=8 sw=8 sts=4 et tw=80 cino=(0: */
