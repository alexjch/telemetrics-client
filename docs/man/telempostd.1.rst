==========
telempostd
==========

------------------------
Telemetry client service
------------------------

:Copyright: \(C) 2018 Intel Corporation, CC-BY-SA-3.0
:Manual section: 1


SYNOPSIS
========

``telempostd`` \<flags\>

``/etc/telemetrics/telemetrics.conf``

``/usr/share/defaults/telemetrics/telemetrics.conf``

``/etc/telemetrics/opt-in-static-machine-id``


DESCRIPTION
===========

The ``telempostd`` program delivers locally generated telemetry records to a remote
telemetry service. Telemetry data can be in any format, and is relayed as-is.


OPTIONS
=======

  * ``-f``, ``--config_file`` \[\<file\>\]:
    Configuration file. This overrides the other parameters.

  * ``-h``, ``--help``:
    Display this help message.

  * ``-V``, ``--version``:
    Print the program version.


FILES
=====

* ``/usr/share/defaults/telemetrics/telemetrics.conf``

    If no custom configuration file is found, ``telempostd`` uses the
    settings in this file.

* ``/etc/telemetrics/telemetrics.conf``

    Custom configuration file that ``telempostd`` reads. See ``telemetrics.conf``\(5).


EXIT STATUS
===========

0 when no errors occurred. A non-zero exit status indicates a failure occurred.


SEE ALSO
========

* ``telemetry``\(3)
* ``telemetrics.conf``\(5)
* https://github.com/clearlinux/telemetrics-client
* https://clearlinux.org/documentation/

