#include <winsock2.h>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
const std::string host = "127.0.0.1";
int port = 8080;
int passed = 0;
int failed = 0;

struct Response {
    int status = 0;
    std::string body;
};
// Sends a complete HTTP request over a socket.
// send() may transmit only part of the data, so this loop continues
// until every character in 'data' has been sent.
bool sendAll(SOCKET socket, const std::string& data)
bool sendAll(SOCKET socket, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int count = send(socket, data.data() + sent,
                               static_cast<int>(data.size() - sent), 0);
        if (count <= 0) return false;
        sent += static_cast<std::size_t>(count);
    }
    return true;
}
// Connects to the local server, builds an HTTP GET/POST request,
// sends it, receives the full response, and extracts:
// - HTTP status code, such as 200 or 404
// - response body, usually JSON
Response request(const std::string& method, const std::string& path,
                 const std::string& body = "", const std::string& origin = "")
Response request(const std::string& method, const std::string& path,
                 const std::string& body = "", const std::string& origin = "") {
    Response response;
    const SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) return response;

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(static_cast<unsigned short>(port));
    server.sin_addr.s_addr = inet_addr(host.c_str());
    if (connect(socketHandle, reinterpret_cast<sockaddr*>(&server), sizeof(server)) == SOCKET_ERROR) {
        closesocket(socketHandle);
        return response;
    }

    std::string message = method + " " + path + " HTTP/1.1\r\n";
    message += "Host: " + host + "\r\nConnection: close\r\n";
    if (!origin.empty()) message += "Origin: " + origin + "\r\n";
    if (method == "POST") {
        message += "Content-Type: application/x-www-form-urlencoded\r\n";
        message += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    message += "\r\n" + body;

    if (!sendAll(socketHandle, message)) {
        closesocket(socketHandle);
        return response;
    }

    std::string raw;
    char buffer[4096];
    int count = 0;
    while ((count = recv(socketHandle, buffer, sizeof(buffer), 0)) > 0) {
        raw.append(buffer, static_cast<std::size_t>(count));
    }
    closesocket(socketHandle);

    const std::size_t headerEnd = raw.find("\r\n\r\n");
    const std::size_t firstSpace = raw.find(' ');
    if (headerEnd == std::string::npos || firstSpace == std::string::npos || firstSpace > headerEnd) {
        return response;
    }
    try {
        response.status = std::stoi(raw.substr(firstSpace + 1, 3));
    } catch (...) {
        return response;
    }
    response.body = raw.substr(headerEnd + 4);
    return response;
}
// Checks whether 'value' occurs anywhere inside 'text'.
// Used to check for expected JSON fields or error messages.
bool contains(const std::string& text, const std::string& value)
bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}
// Extracts a simple string field from JSON.
// Example: jsonStringField(json, "id", captchaId)
// gets the value of {"id":"..."} and saves it in captchaId.
bool jsonStringField(const std::string& json, const std::string& name,
                     std::string& value)
