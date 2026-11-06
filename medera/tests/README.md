# MedEra tests

## Ranking unit tests

From the `medera` project folder, run:

```bat
tests\run_tests.bat
```

This target builds `ranking_tests.cpp` with `src/ranking.cpp`. It covers distance calculations, specialty and location matching, bed and ambulance filters, rating and distance sorting, result limits, stale index entries, and optional patient location. It does not include the HTTP server, so it does not need Winsock or a running MedEra process.

## Security utility tests

Run:

```bat
tests\run_security_tests.bat
```

This target tests secure random token formatting, PBKDF2 password hashing, salt validation, and hash comparisons. It links with Windows BCrypt and does not need the HTTP server.

## API regression checks

Start MedEra in one terminal, then run the tests in a second. Both use port 8080 by default. Pass a different port to the runner if you started MedEra elsewhere:

```bat
tests\run_regression_tests.bat
```

The client source is `tests\api_regression_tests.cpp` and the runner links it with `ws2_32`. The checks cover health, CAPTCHA, and search endpoints; reject malformed or duplicate query data, invalid coordinates, invalid CAPTCHA registration, unknown tracking codes, and unauthenticated staff access; and verify that cross-origin submissions and unsupported methods are blocked. They do not create records.
