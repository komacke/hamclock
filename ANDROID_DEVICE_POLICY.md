# Privacy Policy for HamClock for Android

**Last Updated:** August 24, 2026

This privacy policy explains how **HamClock for Android** ("the App"), developed and maintained by the Open HamClock community (https://github.com/openhamclock/hamclock), handles your information and device permissions.

---

## 1. Overview
HamClock is an open-source, non-commercial utility application designed for amateur radio operators. We believe in strict user privacy:
* **No Advertisements:** The App contains no advertising networks or promotional trackers.
* **No Telemetry or Tracking:** The App contains no analytics, user tracking, or behavioral profiling SDKs.
* **No Personal Data Collection:** We do not collect, sell, lease, or distribute your personal information.

---

## 2. Device Permissions and Usage

The App requests the following Android device permissions strictly to provide core application functionality:

### Location (`ACCESS_FINE_LOCATION` and `ACCESS_COARSE_LOCATION`)
* **Purpose:** If granted, the App reads your device's approximate or precise location solely to determine your initial station coordinates (Latitude, Longitude, and Maidenhead Grid Square).
* **Handling:** Location data is processed entirely on your local device. It is never transmitted to developers, analytics providers, or third parties.
* **Optional:** Location permission is completely optional. If denied, you can manually enter your station coordinates (QTH) in the HamClock setup menu.

### Internet (`INTERNET` and `ACCESS_NETWORK_STATE`)
* **Purpose:** Required to connect to public backend servers (such as `ohb.hamclock.app` or custom user-configured servers) to download live space weather indices, solar flux data, VOACAP propagation models, satellite orbital elements (TLEs), weather information, and DX cluster spots.
* **Handling:** Network requests are made strictly to retrieve public data needed for the clock displays.

### Keep Screen On (`WAKE_LOCK`)
* **Purpose:** Keeps the device display active continuously when used as a desktop shack clock.

---

## 3. Data Storage
* All settings, callsign configurations, screen profiles, and cached map/solar data are stored locally in the App's private internal storage.
* No local data or preferences are sent to external databases or servers without explicit user action (e.g., custom REST API commands configured by the user).

---

## 4. Third-Party Services & External Links
* The App connects to public amateur radio data sources (e.g., NOAA/SWPC space weather feeds, VOACAP servers, DX cluster nodes). These connections only receive the data feeds required for display.
* No third-party commercial analytics, crash reporters, or advertising SDKs are embedded in the App.

---

## 5. Children's Privacy
The App is a technical utility tool designed for licensed amateur radio operators and space weather enthusiasts. It does not knowingly collect or solicit any personal information from children.

---

## 6. Open Source & Contact
HamClock for Android is open-source software distributed under the project's open-source license.

For questions, issues, or contributions regarding this policy or the software, please visit our repository:
* **GitHub:** [https://github.com/openhamclock/hamclock](https://github.com/openhamclock/hamclock)
* **Issue Tracker:** [https://github.com/openhamclock/hamclock/issues](https://github.com/openhamclock/hamclock/issues)