bool jsonStringField(const std::string& json, const std::string& name, std::string& value) {
    const std::string marker = "\"" + name + "\":\"";
    const std::size_t start = json.find(marker);
    if (start == std::string::npos) return false;
    const std::size_t valueStart = start + marker.size();
    const std::size_t end = json.find('"', valueStart);
    if (end == std::string::npos) return false;
    value = json.substr(valueStart, end - valueStart);
    return true;
}
// Records and displays each test result.
// Increments either the passed or failed counter.
void check(const std::string& name, bool condition)
void check(const std::string& name, bool condition) {
    if (condition) {
        std::cout << "PASS  " << name << '\n';
        ++passed;
    } else {
        std::cerr << "FAIL  " << name << '\n';
        ++failed;
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        try {
            const long parsedPort = std::stol(argv[1]);
            if (parsedPort < 1 || parsedPort > 65535) throw std::out_of_range("port");
            port = static_cast<int>(parsedPort);
        } catch (...) {
            std::cerr << "Usage: api_regression_tests.exe [port 1-65535]\n";
            return 2;
        }
    }

    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        std::cerr << "Winsock initialization failed.\n";
        return 2;
    }

    std::cout << "MedEra API regression checks at " << host << ':' << port << "\n\n";

    const Response health = request("GET", "/api/health");
    check("Health endpoint responds", health.status == 200 && contains(health.body, "\"ok\":true"));

    const Response captcha = request("GET", "/api/captcha");
    check("CAPTCHA endpoint returns an ID and question",
          captcha.status == 200 && contains(captcha.body, "\"id\"") && contains(captcha.body, "\"question\""));

    const Response hospitals = request("GET", "/api/hospitals");
    check("Hospital search endpoint returns results",
          hospitals.status == 200 && contains(hospitals.body, "\"results\""));

    const Response invalidLocation = request("GET", "/api/hospitals?lat=91&lon=0");
    check("Hospital search rejects out-of-range coordinates",
          contains(invalidLocation.body, "\"error\""));

    const Response malformedEncoding = request("GET", "/api/hospitals?city=%GG");
    check("Malformed percent-encoded query is rejected", malformedEncoding.status == 400);

    const Response duplicateParameter = request("GET", "/api/hospitals?city=North&city=South");
    check("Duplicate query parameters are rejected", duplicateParameter.status == 400);

    const Response missingCaptcha = request("POST", "/api/register", "hospitalName=RegressionTestHospital");
    check("Hospital registration without CAPTCHA is rejected",
          contains(missingCaptcha.body, "\"error\""));

    const Response invalidCaptcha = request(
        "POST", "/api/register",
        "hospitalName=RegressionTestHospital&captchaId=invalid-test-id&captchaAnswer=2");
    check("Hospital registration with an invalid CAPTCHA is rejected",
          contains(invalidCaptcha.body, "\"error\""));

    std::string captchaId, captchaQuestion;
    const bool parsedCaptcha = jsonStringField(captcha.body, "id", captchaId) &&
                               jsonStringField(captcha.body, "question", captchaQuestion);
    check("CAPTCHA response contains parseable challenge values", parsedCaptcha);
    if (parsedCaptcha) {
        const std::string prefix = "Solve this check: ";
        int left = 0, right = 0;
        std::string operation;
        bool parsedExpression = false;
        if (captchaQuestion.rfind(prefix, 0) == 0) {
            std::istringstream expression(captchaQuestion.substr(prefix.size()));
            parsedExpression = static_cast<bool>(expression >> left >> operation >> right);
        }
        check("CAPTCHA question contains a solvable arithmetic expression", parsedExpression);
        if (parsedExpression && (operation == "+" || operation == "−")) {
            const int answer = operation == "+" ? left + right : left - right;
            const std::string registration = "name=Regression+Test&city=Test+City&area=Test+Area&email=test%40example.org&phone=1234567890&password=RegressionPassword123&captchaId=" + captchaId + "&captchaAnswer=" + std::to_string(answer);
            const Response missingCommissionCode = request("POST", "/api/register", registration);
            check("Registration with a valid CAPTCHA but no access code is rejected",
                  contains(missingCommissionCode.body, "\"error\"") &&
                  (contains(missingCommissionCode.body, "code") || contains(missingCommissionCode.body, "CODE")));
            const Response reusedCaptcha = request("POST", "/api/register", registration);
            check("A CAPTCHA challenge cannot be reused",
                  contains(reusedCaptcha.body, "\"error\"") && contains(reusedCaptcha.body, "CAPTCHA"));
        } else if (parsedExpression) {
            check("CAPTCHA operator is supported by the test client", false);
        }
    }

    const Response unknownAppointment = request(
        "GET", "/api/appointments/status?code=not-a-real-appointment-code");
    check("Unknown appointment tracking code is rejected",
          contains(unknownAppointment.body, "\"error\"") || contains(unknownAppointment.body, "not found"));

    const Response unknownEmergency = request(
        "GET", "/api/emergencies/status?code=not-a-real-emergency-code");
    check("Unknown emergency tracking code is rejected",
          contains(unknownEmergency.body, "\"error\"") || contains(unknownEmergency.body, "not found"));

    const Response unauthorizedStaff = request("GET", "/api/staff/overview");
    check("Staff workspace requires an authenticated session",
          contains(unauthorizedStaff.body, "\"error\""));

    const Response crossOrigin = request("POST", "/api/appointments", "", "http://example.invalid");
    check("Cross-origin form submission is blocked", crossOrigin.status == 403);

    const Response unsupportedMethod = request("DELETE", "/api/hospitals");
    check("Unsupported API methods return HTTP 405", unsupportedMethod.status == 405);

    WSACleanup();
    std::cout << "\nPassed: " << passed << "  Failed: " << failed << '\n';
    return failed == 0 ? 0 : 1;
}
