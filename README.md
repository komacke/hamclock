# HamClock Client

[![C++11 web](https://img.shields.io/github/actions/workflow/status/openhamclock/hamclock/compile-web.yml?branch=main&label=C%2B%2B11%20web&logo=cplusplus&style=flat)](https://github.com/openhamclock/hamclock/actions/workflows/compile-web.yml) [![C++11 Pi](https://img.shields.io/github/actions/workflow/status/openhamclock/hamclock/compile-fb0.yml?branch=main&label=C%2B%2B11%20Pi&logo=raspberrypi&style=flat)](https://github.com/openhamclock/hamclock/actions/workflows/compile-fb0.yml)

This repository is the primary source for ongoing maintenance of the HamClock Client,
aka the HamClock "frontend", and often just referred to as "HamClock".

It is intended as a reference implementation for use with backend servers
that are compatible with the original Clear Sky Institute service and the evolving backend standards
being developed by the [Open Hamclock Standards](https://github.com/openhamclock/hamclock-standards) project.

See [doc](./doc/) for all the information and documentation related to installing, using, and developing HamClock.

The HamClock Client was originally created by Clear Sky Institute, and made available under an [MIT License](./LICENSE).
This repository was started with the source code for Clear Sky Institute's HamClock v4.22, the final version they created.

An archive of historical HamClock Client releases up to 4.22 is available at <https://github.com/openhamclock/hamclock-client-archive>.

## Contributing

Bug reports and pull requests are welcome on GitHub at
<https://github.com/openhamclock/hamclock/issues>

## Related

* [HamClock Standards](https://github.com/openhamclock/hamclock-standards) - specifications source
* [HamClock Client](https://github.com/openhamclock/hamclock) - reference frontend implementation source
    * includes Raspberry Pi/Debian and Docker installers
    * [HamClockLauncher](https://github.com/huberthickman/HamClockLauncher) - macOS frontend installer/launcher source
    * also many appliances available for sale, with HamClock pre-installed and automatically maintained
* Open Hamclock Backend project
    * <https://ohb.works/> - main site
    * <https://github.com/komacke/open-hamclock-backend> - source
* [hamclock.com](https://hamclock.com) backend
