// antennas.cpp
// LICENSE_BEGIN
// Copyright (c) 2026 David Strickland KR8X
// Contributor to HamClock by Elwood Charles Downey.
// SPDX-License-Identifier: MIT
// See LICENSE file distributed with HamClock for full license text.
// LICENSE_END
#include "HamClock.h"
#include "antenna_lookup.h"
#include "antenna_data.h"

//
// files to store advanced antenna propagation parameters
//

uint16_t antennas_de;
uint16_t antennas_dx;
uint8_t  antennas_dedx_control;
float    antennas_de_az;
float    antennas_dx_az;

//
// called during setup to initialize NV parameters and data structures
//
void initAntennas()
{
	// initialize antenna_lookup data structures
	// it is done here and not from ESPHamClock to keep antenna_data confined here 
    antenna_lookup_init();
	// initialize advanced antenna propagation parameters.  If they don't exist in nvram, initialize them to defaults
    if (!NVReadUInt8 (NV_ANT_DEDX_CONTROL, &antennas_dedx_control)) {
        antennas_dedx_control = 0;  
        NVWriteUInt8 (NV_ANT_DEDX_CONTROL, antennas_dedx_control);
    }
    if (!NVReadUInt16 (NV_ANT_DE_INDEX, &antennas_de)) {
        antennas_de = 0;  
        NVWriteUInt16 (NV_ANT_DE_INDEX, antennas_de);
    }
    if (!NVReadUInt16 (NV_ANT_DX_INDEX, &antennas_dx)) {
        antennas_dx = 0;  
        NVWriteUInt16 (NV_ANT_DX_INDEX, antennas_dx);
    }
    if (!NVReadFloat (NV_ANT_DE_AZ, &antennas_de_az)) {
        antennas_de_az = 0.0;  
        NVWriteFloat (NV_ANT_DE_AZ, antennas_de_az);
    }
    if (!NVReadFloat (NV_ANT_DX_AZ, &antennas_dx_az)) {
        antennas_dx_az = 0.0;  
        NVWriteFloat (NV_ANT_DX_AZ, antennas_dx_az);
    }	
}

//
// urls that can have antenna control parameters added have antenna control parameters added here based on parameters above
//

void antenna_addargs(char *buf, size_t size) {
	if (antennas_dedx_control > 0) {
		snprintf(buf,size,"&ANTDEDXCONTROL=%d",antennas_dedx_control);
		if (antennas_dedx_control & 1) {
			int len = strlen(buf);
			snprintf(buf+len,size-len,"&ANTDEINDEX=%d&ANTDEAZ=%.1f",antennas_de,antennas_de_az);
		}
		if (antennas_dedx_control & 2) {
			int len = strlen(buf);
			snprintf(buf+len,size-len,"&ANTDXINDEX=%d&ANTDXAZ=%.1f",antennas_dx,antennas_dx_az);	
		}			
	}
}

//
// get line of antenna description information.  return true if line at line number exists, else false
//	
bool antenna_getline(char *buf, size_t size, int lineno) {
    if (lineno < 0)
		return (false);
	if (lineno == 0) {
		snprintf(buf, size, "%6s %-12s %s","Index","model","Description");
		return(true);
	}
	size_t i = lineno-1;
	if (i >= ANTENNA_DATA_COUNT)
        return (false);		
    const AntennaEntry& e = ANTENNA_DATA[i];
    const AntennaInfo*  info = antenna_lookup(e.index);
    if (info) {
		size_t pos = info->path.find('/');
		std::string after;
		if (pos != std::string::npos) {
			after = info->path.substr(pos + 1);
		} else {
			after = info->path;  // expect substr after / but initialize just in case
		}
        snprintf(buf, size, "%6d %-12s %s", e.index, after.c_str(), info->description.c_str());
		return(true);
    }
    return (false);
}

//
// use antenna_lookup to determine if an antenna index is valid 
//
bool antenna_validindex(uint16_t index)
{
	const AntennaInfo* it = antenna_lookup(index);
    return (it != nullptr);
}