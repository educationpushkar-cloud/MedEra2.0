#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "medera/models.hpp"

extern std::vector<Hospital> hospitals;
extern std::unordered_map<std::string, std::vector<std::string>> locationIndex;
extern std::unordered_map<std::string, std::vector<std::string>> cityIndex;

Hospital* hospitalById(const std::string& id);

double haversineKm(double lat1, double lon1, double lat2, double lon2);
bool containsCaseInsensitive(const std::string& values, const std::string& needle);
std::vector<Ranked> rankHospitals(const std::string& city, const std::string& area, const std::string& department,
                                 bool requireBeds, bool requireAmbulance, bool hasLocation, double lat, double lon, int limit,
                                 const std::string& sortMode = "recommended");
//"Add hospital ranking and location utility functions"
//Implement Haversine distance and hospital filtering