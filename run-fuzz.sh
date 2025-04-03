#!/bin/bash
fuzzing/fuzzing -max_total_time=43200  2>&1| grep -v process_record
