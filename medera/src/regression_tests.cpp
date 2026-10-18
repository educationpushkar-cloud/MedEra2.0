
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace std;

const string HOST = "127.0.0.1";
const int PORT = 8097;

struct Response {
    int status = 0;
    string body;
};

int passed = 0;
int failed = 0;
int skipped = 0;

// Send all bytes, handling partial socket writes.
bool sendAll(SOCKET sock, const string& data) {
    size_t sent = 0;

    while (sent < data.size()) {
        int n = send(
            sock,
            data.data() + sent,
            static_cast<int>(data.size() - sent),
            0
        );

        if (n <= 0)
            return false;

        sent += static_cast<size_t>(n);
    }

    return true;
}

// Send an HTTP request and read the response.
Response request(
    const string& method,
    const string& path,
    const string& body = ""
) {
    Response result;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (sock == INVALID_SOCKET)
        return result;

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = inet_addr(HOST.c_str());

    if (connect(
            sock,
            reinterpret_cast<sockaddr*>(&server),
            sizeof(server)
        ) == SOCKET_ERROR) {
        closesocket(sock);
        return result;
    }

    string http =
        method + " " + path + " HTTP/1.1\r\n"
        "Host: " + HOST + "\r\n"
        "Connection: close\r\n";

    if (method == "POST") {
        http +=
            "Content-Type: application/x-www-form-urlencoded\r\n";
        http += "Content-Length: " +
                to_string(body.size()) + "\r\n";
    }

    http += "\r\n";
    http += body;

    if (!sendAll(sock, http)) {
        closesocket(sock);
        return result;
    }

    string raw;
    char buffer[4096];
    int received;

    while ((received = recv(
                sock, buffer, sizeof(buffer), 0
            )) > 0) {
        raw.append(buffer, received);
    }

    closesocket(sock);

    size_t headerEnd = raw.find("\r\n\r\n");

    if (headerEnd == string::npos)
        return result;

    size_t firstSpace = raw.find(' ');

    if (firstSpace != string::npos &&
        firstSpace < headerEnd) {
        try {
            result.status =
                stoi(raw.substr(firstSpace + 1, 3));
        } catch (...) {
            result.status = 0;
        }
    }

    result.body = raw.substr(headerEnd + 4);
    return result;
}

// Check whether a string contains another string.
bool has(const string& text, const string& value) {
    return text.find(value) != string::npos;
}

// Extract a simple JSON string field.
string jsonString(
    const string& json,
    const string& key
) {
    string token = "\"" + key + "\"";

    size_t pos = json.find(token);

    if (pos == string::npos)
        return "";

    pos = json.find(':', pos + token.size());

    if (pos == string::npos)
        return "";

    pos = json.find('"', pos + 1);

    if (pos == string::npos)
        return "";

    size_t end = json.find('"', pos + 1);

    if (end == string::npos)
        return "";

    return json.substr(pos + 1, end - pos - 1);
}

// Encode values for application/x-www-form-urlencoded.
string urlEncode(const string& value) {
    ostringstream out;
    out << hex << uppercase;

    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' ||
            c == '.' || c == '*') {
            out << static_cast<char>(c);
        } else if (c == ' ') {
            out << '+';
        } else {
            out << '%'
                << setw(2) << setfill('0')
                << static_cast<int>(c);
        }
    }

    return out.str();
}

void check(const string& name, bool success) {
    if (success) {
        std::cout << "[PASS] " << name << '\n';
        ++passed;
    } else {
        std::cout << "[FAIL] " << name << '\n';
        ++failed;
    }
}

void skip(const string& name, const string& reason) {
    std::cout << "[SKIP] " << name
              << ": " << reason << '\n';
    ++skipped;
}

