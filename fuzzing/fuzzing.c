/*
 * This program is part of the Clear Linux Project
 *
 * Copyright 2025 Intel Corporation
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

#include <stdio.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <unistd.h>
#include <string.h>

#include "configuration.h"
#include "telemetry.h"
#include "telemdaemon.h"
#include "common.h"

void fuzz_process_record(TelemDaemon *daemon, client *cl);

void fuzz(const uint8_t *record, size_t record_size) {

    TelemDaemon daemon;
    client cl;
    char *machine_id = "abcde";
    bool processed;

    daemon.machine_id_override = malloc(strlen(machine_id) + 1);
    strcpy(daemon.machine_id_override, machine_id);

    cl.buf = (uint8_t *) record;
    cl.size = record_size;

    fuzz_process_record(&daemon, &cl);

    free(daemon.machine_id_override);
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    fuzz(Data, Size);
    return 0;
}

/* vi: set ts=8 sw=8 sts=4 et tw=80 cino=(0: */
