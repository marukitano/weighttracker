#pragma once

void draw_chart_callback(Layer *layer, GContext *ctx);
void set_multiplier(double m);

void set_chart_pulse_step(uint8_t step);

double get_chart_display_weight(void);

int get_chart_history_count(void);
double get_chart_history_weight(int offset);

void set_chart_selected_history_offset(int offset);
