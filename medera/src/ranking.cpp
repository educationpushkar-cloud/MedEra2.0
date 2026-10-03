#include "medera/ranking.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <queue>
#include <string>
#include <vector>

namespace {
std::string normalize(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

bool containsCaseInsensitive(const std::string& values, const std::string& needle) {
    if (needle.empty()) return true;
    std::size_t start = 0;
    while (start <= values.size()) {
        const auto end = values.find(',', start);
        const auto value = normalize(values.substr(start, end == std::string::npos ? end : end - start));
        if (value.find(normalize(needle)) != std::string::npos) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

double haversineKm(double lat1, double lon1, double lat2, double lon2) {
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const double dLat = (lat2 - lat1) * radians;
    const double dLon = (lon2 - lon1) * radians;
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
        std::cos(lat1 * radians) * std::cos(lat2 * radians) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 6371.0 * 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
}

std::vector<Ranked> rankHospitals(const std::string& city, const std::string& area, const std::string& department,
                                 bool requireBeds, bool requireAmbulance, bool hasLocation, double lat, double lon, int limit,
                                 const std::string& sortMode) {
    if (limit <= 0) return {};
    std::vector<std::string> candidates;
    const auto cityKey = normalize(city), areaKey = normalize(area);
    if (!city.empty() && !area.empty()) {
        const auto found = locationIndex.find(cityKey + "|" + areaKey);
        if (found != locationIndex.end()) candidates = found->second;
        else { const auto inCity = cityIndex.find(cityKey); if (inCity != cityIndex.end()) candidates = inCity->second; }
    } else if (!city.empty()) {
        const auto found = cityIndex.find(cityKey);
        if (found != cityIndex.end()) candidates = found->second;
    } else {
        for (const auto& hospital : hospitals) candidates.push_back(hospital.id);
    }

    struct LowerScore { bool operator()(const Ranked& a, const Ranked& b) const { return a.score > b.score; } };
    std::priority_queue<Ranked, std::vector<Ranked>, LowerScore> top;
    for (const auto& id : candidates) {
        const auto* hospital = hospitalById(id);
        if (!hospital || !hospital->listed) continue;
        if (!area.empty() && normalize(hospital->area).find(areaKey) == std::string::npos) continue;
        if (hospital->address.empty() || hospital->departments.empty()) continue;
        if (!containsCaseInsensitive(hospital->departments, department)) continue;
        if (requireBeds && hospital->availableBeds <= 0) continue;
        if (requireAmbulance && hospital->availableAmbulances <= 0) continue;
        const double distance = hasLocation ? haversineKm(lat, lon, hospital->latitude, hospital->longitude) : -1;
        const double distanceScore = hasLocation ? 1.0 - std::min(distance, 150.0) / 150.0 : 0.0;
        const double ratingScore = hospital->ratingCount ? std::clamp((hospital->ratingSum / hospital->ratingCount) / 5.0, 0.0, 1.0) : 0.6;
        const double bedScore = std::clamp(hospital->availableBeds / 10.0, 0.0, 1.0);
        const double ambulanceScore = hospital->availableAmbulances > 0 ? std::clamp(hospital->availableAmbulances / 3.0, 0.5, 1.0) : 0.0;
        double score = hasLocation ? 0.45 * distanceScore + 0.25 * ratingScore + 0.20 * bedScore + 0.10 * ambulanceScore
                                   : 0.45 * ratingScore + 0.40 * bedScore + 0.15 * ambulanceScore;
        if (sortMode == "distance" && hasLocation) score = 1.0 - std::min(distance, 10000.0) / 10000.0;
        else if (sortMode == "rating") score = ratingScore;
        else if (sortMode == "beds") score = hospital->availableBeds + ratingScore / 100.0;
        Ranked item{ id, score, distance };
        if (static_cast<int>(top.size()) < limit) top.push(item);
        else if (score > top.top().score) { top.pop(); top.push(item); }
    }
    std::vector<Ranked> result;
    while (!top.empty()) { result.push_back(top.top()); top.pop(); }
    std::sort(result.begin(), result.end(), [](const Ranked& a, const Ranked& b) {
        if (a.score == b.score) return a.id < b.id;
        return a.score > b.score;
    });
    return result;
}
