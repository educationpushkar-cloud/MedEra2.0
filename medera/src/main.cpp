#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <dpapi.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <queue>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "medera/models.hpp"
#include "medera/ranking.hpp"
#include "medera/security.hpp"

namespace fs = std::filesystem;
using Clock = std::chrono::system_clock;

static fs::path APP_DIR;
static fs::path DATA_DIR;
std::vector<Hospital> hospitals;
static std::vector<Account> accounts;
static std::vector<Appointment> appointments;
static std::vector<Rating> ratings;
static std::vector<Emergency> emergencies;
std::unordered_map<std::string, std::vector<std::string>> locationIndex;
std::unordered_map<std::string, std::vector<std::string>> cityIndex;
static std::unordered_map<std::string, std::string> sessions;
static std::mutex dataMutex;
static std::mutex sessionMutex;
static int serverPort = 8080;

using medera::constantTimeEqual;
using medera::makePasswordHash;
using medera::randomHex;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}
std::string nowIso() {
    const auto point = Clock::now();
    const std::time_t time = Clock::to_time_t(point);
    std::tm local{};
    localtime_s(&local, &time);
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%dT%H:%M:%S");
    return out.str();
}
std::string jsonString(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                else out << static_cast<char>(c);
        }
    }
    out << '"';
    return out.str();
}
std::string jsonNumber(double value) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream out; out << std::fixed << std::setprecision(6) << value; return out.str();
}
std::string urlDecode(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') result.push_back(' ');
        else if (value[i] == '%' && i + 2 < value.size()) {
            const auto hex = value.substr(i + 1, 2);
            try { result.push_back(static_cast<char>(std::stoul(hex, nullptr, 16))); i += 2; }
            catch (...) { result.push_back(value[i]); }
        } else result.push_back(value[i]);
    }
    return result;
}
std::string decodeStoredField(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            try { result.push_back(static_cast<char>(std::stoul(value.substr(i + 1, 2), nullptr, 16))); i += 2; }
            catch (...) { result.push_back(value[i]); }
        } else result.push_back(value[i]);
    }
    return result;
}
std::map<std::string, std::string> parseParams(const std::string& encoded) {
    std::map<std::string, std::string> result;
    std::size_t start = 0;
    while (start <= encoded.size()) {
        const auto end = encoded.find('&', start);
        const auto part = encoded.substr(start, end == std::string::npos ? end : end - start);
        const auto equal = part.find('=');
        if (!part.empty()) result[urlDecode(part.substr(0, equal))] = equal == std::string::npos ? "" : urlDecode(part.substr(equal + 1));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}
std::string param(const std::map<std::string, std::string>& values, const std::string& key) {
    const auto found = values.find(key); return found == values.end() ? "" : trim(found->second);
}
double numberParam(const std::map<std::string, std::string>& values, const std::string& key, double fallback = 0) {
    try { const auto text = param(values, key); if (text.empty()) return fallback; std::size_t used = 0; double n = std::stod(text, &used); return used == text.size() && std::isfinite(n) ? n : fallback; }
    catch (...) { return fallback; }
}
int intParam(const std::map<std::string, std::string>& values, const std::string& key, int fallback = 0) {
    const double value = numberParam(values, key, fallback);
    return value >= static_cast<double>(std::numeric_limits<int>::min()) && value <= static_cast<double>(std::numeric_limits<int>::max()) ? static_cast<int>(value) : fallback;
}
std::string encodeField(const std::string& value) {
    static const char* digits = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if (c == '%' || c == '\t' || c == '\r' || c == '\n') {
            result.push_back('%'); result.push_back(digits[c >> 4]); result.push_back(digits[c & 15]);
        } else result.push_back(static_cast<char>(c));
    }
    return result;
}
std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= line.size()) {
        const auto end = line.find('\t', start);
        result.push_back(decodeStoredField(line.substr(start, end == std::string::npos ? end : end - start)));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}
std::string encodeRecord(const std::vector<std::string>& fields) {
    std::string result;
    for (std::size_t i = 0; i < fields.size(); ++i) { if (i) result.push_back('\t'); result += encodeField(fields[i]); }
    return result;
}
std::string joinValues(const std::vector<std::string>& values, char separator = ',') {
    std::string result;
    for (std::size_t i = 0; i < values.size(); ++i) { if (i) result.push_back(separator); result += values[i]; }
    return result;
}
std::vector<std::string> splitValues(const std::string& text, char separator = ',') {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(separator, start);
        const auto value = trim(text.substr(start, end == std::string::npos ? end : end - start));
        if (!value.empty()) result.push_back(value);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}
