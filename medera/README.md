# MedEra

MedEra is a C++17 hospital directory and care-request web app. Patients can search hospital-managed listings by city, locality, specialty, and reported availability; use browser location to rank nearby hospitals; request appointments; and submit a tracked emergency-coordination request. Hospital staff have a sign-in workspace for listing details, availability, and request inboxes.

The interface is inspired by the clear location-first search and browsing pattern in the reference website. It uses original MedEra branding and CSS-built hospital illustrations.

## Run locally on Windows

1. Install MinGW-w64 UCRT64 and make `g++` available in `PATH`.
2. In this folder, run `build.bat`. It creates `medera.exe`. If that file is running and locked, use `build.bat medera-updated.exe` to compile to a second filename.
3. Start the app with `medera.exe` (or `medera.exe 8090` to choose another port).
4. Open [http://127.0.0.1:8080](http://127.0.0.1:8080) in your browser.

Keep the server console open while using the app. It binds only to `127.0.0.1`; other devices cannot connect to this local build. The executable loads `public/` and stores records in `data/` beside itself, even if you launch it while PowerShell is in another folder. The `data/` directory is created when the app first runs. The directory intentionally starts empty and does not contain fictional hospitals or availability data.

## Project files

```text
medera/
├── build.bat
├── data/.gitkeep
├── include/medera/
│   ├── models.hpp       # Shared hospital, request, and workflow records
│   ├── ranking.hpp      # Location-index and ranking interface
│   └── security.hpp     # Cryptographic helper interface
├── public/
│   ├── app.js           # Search, map, forms, tracking, and staff interactions
│   ├── index.html       # Accessible page structure and dialogs
│   └── styles.css       # Responsive MedEra visual design
├── src/
│   ├── main.cpp         # HTTP server, persistence, and application/API workflows
│   ├── ranking.cpp      # Haversine distance and top-K hospital ranking
│   └── security.cpp     # BCrypt password hashing and secure token generation
└── README.md
```

## Features

- Search by city, locality, and specialty, with filters for open beds and emergency-ready listings.
- Ask for browser location only after a patient chooses **Use my location**. Coordinates are used locally to rank facilities and show relative positions. The map is an approximate schematic, not turn-by-turn navigation.
- See hospital-reported bed and ambulance availability, ratings, contact links, and facility directions when coordinates are listed.
- Submit and track appointment requests; hospital staff can confirm, decline, or complete them. A completed visit can receive one rating.
- Patients enter their appointment code in the tracking section; the review form appears after hospital staff mark the visit complete. Reviews are limited to one per completed appointment.
- Hospital registration and profile setup offer an explicit **Use current location** button to fill coordinates, with manual latitude/longitude fields kept available.
- Create an emergency coordination request. The backend considers hospitals reporting both an available bed and ambulance, and provides a private tracking code with a status timeline. Staff can accept or decline, validate a phone callback, assign an ambulance, and update dispatch milestones.
- Hospital staff can register, sign in, edit facility and capacity details, and manage appointment and emergency inboxes.
- Persist records in `data/`; Windows DPAPI encrypts the database files for the Windows account running the server.

## Location ranking and data structures

- `std::unordered_map` indexes hospital IDs by normalized city and city/locality for quick candidate lookup.
- A bounded `std::priority_queue` retains the best matches; `std::sort` orders the final top results.
- Haversine distance ranks facilities when a patient has chosen to share their browser location.
- Hospital and request records are represented by C++ structs in `include/medera/models.hpp`.

## Important operating limits

This version runs locally on one Windows computer and supports real saved records and manual hospital workflows. It does not connect to hospital scheduling systems, verify facility identity, send SMS/email notifications, contact emergency services, or dispatch ambulances. Staff inboxes refresh while the staff member is signed in, but a hospital must respond manually. Emergency requests are not monitored by MedEra; for immediate danger, the page instructs people to contact local emergency services first.

Before offering this to the public or exposing it beyond the local computer, add an authenticated deployment, TLS, access controls, verified hospital onboarding, backups, and an operational notification and emergency-response process. Hospital-submitted capacity and reviews should be independently checked before people rely on them.

## GitHub

The source tree is organized for a GitHub repository. `medera.exe` and local database files are excluded by `.gitignore`; commit the C++ source, headers, frontend, build script, and README. To publish it, initialize or clone the intended repository in this folder and push to the repository URL you choose.
