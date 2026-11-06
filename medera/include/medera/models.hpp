#pragma once

#include <map>
#include <string>

struct Hospital {
    std::string id, name, city, area, address, phone, departments, doctors;
    std::string updatedAt, accountEmail, createdAt;
    double latitude = 0, longitude = 0, ratingSum = 0;
    int totalBeds = 0, availableBeds = 0, totalAmbulances = 0, availableAmbulances = 0, ratingCount = 0;
    bool listed = true;
};

struct Account { std::string hospitalId, email, salt, passwordHash; };

struct Appointment {
    std::string id, hospitalId, patientName, phone, email, department, doctor, date, time, notes, status, createdAt;
    bool ratingSubmitted = false;
};

struct Rating { std::string id, appointmentId, hospitalId, comment, createdAt; int stars = 0; };

struct Emergency {
    std::string id, patientName, phone, symptoms, hospitalId, candidates, status, createdAt, updatedAt, timeline;
    double latitude = 0, longitude = 0;
    int candidateIndex = -1;
    bool bedReserved = false, ambulanceReserved = false;
};

struct Request {
    std::string method, path, query, body;
    std::map<std::string, std::string> headers;
};

struct Ranked { std::string id; double score = 0, distance = -1; };