std::string stringArray(const std::string& csv) {
    const auto values = splitValues(csv);
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) { if (i) out += ','; out += jsonString(values[i]); }
    return out + "]";
}
std::vector<std::string> readLines(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<std::string> result;
    if (!in) return result;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    static const std::string magic = "MEDERA1";
    if (content.rfind(magic, 0) == 0) {
        DATA_BLOB encrypted{static_cast<DWORD>(content.size() - magic.size()), reinterpret_cast<BYTE*>(content.data() + magic.size())};
        DATA_BLOB decrypted{};
        if (!CryptUnprotectData(&encrypted, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &decrypted))
            throw std::runtime_error("MedEra data was encrypted for a different Windows account or could not be decrypted.");
        content.assign(reinterpret_cast<const char*>(decrypted.pbData), decrypted.cbData);
        LocalFree(decrypted.pbData);
    }
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); if (!line.empty()) result.push_back(line); }
    return result;
}
void saveLines(const fs::path& path, const std::vector<std::string>& lines) {
    fs::create_directories(path.parent_path());
    const std::string temporary = path.string() + ".tmp";
    std::ostringstream serialized;
    for (const auto& line : lines) serialized << line << "\r\n";
    const std::string plain = serialized.str();
    DATA_BLOB input{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB protectedData{};
    if (!CryptProtectData(&input, L"MedEra local records", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &protectedData))
        throw std::runtime_error("Windows could not encrypt the local data store.");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) { LocalFree(protectedData.pbData); throw std::runtime_error("Unable to write data file: " + path.string()); }
        out.write("MEDERA1", 7);
        out.write(reinterpret_cast<const char*>(protectedData.pbData), protectedData.cbData);
        out.flush();
        if (!out) { LocalFree(protectedData.pbData); throw std::runtime_error("Unable to finish writing data file: " + path.string()); }
    }
    LocalFree(protectedData.pbData);
    if (!MoveFileExA(temporary.c_str(), path.string().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(temporary.c_str());
        throw std::runtime_error("Unable to update data file: " + path.string());
    }
}
std::string csvList(const std::string& value) {
    auto items = splitValues(value);
    for (auto& item : items) {
        if (item.find(';') != std::string::npos || item.find('\n') != std::string::npos) throw std::runtime_error("List entries cannot contain semicolons or line breaks.");
    }
    return joinValues(items);
}
bool validEmail(const std::string& email) {
    const auto at = email.find('@');
    return at > 0 && at != std::string::npos && email.find('.', at) != std::string::npos && email.find_first_of(" \t\r\n") == std::string::npos && email.size() <= 254;
}
bool validPhone(const std::string& phone) {
    int digits = 0;
    for (unsigned char c : phone) { if (std::isdigit(c)) ++digits; else if (c != '+' && c != '-' && c != '(' && c != ')' && c != ' ') return false; }
    return digits >= 7 && digits <= 15;
}
bool validDate(const std::string& value) {
    if (value.size()!=10 || value[4]!='-' || value[7]!='-') return false;
    for (std::size_t i=0;i<value.size();++i) if (i!=4&&i!=7&&!std::isdigit(static_cast<unsigned char>(value[i]))) return false;
    const int year=std::stoi(value.substr(0,4)), month=std::stoi(value.substr(5,2)), day=std::stoi(value.substr(8,2));
    if (year<2020 || month<1 || month>12) return false;
    static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap=(year%4==0&&year%100!=0)||year%400==0;
    const int maximum=days[month-1]+(month==2&&leap?1:0);
    if (day<1 || day>maximum) return false;
    const auto point=Clock::now(); const std::time_t timestamp=Clock::to_time_t(point); std::tm local{}; localtime_s(&local,&timestamp);
    std::ostringstream today; today<<std::put_time(&local,"%Y-%m-%d");
    return value>=today.str();
}
bool validTime(const std::string& value) {
    if (value.size()!=5 || value[2]!=':') return false;
    if (!std::isdigit(static_cast<unsigned char>(value[0]))||!std::isdigit(static_cast<unsigned char>(value[1]))||!std::isdigit(static_cast<unsigned char>(value[3]))||!std::isdigit(static_cast<unsigned char>(value[4]))) return false;
    const int hour=std::stoi(value.substr(0,2)), minute=std::stoi(value.substr(3,2));
    return hour>=0&&hour<=23&&minute>=0&&minute<=59;
}
bool validAppointmentDateTime(const std::string& date, const std::string& time) {
    if (!validDate(date)||!validTime(time)) return false;
    const auto current=nowIso();
    return date!=current.substr(0,10) || time>=current.substr(11,5);
}
bool validCoordinate(double latitude, double longitude) { return latitude >= -90 && latitude <= 90 && longitude >= -180 && longitude <= 180; }
std::string formBody(const Request& request) {
    const auto type = request.headers.find("content-type");
    if (type != request.headers.end() && type->second.find("application/x-www-form-urlencoded") == std::string::npos)
        throw std::runtime_error("Send form data as URL-encoded fields.");
    return request.body;
}
std::string safeError(const std::string& message) { return "{\"error\":" + jsonString(message) + "}"; }
std::string authToken(const Request& request) {
    const auto it = request.headers.find("authorization");
    if (it == request.headers.end() || it->second.rfind("Bearer ", 0) != 0) return {};
    return it->second.substr(7);
}
std::string authorizeHospital(const Request& request) {
    const auto token = authToken(request);
    if (token.empty()) return {};
    std::lock_guard<std::mutex> lock(sessionMutex);
    const auto it = sessions.find(token);
    return it == sessions.end() ? std::string{} : it->second;
}
Hospital* hospitalById(const std::string& id) {
    const auto it = std::find_if(hospitals.begin(), hospitals.end(), [&](const Hospital& h) { return h.id == id; });
    return it == hospitals.end() ? nullptr : &*it;
}
Appointment* appointmentById(const std::string& id) {
    const auto it = std::find_if(appointments.begin(), appointments.end(), [&](const Appointment& a) { return a.id == id; });
    return it == appointments.end() ? nullptr : &*it;
}
Emergency* emergencyById(const std::string& id) {
    const auto it = std::find_if(emergencies.begin(), emergencies.end(), [&](const Emergency& e) { return e.id == id; });
    return it == emergencies.end() ? nullptr : &*it;
}
void rebuildIndexes() {
    locationIndex.clear(); cityIndex.clear();
    for (const auto& hospital : hospitals) {
        locationIndex[lower(hospital.city) + "|" + lower(hospital.area)].push_back(hospital.id);
        cityIndex[lower(hospital.city)].push_back(hospital.id);
    }
}
void loadDatabase() {
    fs::create_directories(DATA_DIR);
    for (const auto& line : readLines(DATA_DIR / "hospitals.db")) {
        const auto f = splitFields(line); if (f.size() < 19) continue;
        Hospital h; h.id=f[0]; h.name=f[1]; h.city=f[2]; h.area=f[3]; h.address=f[4]; h.phone=f[5];
        try { h.latitude=std::stod(f[6]); h.longitude=std::stod(f[7]); h.totalBeds=std::stoi(f[9]); h.availableBeds=std::stoi(f[10]); h.totalAmbulances=std::stoi(f[11]); h.availableAmbulances=std::stoi(f[12]); h.ratingSum=std::stod(f[13]); h.ratingCount=std::stoi(f[14]); } catch (...) { continue; }
        h.departments=f[8]; h.doctors=f[15]; h.updatedAt=f[16]; h.accountEmail=f[17]; h.createdAt=f[18]; hospitals.push_back(std::move(h));
    }
    for (const auto& line : readLines(DATA_DIR / "accounts.db")) {
        const auto f = splitFields(line); if (f.size() >= 4) accounts.push_back({f[0], lower(f[1]), f[2], f[3]});
    }
    for (const auto& line : readLines(DATA_DIR / "appointments.db")) {
        const auto f=splitFields(line); if (f.size()<13) continue;
        appointments.push_back({f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7],f[8],f[9],f[10],f[11],f[12]=="1"});
    }
    for (const auto& line : readLines(DATA_DIR / "ratings.db")) {
        const auto f=splitFields(line); if (f.size()<6) continue;
        try { ratings.push_back({f[0],f[1],f[2],f[4],f[5],std::stoi(f[3])}); } catch (...) {}
    }
    for (const auto& line : readLines(DATA_DIR / "emergencies.db")) {
        const auto f=splitFields(line); if (f.size()<16) continue;
        Emergency e; e.id=f[0]; e.patientName=f[1]; e.phone=f[2]; e.symptoms=f[3];
        try { e.latitude=std::stod(f[4]); e.longitude=std::stod(f[5]); e.candidateIndex=std::stoi(f[8]); } catch (...) { continue; }
        e.hospitalId=f[6]; e.candidates=f[7]; e.status=f[9]; e.createdAt=f[10]; e.updatedAt=f[11]; e.timeline=f[12]; e.bedReserved=f[13]=="1"; e.ambulanceReserved=f[14]=="1"; emergencies.push_back(std::move(e));
    }
    rebuildIndexes();
}
void saveHospitals() {
    std::vector<std::string> lines;
    for (const auto& h : hospitals) lines.push_back(encodeRecord({h.id,h.name,h.city,h.area,h.address,h.phone,jsonNumber(h.latitude),jsonNumber(h.longitude),h.departments,std::to_string(h.totalBeds),std::to_string(h.availableBeds),std::to_string(h.totalAmbulances),std::to_string(h.availableAmbulances),jsonNumber(h.ratingSum),std::to_string(h.ratingCount),h.doctors,h.updatedAt,h.accountEmail,h.createdAt}));
    saveLines(DATA_DIR / "hospitals.db", lines);
}
void saveAccounts() {
    std::vector<std::string> lines;
    for (const auto& a : accounts) lines.push_back(encodeRecord({a.hospitalId,a.email,a.salt,a.passwordHash}));
    saveLines(DATA_DIR / "accounts.db", lines);
}
void saveAppointments() {
    std::vector<std::string> lines;
    for (const auto& a : appointments) lines.push_back(encodeRecord({a.id,a.hospitalId,a.patientName,a.phone,a.email,a.department,a.doctor,a.date,a.time,a.notes,a.status,a.createdAt,a.ratingSubmitted?"1":"0"}));
    saveLines(DATA_DIR / "appointments.db", lines);
}
void saveRatings() {
    std::vector<std::string> lines;
    for (const auto& r : ratings) lines.push_back(encodeRecord({r.id,r.appointmentId,r.hospitalId,std::to_string(r.stars),r.comment,r.createdAt}));
    saveLines(DATA_DIR / "ratings.db", lines);
}
void saveEmergencies() {
    std::vector<std::string> lines;
    for (const auto& e : emergencies) lines.push_back(encodeRecord({e.id,e.patientName,e.phone,e.symptoms,jsonNumber(e.latitude),jsonNumber(e.longitude),e.hospitalId,e.candidates,std::to_string(e.candidateIndex),e.status,e.createdAt,e.updatedAt,e.timeline,e.bedReserved?"1":"0",e.ambulanceReserved?"1":"0","v1"}));
    saveLines(DATA_DIR / "emergencies.db", lines);
}
double averageRating(const Hospital& h) { return h.ratingCount? h.ratingSum/h.ratingCount : 0; }
std::string hospitalJson(const Hospital& h, double distance = -1, bool full = true) {
    std::ostringstream out;
    out << "{\"id\":" << jsonString(h.id) << ",\"name\":" << jsonString(h.name)
        << ",\"city\":" << jsonString(h.city) << ",\"area\":" << jsonString(h.area)
        << ",\"address\":" << jsonString(h.address) << ",\"phone\":" << jsonString(h.phone)
        << ",\"latitude\":" << jsonNumber(h.latitude) << ",\"longitude\":" << jsonNumber(h.longitude)
        << ",\"departments\":" << stringArray(h.departments) << ",\"doctors\":" << stringArray(h.doctors)
        << ",\"totalBeds\":" << h.totalBeds << ",\"availableBeds\":" << h.availableBeds
        << ",\"totalAmbulances\":" << h.totalAmbulances << ",\"availableAmbulances\":" << h.availableAmbulances
        << ",\"rating\":" << jsonNumber(averageRating(h)) << ",\"ratingCount\":" << h.ratingCount
        << ",\"updatedAt\":" << jsonString(h.updatedAt) << ",\"distanceKm\":" << (distance<0?"null":jsonNumber(distance));
    if (full) {
        out << ",\"reviews\":[";
        bool first=true;
        for (const auto& r : ratings) if (r.hospitalId==h.id) {
            if (!first) out << ',';
            first=false;
            out << "{\"stars\":" << r.stars << ",\"comment\":" << jsonString(r.comment) << ",\"createdAt\":" << jsonString(r.createdAt) << "}";
        }
        out << ']';
    }
    out << '}'; return out.str();
}
std::string appointmentJson(const Appointment& a, bool privateData) {
    std::ostringstream out;
    out << "{\"id\":" << jsonString(a.id) << ",\"hospitalId\":" << jsonString(a.hospitalId)
        << ",\"hospitalName\":" << jsonString(hospitalById(a.hospitalId)?hospitalById(a.hospitalId)->name:"")
        << ",\"department\":" << jsonString(a.department) << ",\"doctor\":" << jsonString(a.doctor)
        << ",\"date\":" << jsonString(a.date) << ",\"time\":" << jsonString(a.time)
        << ",\"status\":" << jsonString(a.status) << ",\"createdAt\":" << jsonString(a.createdAt)
        << ",\"ratingSubmitted\":" << (a.ratingSubmitted?"true":"false");
    if (privateData) out << ",\"patientName\":" << jsonString(a.patientName) << ",\"phone\":" << jsonString(a.phone) << ",\"email\":" << jsonString(a.email) << ",\"notes\":" << jsonString(a.notes);
    out << '}'; return out.str();
}
std::string eventsJson(const std::string& timeline) {
    std::string out="["; bool first=true;
    std::istringstream in(timeline); std::string line;
    while (std::getline(in,line)) {
        const auto split=line.find('~'); if (split==std::string::npos) continue;
        if (!first) out+=',';
        first=false;
        out+="{\"at\":"+jsonString(line.substr(0,split))+",\"label\":"+jsonString(line.substr(split+1))+"}";
    }
    return out+"]";
}
std::string emergencyJson(const Emergency& e, bool privateData) {
    const auto* h=hospitalById(e.hospitalId);
    std::string out="{\"id\":"+jsonString(e.id)+",\"status\":"+jsonString(e.status)+",\"hospitalId\":"+jsonString(e.hospitalId)+",\"hospitalName\":"+jsonString(h?h->name:"")+
        ",\"createdAt\":"+jsonString(e.createdAt)+",\"updatedAt\":"+jsonString(e.updatedAt)+",\"timeline\":"+eventsJson(e.timeline);
    if (privateData) out+=",\"patientName\":"+jsonString(e.patientName)+",\"phone\":"+jsonString(e.phone)+",\"symptoms\":"+jsonString(e.symptoms)+",\"latitude\":"+jsonNumber(e.latitude)+",\"longitude\":"+jsonNumber(e.longitude);
    return out+"}";
}
std::string eventLabel(const std::string& status) {
    static const std::unordered_map<std::string,std::string> labels={
        {"hospital_notified","Hospital notified"},{"accepted","Hospital accepted; a bed was reserved"},
        {"callback_validated","Hospital called back and validated the request"},{"ambulance_assigned","Ambulance assigned"},
        {"dispatched","Ambulance dispatched"},{"patient_picked_up","Patient picked up"},
        {"hospital_reached","Patient reached the hospital"},{"completed","Request completed"},
        {"rejected","Hospital declined; checking the next option"},{"no_hospital_available","No matching hospital currently has both a bed and an ambulance"},
        {"cancelled","Request cancelled"}};
    const auto it=labels.find(status); return it==labels.end()?status:it->second;
}
void setEmergencyStatus(Emergency& e, const std::string& status, const std::string& note = {}) {
    e.status=status; e.updatedAt=nowIso();
    e.timeline += e.updatedAt+"~"+(note.empty()?eventLabel(status):note)+"\n";
}
void releaseEmergencyReservations(Emergency& e) {
    if (auto* h=hospitalById(e.hospitalId)) {
        if (e.bedReserved) h->availableBeds=std::min(h->totalBeds,h->availableBeds+1);
        if (e.ambulanceReserved) h->availableAmbulances=std::min(h->totalAmbulances,h->availableAmbulances+1);
        h->updatedAt=nowIso();
    }
    e.bedReserved=false; e.ambulanceReserved=false;
}
bool isTerminalEmergency(const std::string& status) {
    return status=="completed" || status=="cancelled" || status=="no_hospital_available";
}
bool fallbackEmergency(Emergency& e, const std::string& reason) {
    releaseEmergencyReservations(e);
    const auto ids=splitValues(e.candidates);
    for (int i=e.candidateIndex+1; i<static_cast<int>(ids.size()); ++i) {
        auto* h=hospitalById(ids[i]);
        if (!h || h->availableBeds<=0 || h->availableAmbulances<=0) continue;
        e.candidateIndex=i; e.hospitalId=h->id;
        setEmergencyStatus(e,"hospital_notified",reason+" MedEra routed the request to "+h->name+".");
        return true;
    }
    e.hospitalId.clear();
    setEmergencyStatus(e,"no_hospital_available",reason+" No next hospital currently has both a bed and an ambulance. Contact local emergency services now.");
    return false;
}

