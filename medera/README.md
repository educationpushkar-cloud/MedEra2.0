# MedEra

MedEra is a local hospital search and care-request application. It uses a C++17 server on Windows and a browser interface built with HTML, CSS, and JavaScript.

## Build and run

1. Install MinGW-w64 UCRT64 and make `g++` available in `PATH`.
2. Open PowerShell in this folder.
3. Set the staff access code for this server session. Keep the value private and out of Git.

   ```powershell
   $env:MEDERA_COMMISSION_CODE = 'replace-with-your-local-code'
   ```

4. Build and start MedEra:

   ```powershell
   .\build.bat
   .\medera.exe 8080
   ```

5. Open <http://127.0.0.1:8080>. Keep the server window open while using the app.

If `MEDERA_COMMISSION_CODE` is not set, patient search remains available, while hospital registration, staff sign-in, and password reset are disabled. The configured code is a local access gate; it does not verify a hospital against an official licensing registry.

## Tests

Run the ranking unit tests without starting MedEra:

```bat
tests\run_tests.bat
```

The suite covers distance, specialty and location matching, availability filters, ranking modes, top-K limits, and index edge cases.

Run the password and secure-random utility tests with:

```bat
tests\run_security_tests.bat
```

For API regression checks, start MedEra first. The test runner defaults to the app's normal port, 8080:

```bat
tests\run_regression_tests.bat
```

The API test client uses Winsock and its runner links `ws2_32`. The API checks do not create records. See [tests/README.md](tests/README.md) for details.

## Code layout

- `src/main.cpp`: HTTP server, request validation, API routes, and local file persistence.
- `src/ranking.cpp` and `include/medera/ranking.hpp`: hospital filters, distance calculation, and priority-queue ranking.
- `src/security.cpp` and `include/medera/security.hpp`: Windows BCrypt random values and password hashing.
- `include/medera/models.hpp`: hospital, account, appointment, rating, emergency, and HTTP request records.
- `public/`: browser UI, styles, interaction logic, and the local QR encoder.
- `tests/`: ranking, security utility, and API checks with Windows batch runners.
- `data/`: local records protected with Windows DPAPI for the Windows account running MedEra.

The server binds to `127.0.0.1` on the local computer. It does not connect to hospital scheduling systems, send external notifications, contact emergency services, or automatically dispatch ambulances. Do not use this academic build to store real patient information.
