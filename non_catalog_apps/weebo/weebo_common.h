#pragma once

#include <furi.h>
#include <weebo_i.h>

void weebo_calculate_pwd(uint8_t* uid, uint8_t* pwd);
void weebo_remix(Weebo* weebo);
