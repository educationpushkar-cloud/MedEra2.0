# MedEra

3. Open PowerShell in the `medera` folder (the folder that contains `build.bat`), then run `.\build.bat medera-reviews.exe`.
4. Start the corrected build from that same folder with `.\medera-reviews.exe 8097`.
The interface is inspired by the clear location-first search and browsing pattern in the reference website. It uses original MedEra branding and CSS-built hospital illustrations.

## Run locally on Windows

1. Install MinGW-w64 UCRT64 and make `g++` available in `PATH`.
2. Stop any older MedEra server by pressing **Ctrl+C** in its PowerShell window.
3. Open PowerShell in the `medera` folder (the folder that contains `build.bat`), then run `.\build.bat medera-reviews.exe`.
4. Start the corrected build from that same folder with `.\medera-reviews.exe 8097`.
5. Open [http://127.0.0.1:8097](http://127.0.0.1:8097) in your browser. Leave the server window running.

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
│   ├── qr.js            # In-browser QR encoder; appointment links stay local
│   └── styles.css       # Responsive MedEra visual design
├── src/
│   ├── main.cpp         # HTTP server, persistence, and application/API workflows
│   ├── ranking.cpp      # Haversine distance and top-K hospital ranking
│   └── security.cpp     # BCrypt password hashing and secure token generation
└── README.md
```

## Features

- Search by city, locality, and specialty, with filters for open beds and emergency-ready listings.
- Ask for browser location only after a patient chooses **Use my location**. Coordinates rank facilities and show approximate hospital positions with marker selection, drag-to-pan, zoom in/out, and fit-to-locations controls. This local map does not show streets; use a hospital's directions link for road navigation.
- See hospital-reported bed and ambulance availability, ratings, contact links, and facility directions when coordinates are listed. Each hospital profile includes its overall rating and patient comments.
- Submit and track appointment requests; hospital staff can confirm, decline, or complete them. A completed visit can receive one rating.
- Each submitted appointment receives a QR image that opens MedEra's tracker with its code prefilled. Patients can screenshot the confirmation or download the QR image. QR generation runs in the browser and does not send codes to an outside QR service.
- Patients enter their appointment code in the tracking section; marking a visit complete unlocks the review form but does not submit a review automatically. After submission, the tracker links directly to the published hospital profile. Reviews are limited to one per completed appointment.
- Hospital registration and profile setup offer an explicit **Use current location** button to fill coordinates, with manual latitude/longitude fields kept available.
- Create an emergency coordination request. The backend considers hospitals reporting both an available bed and ambulance, and provides a private tracking code with a status timeline. Staff can accept or decline, validate a phone callback, assign an ambulance, and update dispatch milestones.
- Hospital staff can register, sign in, edit facility and capacity details, and manage appointment and emergency inboxes.
- Hospital staff can reset a password with the registered email, shared Health Commission code, CAPTCHA, and a new password. Passwords are one-way hashed and cannot be retrieved in plaintext.
- Hospital staff can hide or restore their public listing. Hiding removes it from patient search while retaining the account, appointments, and reviews.
- The page shows the current date and time in India and rotates general health reminders with links to CDC guidance.
- Hospital registration and sign-in require the shared commission access code `admin222`.
- Staff sign-in and hospital registration also use a one-time, five-minute CAPTCHA arithmetic challenge validated by the C++ server.
- Persist records in `data/`; Windows DPAPI encrypts the database files for the Windows account running the server.

## Location ranking and data structures

- `std::unordered_map` indexes hospital IDs by normalized city and city/locality for quick candidate lookup.
- A bounded `std::priority_queue` retains the best matches; `std::sort` orders the final top results.
- Haversine distance ranks facilities when a patient has chosen to share their browser location.
- Hospital and request records are represented by C++ structs in `include/medera/models.hpp`.

## Important operating limits

The shared code `admin222` is only a temporary access gate. It is not checked against a Health Commission registry, does not prove that a hospital is licensed, and anyone who learns the shared code can pass it. Replace it with individual credentials and a trusted licensing-registry check before representing accounts as verified or opening registration to the public.

The CAPTCHA is a basic server-checked arithmetic challenge, not a sophisticated bot-prevention or rate-limiting service. A QR created by the local build uses its local address; a phone cannot open a `127.0.0.1` link. For phone scanning, host MedEra at a network-accessible address first.

This version runs locally on one Windows computer and supports saved records and manual hospital workflows. It does not connect to hospital scheduling systems, verify facility identity, send SMS/email notifications, contact emergency services, or dispatch ambulances. Staff inboxes refresh while the staff member is signed in, but a hospital must respond manually. Emergency requests are not monitored by MedEra; for immediate danger, the page instructs people to contact local emergency services first.

Before offering this to the public or exposing it beyond the local computer, add an authenticated deployment, TLS, access controls, verified hospital onboarding, backups, and an operational notification and emergency-response process. Hospital-submitted capacity and reviews should be independently checked before people rely on them.

## GitHub

The source tree is organized for a GitHub repository. Compiled executables and local database files are excluded by `.gitignore`; commit the C++ source, headers, frontend, build script, and README. To publish it, initialize or clone the intended repository in this folder and push to the repository URL you choose.





