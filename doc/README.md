# HamClock Client Documentation

## Using the HamClock Client

For information on how to install and use the HamClock client, see:

* [Running the HamClock Client with Docker](../docker/)
* [Running the HamClock Client on Raspberry Pi/Debian](../debian/)
* [HamClock Client Command-line Interface](./cli/)
* [HamClock Client User Manual (PDF)](./HamClockUserGuide.pdf)
* [User Contributions](../hamclock-contrib/README.md)

## Development

See the [Open Hamclock Standards](https://github.com/openhamclock/hamclock-standards) project
for details of the backend APIs that all "HamClock Compatible" clients and backend servers must support. The HamClock Client uses these APIs for all external data access.

The HamClock Client has a number of public interfaces and file formats that are defined in the following documents:

* [HamClock Client Command-line Interface](./cli/)
* [HamClock Client EEPROM File Layout](./eeprom/)
* [HamClock Client RESTful API](./client-api/)

## HamClock System Architecture

The following diagram illustrates the components of the classic HamClock architecture as developed by CSI.

       ┌────────────────────────────────────────┐
       │               HamClock                 │
       │     (Frontend/Client Installation)     │───O RW Web Interface
       │                              ┌─────┐   │
       │   ┌──────────────────────┐   │ GUI │   │───O RO Web Interface
       │   │ ~/.hamclock          │   └─────┘   │
       │   │ (settings and cache) │   ┌─────┐   │───O RESTful API Web Interface
       │   │ EEPROM file          │   │ CLI │   │   ┌────────────┐
       │   └──────────────────────┘   └─────┘   │───│ GPIO (RPi) │
       └────────────────────┬───────────────────┘   └────────────┘
                            │
                            │ HamClock Backend API
                            │
                            ▼
           ┌──────────────────────────────────┐
           │         HamClock Backend         │
           │    (3rd party or self-hosted)    │
           └────────────────┬─────────────────┘
                            │
                            │ External APIs
                            │
                            ▼
      ┌─────────────────────────────────────────────┐
      │           3rd party data services           │
      │ (e.g. POTA park locations; solar flux data) │
      └─────────────────────────────────────────────┘

## About the HamClock Project(s)

For a general starting point to find out more about the ongoing development efforts related to HamClock, see:

* [Open Hamclock Backend project](https://ohb.works/)
* [hamclock.com](https://hamclock.com) backend

For an historical appreciation of the HamClock project, see:

* Original [October 2017 QST article](https://web.archive.org/web/20251130070310/https://clearskyinstitute.com/ham/HamClock/QST-HamClock.pdf)
* <https://clearskyinstitute.com/ham/HamClock/> - the original home
* <https://web.archive.org/web/20260123074655/https://www.clearskyinstitute.com/ham/HamClock/> a good archive.org entry point into the history of the HamClock website
