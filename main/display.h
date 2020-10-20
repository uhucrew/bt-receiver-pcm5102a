#pragma once


void display_init();
void display_volume(uint8_t vol);
void display_state(char *state, uint8_t *remote_name, uint8_t offset);
void display_sample_rate(int sample_rate);
void display_packets(uint32_t packets);
void update_vu_meter(uint64_t level[2]);
void display_reboot();
