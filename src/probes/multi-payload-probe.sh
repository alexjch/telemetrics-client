#!/bin/sh

MAX_PAYLOAD_LEN_BYTES=8000
TELEM_CLI=.libs/telem-record-gen

submit_chunk() {
	read payload
	echo $payload
#	$TELEM_CLI --payload $1
}


main() {
	local LONG_FILE=$1
	# check that file exists
  	cat ${LONG_FILE} | xz -c | base64 -- | fold "-w${MAX_PAYLOAD_LEN_BYTES}" | submit_chunk
}


main $@

# vi: set ts=8 sw=8 sts=4 et tw=80 cino=(0:
