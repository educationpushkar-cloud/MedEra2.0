
"""
MedEra appointment CAPTCHA regression tests.

Requirements:
    Python 3.10+ (standard library only)

Run:
    python tests/test_appointment_captcha.py

The MedEra server must be running locally on port 8097.
Appointment security tests require a listed hospital with a department.
"""

import json
import unittest
from datetime import date, timedelta
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

BASE_URL = "http://127.0.0.1:8097"


def api_get(path):
    """Send a GET request and return its decoded JSON response."""
    with urlopen(BASE_URL + path, timeout=5) as response:
        return json.loads(response.read().decode("utf-8"))


def api_post(path, fields):
    """Send URL-encoded form data and return its decoded JSON response."""
    data = urlencode(fields).encode("utf-8")
    request = Request(
        BASE_URL + path,
        data=data,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        method="POST",
    )
    with urlopen(request, timeout=5) as response:
        return json.loads(response.read().decode("utf-8"))


class MedEraRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        """Load an existing hospital for appointment API tests."""
        health = api_get("/api/health")
        if health.get("ok") is not True:
            raise RuntimeError("MedEra health endpoint did not report OK.")

        result = api_get("/api/hospitals")
        cls.hospital = None

        for hospital in result.get("results", []):
            departments = hospital.get("departments", [])
            if hospital.get("listed") and departments:
                cls.hospital = hospital
                cls.department = departments[0]
                break

    def test_health_endpoint(self):
        """The server should report that it is running."""
        result = api_get("/api/health")
        self.assertTrue(result.get("ok"))

    def test_captcha_returns_id_and_question(self):
        """A CAPTCHA challenge should include an ID and a question."""
        result = api_get("/api/captcha")
        self.assertTrue(result.get("id"))
        self.assertTrue(result.get("question"))

    def test_unknown_appointment_code_is_rejected(self):
        """An unknown tracking code should return an error."""
        result = api_get(
            "/api/appointments/status?code=invalid-test-code"
        )
        self.assertIn("error", result)

    def appointment_fields(self):
        """Build valid appointment data for a known hospital."""
        return {
            "hospitalId": self.hospital["id"],
            "patientName": "Regression Test Patient",
            "phone": "9876543210",
            "email": "regression@example.com",
            "department": self.department,
            "doctor": "",
            "date": (date.today() + timedelta(days=2)).isoformat(),
            "time": "11:00",
            "notes": "Automated regression test",
        }

    def test_missing_captcha_is_rejected(self):
        """Appointment submission without CAPTCHA must fail."""
        if not self.hospital:
            self.skipTest(
                "Register or configure a listed test hospital "
                "with at least one department."
            )

        result = api_post(
            "/api/appointments", self.appointment_fields()
        )
        self.assertIn("error", result)

    def test_invalid_captcha_is_rejected(self):
        """An unknown CAPTCHA ID must not authorize an appointment."""
        if not self.hospital:
            self.skipTest(
                "Register or configure a listed test hospital "
                "with at least one department."
            )

        fields = self.appointment_fields()
        fields["captchaId"] = "invalid-test-captcha-id"
        fields["captchaAnswer"] = "1"

        result = api_post("/api/appointments", fields)
        self.assertIn("error", result)


if __name__ == "__main__":
    try:
        unittest.main(verbosity=2)
    except (URLError, HTTPError, TimeoutError, OSError) as exc:
        print(f"Could not reach MedEra at {BASE_URL}: {exc}")
        print("Start the MedEra server and run the tests again.")