std::string api(const Request& request) {
    std::lock_guard<std::mutex> lock(dataMutex);
    const auto query=parseParams(request.query);
    const auto body=request.method=="POST"?parseParams(formBody(request)):std::map<std::string,std::string>{};
    const std::string& path=request.path;

    if (request.method=="GET" && path=="/api/health") return "{\"ok\":true,\"service\":\"MedEra\",\"version\":\"1.0\"}";
    if (request.method=="GET" && path=="/api/hospitals") {
        const auto city=param(query,"city"), area=param(query,"area"), department=param(query,"department");
        const bool beds=param(query,"beds")=="1";
        const bool ambulances=param(query,"ambulances")=="1";
        const auto sortMode=param(query,"sort");
        const bool hasLat=query.count("lat")&&query.count("lon");
        const double lat=numberParam(query,"lat"), lon=numberParam(query,"lon");
        if (hasLat && !validCoordinate(lat,lon)) return safeError("Enter a valid map location.");
        const auto results=rankHospitals(city,area,department,beds,ambulances,hasLat,lat,lon,12,sortMode);
        std::string out="{\"count\":"+std::to_string(results.size())+",\"results\":[";
        for (std::size_t i=0;i<results.size();++i) { if (i) out+=','; const auto* h=hospitalById(results[i].id); if (h) out+=hospitalJson(*h,results[i].distance,false); }
        return out+"]}";
    }
    if (request.method=="GET" && path.rfind("/api/hospitals/",0)==0) {
        const auto* h=hospitalById(path.substr(15));
        return h?hospitalJson(*h):safeError("Hospital not found.");
    }
    if (request.method=="POST" && path=="/api/register") {
        const auto name=param(body,"name"), city=param(body,"city"), area=param(body,"area"), email=lower(param(body,"email")), phone=param(body,"phone"), password=param(body,"password");
        if (name.size()<2 || city.empty() || area.empty()) return safeError("Enter the hospital name, city, and locality.");
        if (!validEmail(email)) return safeError("Enter a valid staff email address.");
        if (!validPhone(phone)) return safeError("Enter a valid hospital contact number.");
        if (password.size()<12 || password.size()>128) return safeError("Use a password between 12 and 128 characters.");
        if (std::any_of(accounts.begin(),accounts.end(),[&](const Account& a){return a.email==email;})) return safeError("An account already uses this email. Sign in instead.");
        Hospital h; h.id=randomHex(12); h.name=name; h.city=city; h.area=area; h.phone=phone; h.accountEmail=email; h.createdAt=nowIso(); h.updatedAt=h.createdAt;
        Account a{h.id,email,randomHex(16),""}; a.passwordHash=makePasswordHash(password,a.salt);
        hospitals.push_back(h); accounts.push_back(a); rebuildIndexes(); saveHospitals(); saveAccounts();
        const auto token=randomHex(32); { std::lock_guard<std::mutex> sessionLock(sessionMutex); sessions[token]=h.id; }
        return "{\"token\":"+jsonString(token)+",\"hospital\":"+hospitalJson(h)+"}";
    }
    if (request.method=="POST" && path=="/api/login") {
        const auto email=lower(param(body,"email")), password=param(body,"password");
        const auto a=std::find_if(accounts.begin(),accounts.end(),[&](const Account& item){return item.email==email;});
        if (a==accounts.end()) return safeError("Email or password is incorrect.");
        if (!constantTimeEqual(makePasswordHash(password,a->salt),a->passwordHash)) return safeError("Email or password is incorrect.");
        const auto token=randomHex(32); { std::lock_guard<std::mutex> sessionLock(sessionMutex); sessions[token]=a->hospitalId; }
        const auto* h=hospitalById(a->hospitalId);
        return "{\"token\":"+jsonString(token)+",\"hospital\":"+(h?hospitalJson(*h):"null")+"}";
    }
    if (request.method=="POST" && path=="/api/logout") {
        const auto token=authToken(request); { std::lock_guard<std::mutex> sessionLock(sessionMutex); sessions.erase(token); }
        return "{\"ok\":true}";
    }
    if (path.rfind("/api/staff/",0)==0) {
        const auto staffId=authorizeHospital(request);
        if (staffId.empty()) return safeError("Sign in to your hospital account to continue.");
        auto* ownHospital=hospitalById(staffId);
        if (!ownHospital) return safeError("This hospital account is unavailable.");
        if (request.method=="GET" && path=="/api/staff/overview") {
            std::string apps="[", emergenciesJson="["; bool first=true;
            for (const auto& a : appointments) if (a.hospitalId==staffId) { if (!first) apps+=','; first=false; apps+=appointmentJson(a,true); }
            first=true;
            for (const auto& e : emergencies) if (e.hospitalId==staffId) { if (!first) emergenciesJson+=','; first=false; emergenciesJson+=emergencyJson(e,true); }
            return "{\"hospital\":"+hospitalJson(*ownHospital)+",\"appointments\":"+apps+"],\"emergencies\":"+emergenciesJson+"]}";
        }
        if (request.method=="POST" && path=="/api/staff/profile") {
            const auto name=param(body,"name"), city=param(body,"city"), area=param(body,"area"), address=param(body,"address"), phone=param(body,"phone");
            const double lat=numberParam(body,"latitude",std::numeric_limits<double>::quiet_NaN()), lon=numberParam(body,"longitude",std::numeric_limits<double>::quiet_NaN());
            const int totalBeds=intParam(body,"totalBeds",-1), availableBeds=intParam(body,"availableBeds",-1), totalAmbulances=intParam(body,"totalAmbulances",-1), availableAmbulances=intParam(body,"availableAmbulances",-1);
            if (name.size()<2 || city.empty() || area.empty() || address.empty()) return safeError("Hospital name, city, locality, and address are required.");
            if (!validPhone(phone)) return safeError("Enter a valid hospital contact number.");
            if (!std::isfinite(lat)||!std::isfinite(lon)||!validCoordinate(lat,lon)) return safeError("Enter valid latitude and longitude coordinates.");
            if (totalBeds<0||availableBeds<0||availableBeds>totalBeds||totalAmbulances<0||availableAmbulances<0||availableAmbulances>totalAmbulances) return safeError("Available bed and ambulance counts must be between zero and their totals.");
            try { ownHospital->departments=csvList(param(body,"departments")); ownHospital->doctors=csvList(param(body,"doctors")); }
            catch (const std::exception& ex) { return safeError(ex.what()); }
            ownHospital->name=name; ownHospital->city=city; ownHospital->area=area; ownHospital->address=address; ownHospital->phone=phone;
            ownHospital->latitude=lat; ownHospital->longitude=lon; ownHospital->totalBeds=totalBeds; ownHospital->availableBeds=availableBeds;
            ownHospital->totalAmbulances=totalAmbulances; ownHospital->availableAmbulances=availableAmbulances; ownHospital->updatedAt=nowIso();
            rebuildIndexes(); saveHospitals();
            return "{\"hospital\":"+hospitalJson(*ownHospital)+"}";
        }
        if (request.method=="POST" && path=="/api/staff/appointment") {
            auto* a=appointmentById(param(body,"id")); if (!a||a->hospitalId!=staffId) return safeError("Appointment was not found.");
            const auto action=param(body,"action");
            if (action=="confirm" && a->status=="requested") a->status="confirmed";
            else if (action=="complete" && a->status=="confirmed") a->status="completed";
            else if (action=="cancel" && (a->status=="requested"||a->status=="confirmed")) a->status="cancelled";
            else return safeError("That appointment status change is not allowed.");
            saveAppointments(); return "{\"appointment\":"+appointmentJson(*a,true)+"}";
        }
        if (request.method=="POST" && path=="/api/staff/emergency") {
            auto* e=emergencyById(param(body,"id")); if (!e||e->hospitalId!=staffId) return safeError("Emergency request was not found for this hospital.");
            const auto action=param(body,"action");
            if (action=="accept" && e->status=="hospital_notified") {
                if (ownHospital->availableBeds<1) { fallbackEmergency(*e,"This hospital no longer has an available bed."); saveHospitals(); saveEmergencies(); return "{\"emergency\":"+emergencyJson(*e,true)+"}"; }
                --ownHospital->availableBeds; e->bedReserved=true; ownHospital->updatedAt=nowIso(); setEmergencyStatus(*e,"accepted");
            } else if (action=="reject" && e->status=="hospital_notified") {
                fallbackEmergency(*e,"The hospital declined the request.");
            } else if (action=="callback" && e->status=="accepted") {
                setEmergencyStatus(*e,"callback_validated");
            } else if (action=="assign" && e->status=="callback_validated") {
                if (ownHospital->availableAmbulances<1) { fallbackEmergency(*e,"This hospital no longer has an available ambulance."); saveHospitals(); saveEmergencies(); return "{\"emergency\":"+emergencyJson(*e,true)+"}"; }
                --ownHospital->availableAmbulances; e->ambulanceReserved=true; ownHospital->updatedAt=nowIso(); setEmergencyStatus(*e,"ambulance_assigned");
            } else if (action=="dispatch" && e->status=="ambulance_assigned") setEmergencyStatus(*e,"dispatched");
            else if (action=="pickup" && e->status=="dispatched") setEmergencyStatus(*e,"patient_picked_up");
            else if (action=="arrive" && e->status=="patient_picked_up") setEmergencyStatus(*e,"hospital_reached");
            else if (action=="complete" && e->status=="hospital_reached") {
                if (e->ambulanceReserved) { ownHospital->availableAmbulances=std::min(ownHospital->totalAmbulances,ownHospital->availableAmbulances+1); e->ambulanceReserved=false; }
                ownHospital->updatedAt=nowIso(); setEmergencyStatus(*e,"completed");
            } else return safeError("That emergency status change is not allowed at this stage.");
            saveHospitals(); saveEmergencies();
            return "{\"emergency\":"+emergencyJson(*e,true)+"}";
        }
        return safeError("Staff route not found.");
    }
    if (request.method=="POST" && path=="/api/appointments") {
        const auto hospitalId=param(body,"hospitalId"); auto* h=hospitalById(hospitalId);
        if (!h) return safeError("Choose a hospital from the directory first.");
        const auto patientName=param(body,"patientName"), phone=param(body,"phone"), department=param(body,"department"), doctor=param(body,"doctor"), date=param(body,"date"), time=param(body,"time"), email=param(body,"email"), notes=param(body,"notes");
        if (patientName.size()<2 || !validPhone(phone) || department.empty() || !validAppointmentDateTime(date,time)) return safeError("Enter your name, a valid phone number, department, and a valid current or future date and time.");
        if (!containsCaseInsensitive(h->departments,department)) return safeError("Choose a department listed by this hospital.");
        if (!doctor.empty()&&!containsCaseInsensitive(h->doctors,doctor)) return safeError("Choose a doctor listed by this hospital, or leave the doctor field blank.");
        if (!email.empty()&&!validEmail(email)) return safeError("Enter a valid email address or leave it blank.");
        Appointment a; a.id=randomHex(8); a.hospitalId=hospitalId; a.patientName=patientName; a.phone=phone; a.email=email; a.department=department; a.doctor=doctor; a.date=date; a.time=time; a.notes=notes; a.status="requested"; a.createdAt=nowIso();
        appointments.push_back(a); saveAppointments();
        return "{\"appointment\":"+appointmentJson(a,false)+",\"message\":\"Your request is waiting for hospital confirmation.\"}";
    }
    if (request.method=="GET" && path=="/api/appointments/status") {
        const auto* a=appointmentById(param(query,"code")); return a?appointmentJson(*a,false):safeError("Appointment request was not found.");
    }
    if (request.method=="POST" && path=="/api/ratings") {
        auto* a=appointmentById(param(body,"appointmentId")); const int stars=intParam(body,"stars"); const auto comment=param(body,"comment");
        if (!a||a->status!="completed") return safeError("Only a completed appointment can receive a review.");
        if (a->ratingSubmitted) return safeError("A review has already been submitted for this appointment.");
        if (stars<1||stars>5) return safeError("Choose a rating from one to five stars.");
        if (comment.size()>800) return safeError("Keep your review under 800 characters.");
        Rating r{randomHex(8),a->id,a->hospitalId,comment,nowIso(),stars}; ratings.push_back(r); a->ratingSubmitted=true;
        if (auto* h=hospitalById(a->hospitalId)) { h->ratingSum+=stars; ++h->ratingCount; h->updatedAt=nowIso(); saveHospitals(); }
        saveAppointments(); saveRatings(); return "{\"ok\":true}";
    }
    if (request.method=="POST" && path=="/api/emergencies") {
        const auto name=param(body,"patientName"), phone=param(body,"phone"), city=param(body,"city"), area=param(body,"area"), symptoms=param(body,"symptoms");
        const double lat=numberParam(body,"latitude",std::numeric_limits<double>::quiet_NaN()), lon=numberParam(body,"longitude",std::numeric_limits<double>::quiet_NaN());
        const bool hasLocation=std::isfinite(lat)&&std::isfinite(lon);
        if (name.size()<2||!validPhone(phone)||city.empty()) return safeError("Enter your name, contact number, and city.");
        if (hasLocation&&!validCoordinate(lat,lon)) return safeError("Enter valid location coordinates.");
        auto choices=rankHospitals(city,area,"",true,true,hasLocation,hasLocation?lat:0,hasLocation?lon:0,std::max(1,static_cast<int>(hospitals.size())));
        Emergency e; e.id=randomHex(16); e.patientName=name; e.phone=phone; e.symptoms=symptoms; e.latitude=hasLocation?lat:0; e.longitude=hasLocation?lon:0; e.createdAt=nowIso(); e.updatedAt=e.createdAt;
        for (const auto& choice:choices) e.candidates+=(e.candidates.empty()?"":",")+choice.id;
        if (choices.empty()) setEmergencyStatus(e,"no_hospital_available","No matching hospital has both a currently available bed and ambulance. Contact local emergency services now.");
        else { e.candidateIndex=0; e.hospitalId=choices.front().id; setEmergencyStatus(e,"hospital_notified","Request created. The hospital must respond manually; MedEra has not called or dispatched anyone."); }
        emergencies.push_back(e); saveEmergencies();
        return "{\"emergency\":"+emergencyJson(e,false)+",\"trackingCode\":"+jsonString(e.id)+"}";
    }
    if (request.method=="GET" && path=="/api/emergencies/status") {
        const auto* e=emergencyById(param(query,"code")); return e?emergencyJson(*e,false):safeError("Emergency tracking code was not found.");
    }
    if (request.method=="POST" && path=="/api/emergencies/action") {
        auto* e=emergencyById(param(body,"id")); if (!e) return safeError("Emergency request was not found.");
        const auto action=param(body,"action");
        const bool cancellable=e->status=="hospital_notified"||e->status=="accepted"||e->status=="callback_validated"||e->status=="ambulance_assigned";
        if (action=="cancel" && cancellable) {
            releaseEmergencyReservations(*e); setEmergencyStatus(*e,"cancelled","Request cancelled by the person tracking it."); saveHospitals(); saveEmergencies(); return "{\"emergency\":"+emergencyJson(*e,false)+"}";
        }
        if (action=="no-response" && e->status=="hospital_notified") {
            fallbackEmergency(*e,"No response was recorded from the hospital."); saveHospitals(); saveEmergencies(); return "{\"emergency\":"+emergencyJson(*e,false)+"}";
        }
        return safeError("That action is not available for this request right now.");
    }
    return safeError("API route not found.");
}