// Find a listed hospital and its first department.
bool findTestHospital(
    const string& json,
    string& hospitalId,
    string& department
) {
    size_t resultsPos = json.find("\"results\"");

    if (resultsPos == string::npos)
        return false;

    size_t arrayStart = json.find('[', resultsPos);

    if (arrayStart == string::npos)
        return false;

    size_t arrayEnd = json.find(']', arrayStart);

    if (arrayEnd == string::npos)
        return false;

    size_t pos = arrayStart + 1;

    while (pos < arrayEnd) {
        size_t objectStart = json.find('{', pos);

        if (objectStart == string::npos ||
            objectStart >= arrayEnd) {
            break;
        }

        // Hospital objects in this API contain nested arrays.
        // Find the next hospital by its "listed" field.
        size_t listedPos = json.find("\"listed\"", objectStart);

        if (listedPos == string::npos ||
            listedPos >= arrayEnd) {
            break;
        }

        size_t objectEnd = json.find('}', listedPos);

        if (objectEnd == string::npos ||
            objectEnd >= json.size()) {
            break;
        }

        string object = json.substr(
            objectStart, objectEnd - objectStart + 1
        );

        if (has(object, "\"listed\":true")) {
            hospitalId = jsonString(object, "id");

            size_t deptPos = object.find("\"departments\"");

            if (deptPos != string::npos) {
                size_t open = object.find('[', deptPos);
                size_t close = object.find(']', open);

                if (open != string::npos &&
                    close != string::npos) {
                    size_t quote = object.find('"', open + 1);

                    if (quote != string::npos &&
                        quote < close) {
                        size_t endQuote =
                            object.find('"', quote + 1);

                        if (endQuote != string::npos &&
                            endQuote < close) {
                            department = object.substr(
                                quote + 1,
                                endQuote - quote - 1
                            );
                        }
                    }
                }
            }

            if (!hospitalId.empty() &&
                !department.empty()) {
                return true;
            }
        }

        pos = listedPos + 8;
    }

    return false;
}

int main() {
    WSADATA wsa{};

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "Winsock initialization failed.\n";
        return 2;
    }

    std::cout << "Med Era C++ Regression Tests\n\n";

    // Test 1: Health endpoint.
    Response health = request("GET", "/api/health");

    check(
        "Health endpoint responds",
        health.status == 200 &&
        has(health.body, "\"ok\":true")
    );

    // Test 2: CAPTCHA endpoint.
    Response captcha = request("GET", "/api/captcha");

    check(
        "CAPTCHA returns ID and question",
        captcha.status == 200 &&
        has(captcha.body, "\"id\"") &&
        has(captcha.body, "\"question\"")
    );

    // Locate a real listed test hospital.
    Response listing = request("GET", "/api/hospitals");

    string hospitalId;
    string department;

    bool hospitalFound = findTestHospital(
        listing.body, hospitalId, department
    );

    if (!hospitalFound) {
        skip(
            "Missing CAPTCHA rejected",
            "No listed hospital with a department was found."
        );

        skip(
            "Invalid CAPTCHA ID rejected",
            "No listed hospital with a department was found."
        );
    } else {
        string form =
            "hospitalId=" + urlEncode(hospitalId) +
            "&patientName=RegressionTestPatient" +
            "&phone=9990001122" +
            "&email=regression%40example.test" +
            "&department=" + urlEncode(department) +
            "&doctor=" +
            "&date=2099-12-20" +
            "&time=10%3A30" +
            "&notes=Automated+test";

        // Test 3: Missing CAPTCHA must not create an appointment.
        Response missing = request(
            "POST", "/api/appointments", form
        );

        bool missingRejected =
            has(missing.body, "\"error\"") ||
            !has(missing.body, "\"appointment\"");

        check(
            "Appointment without CAPTCHA is rejected",
            missingRejected
        );

        // Test 4: Invalid CAPTCHA ID must not create an appointment.
        string invalidForm =
            form +
            "&captchaId=invalid-test-captcha-id"
            "&captchaAnswer=2";

        Response invalid = request(
            "POST", "/api/appointments", invalidForm
        );

        bool invalidRejected =
            has(invalid.body, "\"error\"") ||
            !has(invalid.body, "\"appointment\"");

        check(
            "Appointment with invalid CAPTCHA is rejected",
            invalidRejected
        );
    }

    // Test 5: Unknown tracking code.
    Response tracking = request(
        "GET",
        "/api/appointments/status?code=not-a-real-appointment-code"
    );

    check(
        "Unknown appointment tracking code is rejected",
        has(tracking.body, "\"error\"") ||
        has(tracking.body, "not found")
    );

    std::cout << "\n------ RESULTS ------\n";
    std::cout << "Passed:  " << passed << '\n';
    std::cout << "Failed:  " << failed << '\n';
    std::cout << "Skipped: " << skipped << '\n';

    WSACleanup();

    return failed == 0 ? 0 : 1;
}
