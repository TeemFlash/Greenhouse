#pragma once
// Measured on this device, 2026-10-20, after the user confirmed dry soil
// and then wet soil. Each endpoint is the mean of eight fresh readings.
// Source evidence: build/soil_dry_reference.json and soil_wet_reference.json.
// This is a relative dry-to-wet scale, not volumetric water content.
// Used only if no valid stored calibration record exists.
constexpr int kRecordedSoilDry = 9038;
constexpr int kRecordedSoilWet = 7382;