bool readRequest(SOCKET socket, Request& request) {
    std::string buffer; std::array<char,4096> chunk{};
    std::size_t headerEnd=std::string::npos;
    while ((headerEnd=buffer.find("\r\n\r\n"))==std::string::npos) {
        const int got=recv(socket,chunk.data(),static_cast<int>(chunk.size()),0); if (got<=0) return false;
        buffer.append(chunk.data(),got); if (buffer.size()>16384) return false;
    }
    std::istringstream headers(buffer.substr(0,headerEnd));
    std::string line;
    if (!std::getline(headers,line)) return false;
    if (!line.empty()&&line.back()=='\r') line.pop_back();
    std::istringstream first(line); first>>request.method;
    std::string target; first>>target;
    const auto queryAt=target.find('?'); request.path=target.substr(0,queryAt); request.query=queryAt==std::string::npos?"":target.substr(queryAt+1);
    while (std::getline(headers,line)) {
        if (!line.empty()&&line.back()=='\r') line.pop_back();
        const auto colon=line.find(':'); if (colon==std::string::npos) continue;
        request.headers[lower(trim(line.substr(0,colon)))]=trim(line.substr(colon+1));
    }
    std::size_t length=0;
    const auto contentLength=request.headers.find("content-length");
    if (contentLength!=request.headers.end()) { try { length=static_cast<std::size_t>(std::stoul(contentLength->second)); } catch (...) { return false; } }
    if (length>65536) return false;
    const auto bodyStart=headerEnd+4;
    while (buffer.size()-bodyStart<length) {
        const int got=recv(socket,chunk.data(),static_cast<int>(chunk.size()),0); if (got<=0) return false; buffer.append(chunk.data(),got);
    }
    request.body=buffer.substr(bodyStart,length);
    return true;
}
void sendResponse(SOCKET socket, int status, const std::string& contentType, const std::string& body) {
    const char* statusText=status==200?"OK":status==400?"Bad Request":status==403?"Forbidden":status==404?"Not Found":status==405?"Method Not Allowed":"Internal Server Error";
    std::ostringstream head;
    head << "HTTP/1.1 " << status << ' ' << statusText << "\r\nContent-Type: " << contentType
         << "\r\nContent-Length: " << body.size() << "\r\nConnection: close\r\nCache-Control: no-store\r\n"
         << "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n"
         << "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'\r\n\r\n";
    const std::string response=head.str()+body;
    std::size_t sent=0;
    while (sent<response.size()) { const int n=send(socket,response.data()+sent,static_cast<int>(response.size()-sent),0); if (n<=0) break; sent+=static_cast<std::size_t>(n); }
}
void handleClient(SOCKET client) {
    DWORD timeout=10000; setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    Request request;
    if (!readRequest(client,request)) { sendResponse(client,400,"application/json; charset=utf-8",safeError("Request could not be read.")); closesocket(client); return; }
    try {
        if (request.method=="POST") {
            const auto origin=request.headers.find("origin");
            const std::string portSuffix=":"+std::to_string(serverPort);
            const bool localOrigin=origin==request.headers.end() || origin->second=="http://127.0.0.1"+portSuffix || origin->second=="http://localhost"+portSuffix;
            if (!localOrigin) { sendResponse(client,403,"application/json; charset=utf-8",safeError("Cross-origin form submissions are not accepted.")); closesocket(client); return; }
        }
        if (request.path.rfind("/api/",0)==0) {
            if (request.method!="GET"&&request.method!="POST") sendResponse(client,405,"application/json; charset=utf-8",safeError("Method not allowed."));
            else sendResponse(client,200,"application/json; charset=utf-8",api(request));
        } else if (request.method=="GET") {
            std::string file;
            if (request.path=="/"||request.path=="/index.html") file="public/index.html";
            else if (request.path=="/app.js") file="public/app.js";
            else if (request.path=="/styles.css") file="public/styles.css";
            else { sendResponse(client,404,"text/plain; charset=utf-8","Not found"); closesocket(client); return; }
            std::ifstream in(APP_DIR / file,std::ios::binary);
            if (!in) { sendResponse(client,500,"text/plain; charset=utf-8","App files are missing. Run the server from the MedEra folder."); closesocket(client); return; }
            std::ostringstream content; content<<in.rdbuf();
            const auto type=request.path=="/app.js"?"text/javascript; charset=utf-8":request.path=="/styles.css"?"text/css; charset=utf-8":"text/html; charset=utf-8";
            sendResponse(client,200,type,content.str());
        } else sendResponse(client,405,"text/plain; charset=utf-8","Method not allowed");
    } catch (const std::exception& error) {
        sendResponse(client,400,"application/json; charset=utf-8",safeError(error.what()));
    } catch (...) {
        sendResponse(client,500,"application/json; charset=utf-8",safeError("An unexpected server error occurred."));
    }
    shutdown(client,SD_SEND); closesocket(client);
}
int main(int argc, char** argv) {
    int port=8080;
    if (argc>1) { try { port=std::stoi(argv[1]); } catch (...) { std::cerr<<"Usage: medera.exe [port]\n"; return 2; } }
    if (port<1024||port>65535) { std::cerr<<"Choose a port between 1024 and 65535.\n"; return 2; }
    serverPort=port;
    std::array<wchar_t,32768> executablePath{};
    const DWORD executablePathLength=GetModuleFileNameW(nullptr,executablePath.data(),static_cast<DWORD>(executablePath.size()));
    if (executablePathLength==0||executablePathLength>=executablePath.size()) {
        std::cerr<<"Could not determine the MedEra application folder.\n"; return 1;
    }
    APP_DIR=fs::path(std::wstring(executablePath.data(),executablePathLength)).parent_path();
    DATA_DIR=APP_DIR/"data";
    try { loadDatabase(); }
    catch (const std::exception& error) { std::cerr<<"Could not load MedEra data: "<<error.what()<<"\n"; return 1; }
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2,2),&wsa)!=0) { std::cerr<<"Winsock startup failed.\n"; return 1; }
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (listener==INVALID_SOCKET) { std::cerr<<"Could not create server socket.\n"; WSACleanup(); return 1; }
    sockaddr_in address{}; address.sin_family=AF_INET; address.sin_port=htons(static_cast<u_short>(port)); address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if (bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==SOCKET_ERROR||listen(listener,SOMAXCONN)==SOCKET_ERROR) {
        std::cerr<<"Could not listen on 127.0.0.1:"<<port<<". The port may already be in use.\n"; closesocket(listener); WSACleanup(); return 1;
    }
    std::cout<<"MedEra is running at http://127.0.0.1:"<<port<<"\n";
    std::cout<<"Persistent records are stored in the data folder beside the executable. Press Ctrl+C to stop.\n";
    while (true) {
        const SOCKET client=accept(listener,nullptr,nullptr);
        if (client==INVALID_SOCKET) { std::cerr<<"A client connection could not be accepted.\n"; continue; }
        std::thread(handleClient,client).detach();
    }
    closesocket(listener); WSACleanup(); return 0;
}
