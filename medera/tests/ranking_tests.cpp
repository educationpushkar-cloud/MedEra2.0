#include "medera/ranking.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

std::vector<Hospital> hospitals;
std::unordered_map<std::string, std::vector<std::string>> locationIndex;
std::unordered_map<std::string, std::vector<std::string>> cityIndex;

Hospital* hospitalById(const std::string& id) {
    for (auto& hospital : hospitals) {
        if (hospital.id == id) return &hospital;
    }
    return nullptr;
}

namespace {
int failures = 0;

void expect(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "PASS  " << name << '\n';
    } else {
        std::cerr << "FAIL  " << name << '\n';
        ++failures;
    }
}

void expectNear(double actual, double expected, double tolerance, const std::string& name) {
    expect(std::abs(actual - expected) <= tolerance, name);
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

Hospital makeHospital(std::string id, std::string city, std::string area, std::string departments,
                      int beds, int ambulances, double latitude, double longitude,
                      double ratingSum = 0, int ratingCount = 0, bool listed = true) {
    Hospital hospital;
    hospital.id = std::move(id);
    hospital.name = hospital.id;
    hospital.city = std::move(city);
    hospital.area = std::move(area);
    hospital.address = "Test address";
    hospital.departments = std::move(departments);
    hospital.availableBeds = beds;
    hospital.availableAmbulances = ambulances;
    hospital.latitude = latitude;
    hospital.longitude = longitude;
    hospital.ratingSum = ratingSum;
    hospital.ratingCount = ratingCount;
    hospital.listed = listed;
    return hospital;
}

void rebuildIndexes() {
    locationIndex.clear();
    cityIndex.clear();
    for (const auto& hospital : hospitals) {
        locationIndex[lowercase(hospital.city) + "|" + lowercase(hospital.area)].push_back(hospital.id);
        cityIndex[lowercase(hospital.city)].push_back(hospital.id);
    }
}

void testDistanceAndSpecialtyHelpers() {
    expectNear(haversineKm(12.0, 77.0, 12.0, 77.0), 0.0, 1e-9,
               "Haversine distance is zero at the same point");
    expectNear(haversineKm(0.0, 0.0, 1.0, 0.0), 111.195, 0.1,
               "Haversine distance is approximately correct for one degree latitude");
    expectNear(haversineKm(1.0, 0.0, 0.0, 0.0), haversineKm(0.0, 0.0, 1.0, 0.0), 1e-9,
               "Haversine distance is symmetric");
    expect(containsCaseInsensitive("Cardiology, Emergency Medicine", "CARDIO"),
           "Specialty matching ignores case and allows partial names");
    expect(!containsCaseInsensitive("Cardiology, Emergency Medicine", "Neurology"),
           "Specialty matching rejects absent departments");
    expect(containsCaseInsensitive("Cardiology", ""), "An empty specialty does not filter hospitals");
}

void testHospitalFiltersAndLocalityFallback() {
    hospitals = {
        makeHospital("beds", "Dehradun", "Baily Road", "Cardiology", 3, 1, 30.32, 78.03, 8, 2),
        makeHospital("ambulance", "Dehradun", "Baily Road", "cardiology", 7, 0, 30.33, 78.04, 9, 2),
        makeHospital("hidden", "Dehradun", "Baily Road", "Cardiology", 20, 5, 30.34, 78.05, 10, 2, false),
        makeHospital("other-area", "Dehradun", "Clement Town", "Pediatrics", 9, 2, 30.35, 78.06, 10, 2)
    };
    rebuildIndexes();

    const auto withBeds = rankHospitals("Dehradun", "Baily", "cardio", true, false, false, 0, 0, 10, "beds");
    expect(withBeds.size() == 2, "Bed filter keeps listed hospitals with beds and the requested specialty");
    expect(!withBeds.empty() && withBeds.front().id == "ambulance",
           "Bed sorting puts the hospital with more beds first");

    const auto withAmbulance = rankHospitals("Dehradun", "Baily", "Cardiology", true, true, false, 0, 0, 10);
    expect(withAmbulance.size() == 1 && withAmbulance.front().id == "beds",
           "Ambulance filter excludes hospitals without available ambulances");

    const auto localityFallback = rankHospitals("Dehradun", "Clement", "", false, false, false, 0, 0, 10);
    expect(localityFallback.size() == 1 && localityFallback.front().id == "other-area",
           "Locality search falls back to city candidates and matches the area");

    const auto limited = rankHospitals("Dehradun", "", "", false, false, false, 0, 0, 1, "beds");
    expect(limited.size() == 1 && limited.front().id == "other-area",
           "Top-K ranking returns only the highest bed-count result when limited to one");
    expect(rankHospitals("", "", "", false, false, false, 0, 0, 0).empty(),
           "A non-positive result limit returns no hospitals");
}

void testDistanceRankingAndMissingLocation() {
    hospitals = {
        makeHospital("far", "North", "Central", "General", 5, 1, 1.0, 0.0),
        makeHospital("near", "South", "Central", "General", 5, 1, 0.1, 0.0)
    };
    rebuildIndexes();

    const auto byDistance = rankHospitals("", "", "", false, false, true, 0.0, 0.0, 2, "distance");
    expect(byDistance.size() == 2 && byDistance.front().id == "near",
           "Distance sort ranks the nearest hospital first");
    expect(!byDistance.empty() && byDistance.front().distance >= 0.0,
           "Shared-location results include an estimated distance");

    const auto withoutLocation = rankHospitals("", "", "", false, false, false, 0.0, 0.0, 2);
    expect(withoutLocation.size() == 2 && withoutLocation.front().distance == -1.0,
           "Search without shared location does not calculate or expose a distance");
}

void testRatingRankingAndCaseInsensitiveLocation() {
    hospitals = {
        makeHospital("lower-rated", "Dehradun", "Baily Road", "Cardiology", 8, 1, 30.3, 78.0, 6, 2),
        makeHospital("top-rated", "Dehradun", "Baily Road", "Cardiology", 1, 1, 30.3, 78.0, 10, 2),
        makeHospital("no-beds", "Dehradun", "Baily Road", "Cardiology", 0, 1, 30.3, 78.0, 1, 1)
    };
    rebuildIndexes();

    const auto byRating = rankHospitals("dEhRaDuN", "bAiLy", "CARDIO", false, false, false, 0, 0, 3,
                                        "rating");
    expect(byRating.size() == 3 && byRating.front().id == "top-rated",
           "Rating sort ranks the highest average rating first with case-insensitive filters");

    const auto availableOnly = rankHospitals("Dehradun", "Baily", "Cardiology", true, false, false,
                                             0, 0, 3, "rating");
    expect(availableOnly.size() == 2 && availableOnly.front().id == "top-rated",
           "Bed availability filter excludes a hospital with zero beds");
}

void testFallbackIndexAndInvalidCandidates() {
    hospitals = {
        makeHospital("valid", "Dehradun", "Baily Road", "General", 4, 1, 30.3, 78.0),
        makeHospital("missing-address", "Dehradun", "Baily Road", "General", 4, 1, 30.3, 78.0),
        makeHospital("hidden", "Dehradun", "Baily Road", "General", 4, 1, 30.3, 78.0, 0, 0, false)
    };
    hospitals[1].address.clear();
    rebuildIndexes();
    cityIndex["dehradun"].push_back("stale-index-entry");

    const auto results = rankHospitals("Dehradun", "Baily", "", false, false, false, 0, 0, 5);
    expect(results.size() == 1 && results.front().id == "valid",
           "City fallback safely skips stale index IDs, hidden listings, and incomplete records");
    expect(rankHospitals("Dehradun", "Not a locality", "", false, false, false, 0, 0, 5).empty(),
           "City fallback still applies the requested locality filter");
}
} // namespace

int main() {
    testDistanceAndSpecialtyHelpers();
    testHospitalFiltersAndLocalityFallback();
    testDistanceRankingAndMissingLocation();
    testRatingRankingAndCaseInsensitiveLocation();
    testFallbackIndexAndInvalidCandidates();

    if (failures != 0) {
        std::cerr << failures << " ranking test(s) failed.\n";
        return 1;
    }
    std::cout << "All ranking tests passed.\n";
    return 0;
}
