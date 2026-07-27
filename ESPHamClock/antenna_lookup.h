// antenna_lookup.h
// LICENSE_BEGIN
// Copyright (c) 2026 David Strickland KR8X
// Contributor to HamClock by Elwood Charles Downey.
// SPDX-License-Identifier: MIT
// See LICENSE file distributed with HamClock for full license text.
// LICENSE_END 
#ifndef ANTENNA_LOOKUP_H
#define ANTENNA_LOOKUP_H

#include <cstdint>
#include <string>

struct AntennaInfo {
	std::string path;
	std::string description;
};

// Build the lookup table from ANTENNA_DATA (call once at startup).
void antenna_lookup_init();

// Return pointer to AntennaInfo for the given index (msb*256+lsb),
// or nullptr if not found.
const AntennaInfo* antenna_lookup(uint16_t index);

#endif // ANTENNA_LOOKUP_H
