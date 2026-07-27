// antenna_lookup.cpp
// LICENSE_BEGIN
// Copyright (c) 2026 David Strickland KR8X and openhamclock/hamclock-standards
// See https://github.com/openhamclock/hamclock-standards/blob/main/LICENSE.md
//
// Licensed under Apache License 2.0
// https://www.apache.org/licenses/LICENSE-2.0
// LICENSE_END 
#include "antenna_lookup.h"
#include "antenna_data.h"       // generated -- contains ANTENNA_DATA[]

#include <unordered_map>
#include <cstdint>

static std::unordered_map<uint16_t, AntennaInfo> s_table;

void antenna_lookup_init()
{
	s_table.reserve(ANTENNA_DATA_COUNT);
	for (std::size_t i = 0; i < ANTENNA_DATA_COUNT; ++i) {
		const AntennaEntry& e = ANTENNA_DATA[i];
		s_table[e.index] = AntennaInfo{ e.path, e.description };
	}
}

const AntennaInfo* antenna_lookup(uint16_t index)
{
	auto it = s_table.find(index);
	return (it != s_table.end()) ? &it->second : nullptr;
}