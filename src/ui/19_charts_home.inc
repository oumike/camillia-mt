// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: charts, glance carousel, home dashboard.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static int16_t chartClampInt(float v, int16_t lo, int16_t hi) {
    if (v < (float)lo) return lo;
    if (v > (float)hi) return hi;
    return (int16_t)lroundf(v);
}

// LVGL v9 removed the chart's built-in axis ticks and labels, along with the
// LV_EVENT_DRAW_PART_BEGIN hook the "%d%%" / "%d dB" formatting used to ride
// on. The replacement is a standalone lv_scale sitting beside the plot, with
// the tick text supplied up front as a static array — which is why the labels
// below are literals rather than formatted per draw.
//
// Tick geometry mirrors the old lv_chart_set_axis_tick(chart, axis, 4, 2, 5, 2,
// true, w) calls: 5 major ticks with 2 minor steps between each, so 9 ticks
// total with every 2nd major. lv_scale draws vertical ticks bottom-to-top, so
// index 0 of the label array is the range minimum.
static lv_obj_t *makeChartYScale(lv_obj_t *parent, lv_scale_mode_t mode,
                                 int32_t width, int32_t rangeMin, int32_t rangeMax,
                                 const char **labels) {
    lv_obj_t *scale = lv_scale_create(parent);
    lv_obj_remove_style_all(scale);
    lv_obj_set_width(scale, width);
    lv_obj_set_height(scale, lv_pct(100));
    lv_obj_clear_flag(scale, LV_OBJ_FLAG_SCROLLABLE);
    lv_scale_set_mode(scale, mode);
    lv_scale_set_range(scale, rangeMin, rangeMax);
    lv_scale_set_total_tick_count(scale, 9);
    lv_scale_set_major_tick_every(scale, 2);
    lv_scale_set_label_show(scale, true);
    lv_scale_set_text_src(scale, labels);

    // Axis line (MAIN) and minor ticks (ITEMS) match the chart's grid lines.
    lv_obj_set_style_line_color(scale, lv_color_hex(0x335D9D), LV_PART_MAIN);
    lv_obj_set_style_line_width(scale, 1, LV_PART_MAIN);
    lv_obj_set_style_line_color(scale, lv_color_hex(0x335D9D), LV_PART_ITEMS);
    lv_obj_set_style_line_width(scale, 1, LV_PART_ITEMS);
    lv_obj_set_style_length(scale, 2, LV_PART_ITEMS);
    // Major ticks and their labels use the old LV_PART_TICKS text styling.
    lv_obj_set_style_line_color(scale, lv_color_hex(0xA7C7FF), LV_PART_INDICATOR);
    lv_obj_set_style_line_width(scale, 1, LV_PART_INDICATOR);
    lv_obj_set_style_length(scale, 4, LV_PART_INDICATOR);
    lv_obj_set_style_text_font(scale, &lv_font_montserrat_10, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(scale, lv_color_hex(0xA7C7FF), LV_PART_INDICATOR);
    lv_obj_set_style_pad_left(scale, 3, LV_PART_INDICATOR);
    lv_obj_set_style_pad_right(scale, 3, LV_PART_INDICATOR);
    return scale;
}

// A scale is a sibling of the chart, not a child, so nothing lines its ticks up
// with the plot area automatically. The chart's grid spans its content box, so
// inset the scale by the chart's border + vertical padding to match.
static void syncChartScalePads(lv_obj_t *scale, lv_obj_t *chart) {
    if (!scale || !chart) return;
    int32_t border = lv_obj_get_style_border_width(chart, LV_PART_MAIN);
    lv_obj_set_style_pad_top(scale,
                             lv_obj_get_style_pad_top(chart, LV_PART_MAIN) + border,
                             LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(scale,
                                lv_obj_get_style_pad_bottom(chart, LV_PART_MAIN) + border,
                                LV_PART_MAIN);
}

// Horizontal band holding [scale][chart][scale]; grows to fill the modal the
// way the bare chart used to.
static lv_obj_t *makeChartRow(lv_obj_t *parent) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    // Children carry their own height: 100% here, so no cross-axis stretch
    // needed (and v9's flex has no STRETCH align anyway).
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    return row;
}

#if defined(DEVICE_TDECK_PRO)
static void styleTdeckProChartSeries(lv_event_t *e) {
    lv_draw_task_t *task = lv_event_get_draw_task(e);
    if (!task || lv_draw_task_get_type(task) != LV_DRAW_TASK_TYPE_LINE) return;
    lv_draw_dsc_base_t *base =
        (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(task);
    if (!base || base->part != LV_PART_ITEMS) return;

    lv_draw_line_dsc_t *line =
        (lv_draw_line_dsc_t *)lv_draw_task_get_draw_dsc(task);
    line->color = lv_color_make(0, 0, 0);
    line->width = (base->id1 == 0) ? 2 : 1;
    line->dash_width = (base->id1 == 0) ? 0 : 4;
    line->dash_gap = (base->id1 == 0) ? 0 : 3;
}

static void applyTdeckProChartStyle(lv_obj_t *chart) {
    lv_obj_set_style_bg_color(chart, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chart, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_line_color(chart, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_dash_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(chart, 3, LV_PART_MAIN);
    lv_obj_add_event_cb(chart, styleTdeckProChartSeries, LV_EVENT_DRAW_TASK_ADDED, nullptr);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
}
#endif

static void refreshChUtilChart(bool force) {
    if (!s_chUtilChartModal || !s_chUtilChart) return;
    if (!lv_obj_is_valid(s_chUtilChartModal) || !lv_obj_is_valid(s_chUtilChart)) {
        s_chUtilChartModal = nullptr;
        s_chUtilChart = nullptr;
        s_chUtilSeries = nullptr;
        s_airUtilSeries = nullptr;
        s_chUtilStatsLabel = nullptr;
        return;
    }

    if (!force
        && s_chUtilRenderedSeq == s_chUtilHist.seq
        && s_airUtilRenderedSeq == s_airUtilHist.seq) {
        return;
    }

    auto fillSeries = [&](lv_chart_series_t *series, const ChartHist &h) {
        if (!series) return;
        const int offset = ChartHist::CAP - h.count;  // right-align newest sample
        for (int i = 0; i < ChartHist::CAP; i++) {
            int sampleIdx = i - offset;
            if (sampleIdx >= 0 && sampleIdx < h.count) {
                int16_t v = chartClampInt(chartSampleAt(h, sampleIdx), 0, 100);
                lv_chart_set_value_by_id(s_chUtilChart, series, i, v);
            } else {
                lv_chart_set_value_by_id(s_chUtilChart, series, i, LV_CHART_POINT_NONE);
            }
        }
    };
    fillSeries(s_chUtilSeries, s_chUtilHist);
    fillSeries(s_airUtilSeries, s_airUtilHist);
    lv_chart_refresh(s_chUtilChart);

    s_chUtilRenderedSeq = s_chUtilHist.seq;
    s_airUtilRenderedSeq = s_airUtilHist.seq;

    if (s_chUtilStatsLabel && lv_obj_is_valid(s_chUtilStatsLabel)) {
        char text[160];
        char chCur[16] = "--";
        char airCur[16] = "--";
        if (s_chUtilHist.hasLast) snprintf(chCur, sizeof(chCur), "%.1f%%", (double)s_chUtilHist.lastVal);
        if (s_airUtilHist.hasLast) snprintf(airCur, sizeof(airCur), "%.1f%%", (double)s_airUtilHist.lastVal);

        float chSum = 0.0f, chMax = 0.0f;
        for (int i = 0; i < s_chUtilHist.count; i++) {
            float s = chartSampleAt(s_chUtilHist, i);
            chSum += s;
            if (s > chMax) chMax = s;
        }
        float airSum = 0.0f, airMax = 0.0f;
        for (int i = 0; i < s_airUtilHist.count; i++) {
            float s = chartSampleAt(s_airUtilHist, i);
            airSum += s;
            if (s > airMax) airMax = s;
        }
        float chAvg = (s_chUtilHist.count > 0) ? (chSum / s_chUtilHist.count) : 0.0f;
        float airAvg = (s_airUtilHist.count > 0) ? (airSum / s_airUtilHist.count) : 0.0f;

        snprintf(text, sizeof(text),
                 TR("ChUtil  cur %s  avg %.1f%%  max %.1f%%   n=%d\n"
                 "AirTx   cur %s  avg %.1f%%  max %.1f%%   n=%d"),
                 chCur, (double)chAvg, (double)chMax, s_chUtilHist.count,
                 airCur, (double)airAvg, (double)airMax, s_airUtilHist.count);
        lv_label_set_text(s_chUtilStatsLabel, text);
    }
}

static void openChUtilChartModal() {
    if (!s_rootScreen) return;
    if (s_chUtilChartModal) return;

    int modalW = lv_disp_get_hor_res(NULL);
    int modalH = lv_disp_get_ver_res(NULL);

    s_chUtilChartModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_chUtilChartModal, modalW, modalH);
    lv_obj_align(s_chUtilChartModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_chUtilChartModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_chUtilChartModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_chUtilChartModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chUtilChartModal, 1, 0);
    lv_obj_set_style_border_color(s_chUtilChartModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_chUtilChartModal, 4, 0);
    lv_obj_set_style_pad_row(s_chUtilChartModal, 4, 0);
    lv_obj_set_flex_flow(s_chUtilChartModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_chUtilChartModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Portrait (P4, Heltec V4): Tools' title and no X -- the nav bar added at the end is
    // the way out. The units the header title carried are on the stats lines.
#if HAS_TOOL_STACK_LAYOUT
    if (toolStackLayout()) {
        toolStackTitle(s_chUtilChartModal, TR("ChUtil"));
    } else
#endif
    {
    lv_obj_t *header = lv_obj_create(s_chUtilChartModal);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 26);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_70, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_left(header, 4, 0);
    lv_obj_set_style_pad_right(header, 4, 0);
    lv_obj_set_style_pad_top(header, 1, 0);
    lv_obj_set_style_pad_bottom(header, 1, 0);

    cornerSafeHeader(header);
    lv_obj_t *title = lv_label_create(header);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, TR("CHANNEL UTILIZATION (%)"));
    lv_obj_center(title);

    }   // header bar

    // Bottom-to-top, matching lv_scale's vertical tick order.
    static const char *kChUtilYLabels[] = { "0%", "25%", "50%", "75%", "100%", nullptr };

    lv_obj_t *chartRow = makeChartRow(s_chUtilChartModal);
    lv_obj_t *yScale = makeChartYScale(chartRow, LV_SCALE_MODE_VERTICAL_LEFT,
                                       40, 0, 100, kChUtilYLabels);

    s_chUtilChart = lv_chart_create(chartRow);
    lv_obj_set_height(s_chUtilChart, lv_pct(100));
    lv_obj_set_flex_grow(s_chUtilChart, 1);
    lv_chart_set_type(s_chUtilChart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chUtilChart, ChartHist::CAP);
    lv_chart_set_range(s_chUtilChart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(s_chUtilChart, 5, 6);
    lv_chart_set_update_mode(s_chUtilChart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_size(s_chUtilChart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_chUtilChart, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_chUtilChart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_chUtilChart, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_border_width(s_chUtilChart, 1, 0);
    lv_obj_set_style_line_color(s_chUtilChart, lv_color_hex(0x335D9D), LV_PART_MAIN);
    lv_obj_set_style_line_opa(s_chUtilChart, LV_OPA_40, LV_PART_MAIN);
#if defined(DEVICE_TDECK_PRO)
    applyTdeckProChartStyle(s_chUtilChart);
#endif
    syncChartScalePads(yScale, s_chUtilChart);

#if defined(DEVICE_TDECK_PRO)
    s_chUtilSeries = lv_chart_add_series(s_chUtilChart,
                                         lv_color_make(0, 0, 0),
                                         LV_CHART_AXIS_PRIMARY_Y);
    s_airUtilSeries = lv_chart_add_series(s_chUtilChart,
                                          lv_color_make(0, 0, 0),
                                          LV_CHART_AXIS_PRIMARY_Y);
#else
    s_chUtilSeries = lv_chart_add_series(s_chUtilChart,
                                         lv_color_hex(0x4FD1C5),
                                         LV_CHART_AXIS_PRIMARY_Y);
    s_airUtilSeries = lv_chart_add_series(s_chUtilChart,
                                          lv_color_hex(0xF6AD55),
                                          LV_CHART_AXIS_PRIMARY_Y);
#endif
    if (s_chUtilSeries) lv_chart_set_all_value(s_chUtilChart, s_chUtilSeries, LV_CHART_POINT_NONE);
    if (s_airUtilSeries) lv_chart_set_all_value(s_chUtilChart, s_airUtilSeries, LV_CHART_POINT_NONE);

    s_chUtilStatsLabel = lv_label_create(s_chUtilChartModal);
    lv_obj_set_width(s_chUtilStatsLabel, lv_pct(100));
    lv_label_set_long_mode(s_chUtilStatsLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_chUtilStatsLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chUtilStatsLabel, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(s_chUtilStatsLabel, TR("ChUtil  cur --  avg --  max --   n=0\nAirTx   cur --  avg --  max --   n=0"));

#if !UI_TOUCH_ONLY_PROFILE
    lv_obj_t *hint = lv_label_create(s_chUtilChartModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
#if defined(DEVICE_TDECK_PRO)
    lv_label_set_text(hint, TR("Bksp=Back  solid=ChUtil  dash=AirTx"));
#else
    lv_label_set_text_fmt(hint, TR("%s = Back   teal=ChUtil  orange=AirTx"), modalCloseKeyLabel());
#endif
#endif

    // The way out of this screen on every build: it has no X. Tools lit, as
    // the screen it was opened from. Last, so its spacer is the final child of
    // the column. A no-op where the bar is switched off.
    appendHeltecBottomNav(s_chUtilChartModal, HELTEC_NAV_TOOLS);

    s_chUtilRenderedSeq = 0;
    s_airUtilRenderedSeq = 0;
    refreshChUtilChart(true);
}

static void refreshSnrRssiChart(bool force) {
    if (!s_snrChartModal || !s_snrChart) return;
    if (!lv_obj_is_valid(s_snrChartModal) || !lv_obj_is_valid(s_snrChart)) {
        s_snrChartModal = nullptr;
        s_snrChart = nullptr;
        s_snrSeries = nullptr;
        s_rssiSeries = nullptr;
        s_snrStatsLabel = nullptr;
        return;
    }

    if (!force
        && s_snrRenderedSeq == s_snrHist.seq
        && s_rssiRenderedSeq == s_rssiHist.seq) {
        return;
    }

    auto fillSeries = [&](lv_chart_series_t *series, const ChartHist &h, int16_t lo, int16_t hi) {
        if (!series) return;
        const int offset = ChartHist::CAP - h.count;  // right-align newest sample
        for (int i = 0; i < ChartHist::CAP; i++) {
            int sampleIdx = i - offset;
            if (sampleIdx >= 0 && sampleIdx < h.count) {
                int16_t v = chartClampInt(chartSampleAt(h, sampleIdx), lo, hi);
                lv_chart_set_value_by_id(s_snrChart, series, i, v);
            } else {
                lv_chart_set_value_by_id(s_snrChart, series, i, LV_CHART_POINT_NONE);
            }
        }
    };
    fillSeries(s_snrSeries, s_snrHist, -25, 15);
    fillSeries(s_rssiSeries, s_rssiHist, -130, -30);
    lv_chart_refresh(s_snrChart);

    s_snrRenderedSeq = s_snrHist.seq;
    s_rssiRenderedSeq = s_rssiHist.seq;

    if (s_snrStatsLabel && lv_obj_is_valid(s_snrStatsLabel)) {
        char text[160];
        char snrCur[16] = "--";
        char rssiCur[16] = "--";
        if (s_snrHist.hasLast) snprintf(snrCur, sizeof(snrCur), "%.1fdB", (double)s_snrHist.lastVal);
        if (s_rssiHist.hasLast) snprintf(rssiCur, sizeof(rssiCur), "%.0fdBm", (double)s_rssiHist.lastVal);

        float snrSum = 0.0f, snrMin = 1000.0f, snrMax = -1000.0f;
        for (int i = 0; i < s_snrHist.count; i++) {
            float s = chartSampleAt(s_snrHist, i);
            snrSum += s;
            if (s < snrMin) snrMin = s;
            if (s > snrMax) snrMax = s;
        }
        float rssiSum = 0.0f, rssiMin = 1000.0f, rssiMax = -1000.0f;
        for (int i = 0; i < s_rssiHist.count; i++) {
            float s = chartSampleAt(s_rssiHist, i);
            rssiSum += s;
            if (s < rssiMin) rssiMin = s;
            if (s > rssiMax) rssiMax = s;
        }
        float snrAvg = (s_snrHist.count > 0) ? (snrSum / s_snrHist.count) : 0.0f;
        float rssiAvg = (s_rssiHist.count > 0) ? (rssiSum / s_rssiHist.count) : 0.0f;

        if (s_snrHist.count == 0) snrMin = snrMax = 0.0f;
        if (s_rssiHist.count == 0) rssiMin = rssiMax = 0.0f;

        snprintf(text, sizeof(text),
                 TR("SNR   cur %s  avg %.1fdB  min %.1f  max %.1f   n=%d\n"
                 "RSSI  cur %s  avg %.0fdBm min %.0f  max %.0f   n=%d"),
                 snrCur, (double)snrAvg, (double)snrMin, (double)snrMax, s_snrHist.count,
                 rssiCur, (double)rssiAvg, (double)rssiMin, (double)rssiMax, s_rssiHist.count);
        lv_label_set_text(s_snrStatsLabel, text);
    }
}

static void openSnrRssiChartModal() {
    if (!s_rootScreen) return;
    if (s_snrChartModal) return;

    int modalW = lv_disp_get_hor_res(NULL);
    int modalH = lv_disp_get_ver_res(NULL);

    s_snrChartModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_snrChartModal, modalW, modalH);
    lv_obj_align(s_snrChartModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_snrChartModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_snrChartModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_snrChartModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_snrChartModal, 1, 0);
    lv_obj_set_style_border_color(s_snrChartModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_snrChartModal, 4, 0);
    lv_obj_set_style_pad_row(s_snrChartModal, 4, 0);
    lv_obj_set_flex_flow(s_snrChartModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_snrChartModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Portrait (P4, Heltec V4): Tools' title and no X -- the nav bar added at the end is
    // the way out. The units the header title carried are on the stats lines.
#if HAS_TOOL_STACK_LAYOUT
    if (toolStackLayout()) {
        toolStackTitle(s_snrChartModal, TR("SNR/RSSI"));
    } else
#endif
    {
    lv_obj_t *header = lv_obj_create(s_snrChartModal);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 26);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_70, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_left(header, 4, 0);
    lv_obj_set_style_pad_right(header, 4, 0);
    lv_obj_set_style_pad_top(header, 1, 0);
    lv_obj_set_style_pad_bottom(header, 1, 0);

    cornerSafeHeader(header);
    lv_obj_t *title = lv_label_create(header);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, TR("SNR (dB)  /  RSSI (dBm)"));
    lv_obj_center(title);

    }   // header bar

    // Bottom-to-top. Units live in the modal title ("SNR (dB) / RSSI (dBm)")
    // rather than on every tick — a per-tick suffix no longer fits now that the
    // labels are fixed strings and the columns are only as wide as the widest.
    static const char *kSnrYLabels[]  = { "-25", "-15", "-5", "5", "15", nullptr };
    static const char *kRssiYLabels[] = { "-130", "-105", "-80", "-55", "-30", nullptr };

    lv_obj_t *chartRow = makeChartRow(s_snrChartModal);
    lv_obj_t *snrScale = makeChartYScale(chartRow, LV_SCALE_MODE_VERTICAL_LEFT,
                                         34, -25, 15, kSnrYLabels);

    s_snrChart = lv_chart_create(chartRow);
    lv_obj_set_height(s_snrChart, lv_pct(100));
    lv_obj_set_flex_grow(s_snrChart, 1);
    lv_chart_set_type(s_snrChart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_snrChart, ChartHist::CAP);
    lv_chart_set_range(s_snrChart, LV_CHART_AXIS_PRIMARY_Y, -25, 15);
    lv_chart_set_range(s_snrChart, LV_CHART_AXIS_SECONDARY_Y, -130, -30);
    lv_chart_set_div_line_count(s_snrChart, 5, 6);
    lv_chart_set_update_mode(s_snrChart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_size(s_snrChart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_snrChart, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_snrChart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_snrChart, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_border_width(s_snrChart, 1, 0);
    lv_obj_set_style_line_color(s_snrChart, lv_color_hex(0x335D9D), LV_PART_MAIN);
    lv_obj_set_style_line_opa(s_snrChart, LV_OPA_40, LV_PART_MAIN);
#if defined(DEVICE_TDECK_PRO)
    applyTdeckProChartStyle(s_snrChart);
#endif

    lv_obj_t *rssiScale = makeChartYScale(chartRow, LV_SCALE_MODE_VERTICAL_RIGHT,
                                          40, -130, -30, kRssiYLabels);
    syncChartScalePads(snrScale, s_snrChart);
    syncChartScalePads(rssiScale, s_snrChart);

#if defined(DEVICE_TDECK_PRO)
    s_snrSeries = lv_chart_add_series(s_snrChart,
                                      lv_color_make(0, 0, 0),
                                      LV_CHART_AXIS_PRIMARY_Y);
    s_rssiSeries = lv_chart_add_series(s_snrChart,
                                       lv_color_make(0, 0, 0),
                                       LV_CHART_AXIS_SECONDARY_Y);
#else
    s_snrSeries = lv_chart_add_series(s_snrChart,
                                      lv_color_hex(0x68D391),
                                      LV_CHART_AXIS_PRIMARY_Y);
    s_rssiSeries = lv_chart_add_series(s_snrChart,
                                       lv_color_hex(0xF687B3),
                                       LV_CHART_AXIS_SECONDARY_Y);
#endif
    if (s_snrSeries) lv_chart_set_all_value(s_snrChart, s_snrSeries, LV_CHART_POINT_NONE);
    if (s_rssiSeries) lv_chart_set_all_value(s_snrChart, s_rssiSeries, LV_CHART_POINT_NONE);

    s_snrStatsLabel = lv_label_create(s_snrChartModal);
    lv_obj_set_width(s_snrStatsLabel, lv_pct(100));
    lv_label_set_long_mode(s_snrStatsLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_snrStatsLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_snrStatsLabel, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(s_snrStatsLabel,
                      TR("SNR   cur --  avg --  min --  max --   n=0\n"
                      "RSSI  cur --  avg --  min --  max --   n=0"));

#if !UI_TOUCH_ONLY_PROFILE
    lv_obj_t *hint = lv_label_create(s_snrChartModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
#if defined(DEVICE_TDECK_PRO)
    lv_label_set_text(hint, TR("Bksp=Back  solid=SNR  dash=RSSI"));
#else
    lv_label_set_text_fmt(hint, TR("%s = Back   green=SNR  pink=RSSI"), modalCloseKeyLabel());
#endif
#endif

    // The way out of this screen on every build: it has no X. Tools lit, as
    // the screen it was opened from. Last, so its spacer is the final child of
    // the column. A no-op where the bar is switched off.
    appendHeltecBottomNav(s_snrChartModal, HELTEC_NAV_TOOLS);

    s_snrRenderedSeq = 0;
    s_rssiRenderedSeq = 0;
    refreshSnrRssiChart(true);
}


#if HAS_HOME_DASHBOARD
// ── Home dashboard ───────────────────────────────────────────────────────────
// What the Home button opens. The top half is the lock screen's glance header —
// wordmark, node name, clock, conditions, date, battery — and the bottom half is
// the radio's own health: channel utilisation on the left, SNR/RSSI on the
// right. The chat screen's shortcut bar is left showing beneath it, so the key
// hints do not disappear on the one screen someone is most likely to be lost on.
//
// A destination, not a modal: no backdrop, no close X, and it stops short of the
// shortcut bar rather than covering it. Chat moves onto its own button beside
// Home, which is what makes room for this to exist at all.
//
// The charts are deliberately a second, smaller pair rather than the Tools
// modals reparented. Those own module-level pointers, a y-axis scale and a
// three-line stats block sized for a full panel; borrowing them would mean
// either two owners for one widget set or a modal that lays itself out
// differently depending on who opened it. Sixty points and a value line is all
// this surface wants, and it reads the same ChartHist rings, so there is no
// second source of truth — only a second view of the one there is.

// ---- Glance carousel ------------------------------------------------------
// The chart pair is one face of a band, not a fixture. The others answer what
// the charts cannot -- *who* is out there -- from the two ends of one ordering:
// what was heard most recently, and what has been silent longest. Faces of one
// band rather than screens of their own, because a glance surface has exactly
// one free strip under its header and all of them belong in it.
//
// Two surfaces run one of these. The home dashboard's is driven by the user --
// swipe, tap or j/k -- and its page is sticky across a rebuild, because Home is
// pressed to glance at something and a surface that forgets which face you left
// it on makes you swipe back to it every single time. The lock screen's is
// driven by a timer and takes no input at all, because on that screen every
// input is a dismissal; in exchange it carries a face the dashboard has no use
// for -- the recent messages that used to be the whole of that band.
//
// Instanced rather than shared globals. exitLockScreen() restores whatever was
// underneath rather than rebuilding it, so the dashboard can be sitting open
// *behind* a lit lock screen with both carousels built at once. One set of
// pointers would have the second build overwrite the first's, and the dashboard
// would come back with its charts pointing at objects LVGL had already freed.
enum GlancePageKind : uint8_t {
    GLANCE_PAGE_MESSAGES = 0,   // lock screen only
    GLANCE_PAGE_CHARTS,
    GLANCE_PAGE_RECENT,
    GLANCE_PAGE_OLDEST,
    GLANCE_PAGE_BOTH_LISTS,     // wide panels: both lists on one page
};

// Messages, charts, and either one paired node page or two single ones.
static constexpr int kGlancePageMax = 4;

// Rows are built once, at whatever count the band turned out to fit, and
// refilled in place afterwards -- a repaint must not churn LVGL objects on a
// surface that repaints once a minute forever. The cap is a ceiling on the
// array, not the answer: glanceNodeRowCapacity() decides from real geometry.
static constexpr int kHomeDashNodeRowsMax = 12;

struct GlanceCarousel {
    // The band. Every page occupies it exactly and one of them is visible.
    lv_obj_t *host;
    lv_obj_t *page[kGlancePageMax];
    uint8_t   kind[kGlancePageMax];
    uint8_t   pages;    // how many of the above were actually built
    uint8_t   at;       // which one is in front
    bool      themed;   // the dashboard takes the UI theme; the lock screen does not

    lv_obj_t *chUtilCard;   // the cards themselves: the dashboard's tap
    lv_obj_t *snrCard;      // targets, and what its cursor outlines
    lv_obj_t *chUtilChart;
    lv_obj_t *chUtilValue;
    lv_obj_t *snrChart;
    lv_obj_t *snrValue;
    lv_chart_series_t *chUtilSeries;
    lv_chart_series_t *airUtilSeries;
    lv_chart_series_t *snrSeries;
    lv_chart_series_t *rssiSeries;
    uint32_t chUtilSeq;
    uint32_t airUtilSeq;
    uint32_t snrSeq;
    uint32_t rssiSeq;

    // [0] is the recent list, [1] the oldest one. Which page each ends up on
    // depends on the panel's width; the arrays do not move with it.
    //
    // Two labels per row rather than one: the name can be a 39-byte long name
    // and the age is the column these lists exist to show, so they cannot share
    // a label -- a single one would let a long name push the age off the end of
    // the card, losing exactly the thing the page is sorted by. The name flexes
    // and ellipsizes; the age keeps its natural width against the right edge.
    // Same name/value pairing buildGlanceChartCard() uses for heading and value.
    lv_obj_t *nodeCard[2];   // [0] recent, [1] oldest; dashboard tap targets
    lv_obj_t *nodeRow[2][kHomeDashNodeRowsMax];
    lv_obj_t *nodeAge[2][kHomeDashNodeRowsMax];
    int       nodeRows[2];
    // Refilling a node page walks the node table, so it has to be gated: NodeDB
    // has no sequence counter, and asking it "did you move?" at frame rate would
    // be 250 entries a tick. The SNR ring is pushed once per received packet, so
    // its seq is the cheap stand-in for "something arrived" -- paired with the
    // minute tick, which covers the ages, since those creep with the clock and
    // not with traffic.
    uint32_t  nodeSeq;
};

// Clears every pointer the deleted objects left behind. `at` is deliberately
// not touched: it is which face to rebuild on, not a child pointer, and each
// caller decides for itself whether that should survive.
static void glanceCarouselForget(GlanceCarousel &c) {
    const uint8_t at = c.at;
    const bool themed = c.themed;
    c = GlanceCarousel{};
    c.at = at;
    c.themed = themed;
    c.nodeSeq = UINT32_MAX;
}

static GlanceCarousel s_homeCarousel;
static uint32_t s_homeGlanceMinuteKey = UINT32_MAX;
// The dashboard's cursor, for the boards with no touch to point with: Up puts
// it on the weather, Down on the first card of the band's face in front (the
// Channel Util chart, or a node list), Right steps across to the card beside
// it, and Up/Down between the two rows. Enter opens whatever it is on. See
// homeDashCursorKey().
enum HomeDashCursor : uint8_t {
    HOME_CURSOR_NONE = 0,
    HOME_CURSOR_WEATHER,
    HOME_CURSOR_CHUTIL,
    HOME_CURSOR_SNR,
    HOME_CURSOR_NODES_RECENT,   // nodeCard[0]
    HOME_CURSOR_NODES_OLDEST,   // nodeCard[1]
};
static HomeDashCursor s_homeCursor = HOME_CURSOR_NONE;

// The ink the cursor is drawn in. The Pro's 1-bit panel would threshold the
// accent to whatever it lands nearest, so there it is the header's own ink.
static lv_color_t homeDashCursorInk() {
#if defined(DEVICE_TDECK_PRO)
    return glancePalette(/*themed=*/true).ink;
#else
    return lvColorFrom565(s_ui.selectAccent);
#endif
}

// Moves the cursor and repaints every target, so no path can leave two lit.
// An outline on the cards rather than their border: they already carry a 1 px
// border in the card colour, and an outline sits outside it without having to
// remember what to put back. The page's 4 px padding leaves it room.
static void homeDashSetCursor(HomeDashCursor to) {
    s_homeCursor = to;
#if HAS_WEATHER
    if (lvObjValid(s_homeGlance.wxHit)) {
        lv_obj_set_style_border_width(s_homeGlance.wxHit,
                                      to == HOME_CURSOR_WEATHER ? 2 : 0, 0);
    }
#endif
    if (lvObjValid(s_homeCarousel.chUtilCard)) {
        lv_obj_set_style_outline_width(s_homeCarousel.chUtilCard,
                                       to == HOME_CURSOR_CHUTIL ? 2 : 0, 0);
    }
    if (lvObjValid(s_homeCarousel.snrCard)) {
        lv_obj_set_style_outline_width(s_homeCarousel.snrCard,
                                       to == HOME_CURSOR_SNR ? 2 : 0, 0);
    }
    for (int i = 0; i < 2; i++) {
        if (!lvObjValid(s_homeCarousel.nodeCard[i])) continue;
        const HomeDashCursor mine = i == 0 ? HOME_CURSOR_NODES_RECENT
                                           : HOME_CURSOR_NODES_OLDEST;
        lv_obj_set_style_outline_width(s_homeCarousel.nodeCard[i],
                                       to == mine ? 2 : 0, 0);
    }
}

#if FEATURE_LOCK_SCREEN
// The lock screen's. Built by showTdeckProSleepClock() and torn down with the
// overlay, so unlike the dashboard's it starts on its first face every time --
// which is the messages page, the most urgent thing this screen has to say and
// what the band showed before it turned into a carousel.
static GlanceCarousel s_lockCarousel;
// When the band last turned itself. The lock screen takes no input -- every
// press there dismisses it -- so the only thing that can advance this carousel
// is the clock.
static uint32_t s_lockCarouselTurnedMs = 0;
// Long enough to read a page of node names off without hurrying, short enough
// that a lock screen left up cycles through everything it knows in about two
// minutes.
static constexpr uint32_t kLockCarouselDwellMs = 30000;
#endif

// Tap anywhere on the band to turn it forward. The e-paper panel is the reason:
// a swipe there has to be slow enough for a touch controller sampling against a
// 700 ms refresh to see the whole drag, which makes it a fiddly gesture to land
// — where a tap is one unambiguous event. The keyboard boards keep a reverse
// direction through Alt+left/right, so the Pro loses nothing by only going one
// way on tap, and three pages means any of them is at most two taps off.
#if defined(DEVICE_TDECK_PRO)
#define HAS_HOME_CAROUSEL_TAP 1
#else
#define HAS_HOME_CAROUSEL_TAP 0
#endif

static inline bool homeDashboardVisible() { return lvObjValid(s_homeDash); }

// True only when the dashboard is also the surface on top. homeDashboardVisible()
// answers existence, and the two part company the moment a full-screen view is
// opened over a dashboard that was never closed. That is not a corner case: the
// letter shortcuts in the chat fall-through (N for Nodes, F for Config, D, L, A)
// call their openers directly instead of going through prepareGlobalNavigation(),
// which is the only thing that closes the dashboard on the way somewhere else.
// So the modal lands on top and s_homeDash is still alive underneath it.
//
// Asked by z-order rather than by naming the surfaces that can cover the
// dashboard. A list would have to be complete to be correct, and an entry left
// out of it costs exactly what this function exists to prevent: keys silently
// swallowed on a screen nobody thought to add. Every surface that can cover the
// dashboard is a child of s_rootScreen built after it, so "is anything stacked
// above" is the same question asked in a way that cannot fall behind.
//
// The bar is the one deliberate exception. homeDashRaiseBar() puts it above the
// dashboard on purpose and it is meant to be visible there, so it is not
// something covering anything. Hidden siblings are skipped for the same reason:
// they cover nothing either.
static bool homeDashboardIsForeground() {
    if (!homeDashboardVisible()) return false;
    lv_obj_t *parent = lv_obj_get_parent(s_homeDash);
    if (!parent) return false;

    const int32_t dashIdx = lv_obj_get_index(s_homeDash);
    if (dashIdx < 0) return false;
    const uint32_t childCount = lv_obj_get_child_count(parent);
    for (uint32_t i = (uint32_t)dashIdx + 1; i < childCount; i++) {
        lv_obj_t *above = lv_obj_get_child(parent, (int32_t)i);
        if (!above || above == s_chatShortcutBar) continue;
        if (lv_obj_has_flag(above, LV_OBJ_FLAG_HIDDEN)) continue;
        return false;
    }
    return true;
}

#if UI_TOUCH_NAV_BAR
// Which of the two glance surfaces the shared bar should be lighting. Home and
// Chats are one bar's worth of cells over two screens that swap without the bar
// being rebuilt, so the lit cell has to be told to move.
static void homeDashMarkNavBar(bool onDashboard) {
    navBarSetActive(s_chatShortcutBar, onDashboard ? HELTEC_NAV_HOME : HELTEC_NAV_CHAT);
}
#else
static inline void homeDashMarkNavBar(bool) {}
#endif

// The bar under the dashboard keeps its right-hand cluster — Wi-Fi, GPS and the
// DM alert are status, and status belongs on every screen — but the key hints
// on its left have no business there. They name what the letters do *on the
// chat screen*, which is not the screen you are looking at.
//
// Where there is a hint line to take over, that space becomes a ticker of what
// has arrived instead: the one question a glance surface should answer and did
// not. Where there is not — a nav bar of icon buttons has no text row — the
// hints are simply hidden, as before.
#if HAS_HOME_DASHBOARD && !UI_TOUCH_ONLY_PROFILE && !defined(DEVICE_TDECK_PRO)
// A scrolling label is an LVGL animation, i.e. a continuous stream of panel
// refreshes. That is free on a backlit LCD and ruinous on the Pro's e-paper,
// whose dashboard already rides a once-a-minute cadence for the same reason —
// so the Pro keeps the plain hidden hints.
#define HAS_HOME_TICKER 1
#else
#define HAS_HOME_TICKER 0
#endif

#if HAS_HOME_TICKER
// What the strip is currently showing, as tdeckProSleepUnreadKey() reports it.
// That key already folds in the ring's sequence number as well as the unread
// counts, so it moves for a second message on an already-unread channel — which
// is exactly the case a count-only check misses.
static uint64_t s_homeTickerKey = UINT64_MAX;

// How many messages one line carries. Three is what fits before the scroll
// becomes long enough that the newest one is off screen most of the time.
static constexpr int kHomeTickerMaxMsgs = 3;

static void homeTickerBuildText(char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';

    const TdeckProRecentMsg *picked[kTdeckProSleepMsgSlots] = {nullptr};
    const int n = tdeckProSleepHasUnread() ? tdeckProCollectSleepMsgs(picked) : 0;
    if (n <= 0) {
        // Said, not left blank. An empty strip reads as something failing to
        // draw; this reads as an answer, and it is the answer most of the time.
        utf8util::copyTruncate(out, cap, TR("No new messages"));
        return;
    }

    size_t used = 0;
    const int show = (n < kHomeTickerMaxMsgs) ? n : kHomeTickerMaxMsgs;
    for (int i = 0; i < show && used + 1 < cap; i++) {
        const TdeckProRecentMsg &m = *picked[i];

        char when[LIVE_CLOCK_BUF] = "";
        if (m.epoch != 0) {
            const time_t at = (time_t)m.epoch;
            struct tm lt;
            localtime_r(&at, &lt);
            liveFormatClock(lt, when, sizeof(when));
        }

        char chanFull[24];
        tdeckProSleepChannelLabel(m.chanIdx, chanFull, sizeof(chanFull));
        char chan[20];
        tdeckProClampLabel(chan, sizeof(chan), chanFull, 10);

        char sender[48];
        {
            char raw[sizeof(sender)];
            chatSenderLabel(m.senderNodeId, raw, sizeof(raw));
            // Folded before the clamp counts characters, not after: the fold is
            // what turns a three-byte codepoint into one. The strip's face has
            // no emoji fallback chained, so anything left unfolded draws blank.
            char safe[sizeof(sender)];
            renderEmojiSafeText(raw, safe, sizeof(safe));
            tdeckProClampLabel(sender, sizeof(sender), safe[0] ? safe : "?", 12);
        }

        char body[80];
        {
            char safe[sizeof(body)];
            renderEmojiSafeText(m.text, safe, sizeof(safe));
            tdeckProClampLabel(body, sizeof(body), safe, 40);
        }

        // U+2022 is in the Montserrat faces (it is one of the two non-ASCII
        // codepoints they carry), so the separator needs no fallback either.
        char one[176];
        snprintf(one, sizeof(one), "%s%s%s%s%s %s: %s",
                 (used > 0) ? "  \u2022  " : "",
                 when[0] ? when : "", when[0] ? " " : "",
                 (m.chanIdx >= 0) ? "#" : "", chan,
                 sender, body);
        used = (size_t)strlen(out);
        utf8util::copyTruncate(out + used, cap - used, one);
        used = (size_t)strlen(out);
    }
}

// Puts the ticker on the strip, or the hints back. Returns nothing, because
// every caller has already decided which screen is up.
static void homeTickerApply(bool onDashboard) {
    if (!lvObjValid(s_chatShortcutText)) return;
    lv_obj_clear_flag(s_chatShortcutText, LV_OBJ_FLAG_HIDDEN);

    if (!onDashboard) {
        s_homeTickerKey = UINT64_MAX;   // rebuild on the way back in
        lv_label_set_long_mode(s_chatShortcutText, LV_LABEL_LONG_DOT);
        lv_label_set_text(s_chatShortcutText, chatShortcutHintText());
        return;
    }

    char text[240];
    homeTickerBuildText(text, sizeof(text));
    // Only the case with something to say scrolls. "No new messages" fits the
    // strip, and a short line set to scroll circularly still drifts — motion
    // that says nothing, on the screen whose whole point is to be glanced at.
    const bool anyUnread = tdeckProSleepHasUnread();
    lv_label_set_long_mode(s_chatShortcutText,
                           anyUnread ? LV_LABEL_LONG_SCROLL_CIRCULAR
                                     : LV_LABEL_LONG_DOT);
    lv_label_set_text(s_chatShortcutText, text);
    s_homeTickerKey = tdeckProSleepUnreadKey();
}

// Called from the dashboard's own refresh, which runs on the UI tick.
static void homeTickerService() {
    if (!lvObjValid(s_chatShortcutText)) return;
    if (tdeckProSleepUnreadKey() == s_homeTickerKey) return;
    homeTickerApply(true);
}
#endif  // HAS_HOME_TICKER

static void homeDashSetFooterMode(bool onDashboard) {
    if (!lvObjValid(s_chatShortcutText)) return;
#if HAS_HOME_TICKER
    homeTickerApply(onDashboard);
#else
    // No hint line to take over: hide it on the way in, restore on the way out.
    if (onDashboard) lv_obj_add_flag(s_chatShortcutText, LV_OBJ_FLAG_HIDDEN);
    else             lv_obj_clear_flag(s_chatShortcutText, LV_OBJ_FLAG_HIDDEN);
#endif
}

// True when the bar the dashboard sits above is already showing GPS and Wi-Fi.
//
// Which board that is cannot be answered by a #if: the same build moves the
// cluster between the chat screen's top header and its bottom bar depending on
// the layout, and on the boards with a nav-bar setting it moves when the user
// flips it. What decides is simply where the labels ended up, so that is what
// this asks — walking up from the label rather than comparing against one
// parent, because the nav bar keeps its cluster inside a box of its own.
//
// The dashboard stops short of the bottom bar (see homeDashboardHeight), so a
// cluster down there stays visible underneath and a second copy in the glance
// header would be the same two icons twice on one screen. A cluster in the chat
// header is covered by the dashboard, and then the header copy is the only one.
static bool homeDashFooterShowsStatus() {
    if (!lvObjValid(s_chatShortcutBar)) return false;
    if (!lvObjValid(s_chatHeaderGps) || !lvObjValid(s_chatHeaderWifi)) return false;
    // The condensed column hides its cluster (buildNavStatusCluster()), so the
    // glance header's copy is the only one on screen.
    if (navIsSideColumn()) return false;
    for (lv_obj_t *p = lv_obj_get_parent(s_chatHeaderGps); p; p = lv_obj_get_parent(p)) {
        if (p == s_chatShortcutBar) return true;
    }
    return false;
}

static void closeHomeDashboard() {
    homeDashSetFooterMode(false);
    homeDashMarkNavBar(false);
    lvObjDeleteSafe(s_homeDash);
    // Children of the deleted object; LVGL freed them with it, so this is just
    // the bookkeeping that stops anything reaching a dangling pointer.
    s_homeGlance = GlanceHeader{};
    s_homeGlanceMinuteKey = UINT32_MAX;
    s_homeCursor = HOME_CURSOR_NONE;
    // Keeps `at` — which face to rebuild on, not a child pointer. See
    // glanceCarouselForget().
    glanceCarouselForget(s_homeCarousel);
}

// Where the shortcut bar starts, and so how much height the dashboard's content
// may take before it would run under it. Measured off the bar itself rather
// than recomputed from the chat screen's margins: those live inside buildUi()
// as locals, and a second copy of that arithmetic would be wrong the first time
// either changed.
//
// This bounds the charts, not the dashboard object — that covers the whole
// panel and the bar is drawn over it. See openHomeDashboard().
static int homeDashboardHeight() {
    const int screenH = lv_disp_get_ver_res(NULL);
    // A side column (Nav Alignment Left/Right) runs top to bottom and takes
    // width instead.
    if (navIsSideColumn()) return screenH;
    if (lvObjValid(s_chatShortcutBar) && lvObjValid(s_rootScreen)) {
        lv_obj_update_layout(s_rootScreen);
        const int barY = lv_obj_get_y(s_chatShortcutBar);
        // Sanity bounds, not a guess: a bar that has not been laid out yet
        // reports 0, and covering the whole panel is better than a dashboard
        // with no height at all.
        if (barY > 60 && barY <= screenH) return barY;
    }
    return screenH;
}

// The dashboard used to borrow the lock screen's fixed palette, because the two
// share a header and that header's inks were fixed: a themed background under
// them would have put white text on a light ground. Both move together now —
// glancePalette(themed) hands out the ground and the inks as one set — so the
// dashboard takes the theme like the rest of the UI it is part of.
//
// The card readings are the strongest text on the card, so they take the ink
// the header's wordmark and battery do. The headings below stay a step back.
// Whose ground this carousel is drawn on decides its ink, for the reason the
// header's does: the dashboard is a themed screen, the lock screen is a fixed
// black panel, and the themed ink on a light theme would be dark type on that
// black.
static inline lv_color_t glanceInk(bool themed) { return glancePalette(themed).ink; }

// The rest of the carousel's colours, which glancePalette() does not carry: the
// card slab, its edge, the chart ground and the heading ink. Both surfaces want
// the same look and disagree only about where the colour comes from.
//
// On the dashboard it comes from the theme. Every lv_color_hex() in this file
// is themedColorHex() (see the #define beside it), so 0x0E285B is really
// s_ui.panelBg and 0x335D9D really s_ui.divider -- which is right there, and is
// how the cards follow the rest of the themed UI.
//
// On the lock screen nothing may be themed. That panel is a fixed black
// whatever the theme says, and on a light theme panelBg is near-white and
// textDim a dark grey: the cards came up as pale slabs carrying grey headings,
// on black, which is what this fixes. themed=false takes the literal bytes --
// the colours these constants were authored as, and what the lock screen drew
// before the palette existed.
static inline lv_color_t glanceColor(bool themed, uint32_t rgb) {
    if (themed) return themedColorHex(rgb);
    return lv_color_make((uint8_t)((rgb >> 16) & 0xFF),
                         (uint8_t)((rgb >> 8) & 0xFF),
                         (uint8_t)(rgb & 0xFF));
}
// Headings and units, a step back from the readings themselves. One value for
// both grounds: this pale blue carries on the lock screen's black and on every
// theme ground the dashboard uses. The e-paper has no step to give -- a mid grey
// thresholds to one of the two colours it has, and which one is not ours to
// choose.
static inline lv_color_t homeDashMutedInk(bool themed) {
#if defined(DEVICE_TDECK_PRO)
    LV_UNUSED(themed);
    return sleepOverlayInk();
#else
    return glanceColor(themed, 0xA7C7FF);
#endif
}

// Whether the two cards sit beside each other or stacked. A chart narrower than
// about 140 px cannot show sixty points and a label, so the portrait panels --
// the Pro, and the Heltec/Wio boards when they are rotated -- stack instead.
// Read at build time and again by the card builder, so both agree.
static bool homeDashSideBySide() {
    return lv_disp_get_hor_res(NULL) >= 300;
}

// Every face of the dashboard's band on screen at once, stacked, rather than
// one at a time in a carousel: the CrowPanel 5"'s 800x480 has the room for all
// of them in either orientation, so there is nothing to turn. The CrowPanel
// 3.5" has it upright only: 320x480 fits the charts and both node lists, two
// across, where 480x320 does not. The lock screen does the same, its messages
// above the four widgets (buildLockCarousel()).
//
// homeDashShowAll() is whether it does, in the orientation the board booted
// in. HOME_DASH_CAN_SHOW_ALL guards the lock screen's half, which resizes a
// message budget that is a constant on the boards that never do.
#if defined(DEVICE_CROWPANEL)
#define HOME_DASH_CAN_SHOW_ALL 1
#else
#define HOME_DASH_CAN_SHOW_ALL 0
#endif
static inline bool homeDashShowAll() {
#if defined(DEVICE_CROWPANEL_50)
    return true;
#elif defined(DEVICE_CROWPANEL_35)
    return uiPortrait();
#else
    return false;
#endif
}

// Records a page as it is built. The face list is built in order rather than
// indexed by a fixed enum, because the two carousels do not carry the same set:
// the lock screen has a messages page the dashboard does not, and a wide panel
// folds the two node lists into one page where a narrow one keeps them apart.
// An index into `page[]` therefore means nothing on its own -- `kind[]` is what
// says what is there, and every refresh switches on it.
// What is in front right now. GLANCE_PAGE_CHARTS for an empty carousel is the
// harmless answer: refreshGlanceCharts() checks its own pointers.
static inline GlancePageKind glanceFacing(const GlanceCarousel &c) {
    if (c.pages == 0 || c.at >= c.pages) return GLANCE_PAGE_CHARTS;
    return (GlancePageKind)c.kind[c.at];
}

// Whether a face is worth turning to right now. Only the messages page can come
// up empty -- the charts always have their axes and a node list always has a
// heading and something to say, even if that is "Nothing heard yet" -- so it is
// the one kind that ever answers no. The dashboard has no messages page at all,
// so this is always true there and costs it one compare per turn.
static bool glancePageHasContent(const GlanceCarousel &c, int idx) {
    if (idx < 0 || idx >= (int)c.pages) return false;
    if ((GlancePageKind)c.kind[idx] != GLANCE_PAGE_MESSAGES) return true;
    return tdeckProSleepHasUnread();
}

static lv_obj_t *glanceAddPage(GlanceCarousel &c, lv_obj_t *page, GlancePageKind kind) {
    if (!page || c.pages >= kGlancePageMax) return page;
    c.page[c.pages] = page;
    c.kind[c.pages] = (uint8_t)kind;
    c.pages++;
    return page;
}

// Series colour, or the absence of one. The e-paper has two tones and spends
// both on ink and paper, so its series are black and told apart by the dash
// pattern applyTdeckProChartStyle() installs — exactly as the Tools charts do.
static inline lv_color_t homeDashSeriesColor(uint32_t litHex) {
#if defined(DEVICE_TDECK_PRO)
    LV_UNUSED(litHex);
    return lv_color_make(0, 0, 0);
#else
    return lv_color_hex(litHex);
#endif
}

// The card look, in one place: the chart pair and the two node pages are the
// same slab with different contents, and a second copy of these eleven style
// calls would drift the first time one of them was touched.
static void homeDashStyleCard(lv_obj_t *card, bool themed) {
    LV_UNUSED(themed);   // the e-paper branch below has no use for it
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
#if HAS_HOME_CAROUSEL_TAP
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);   // see buildHomeDashPage()
#endif
#if defined(DEVICE_TDECK_PRO)
    // The e-paper build draws the glance in black on white, so the modals'
    // blues would be a dark slab under black text. No fill, black rule.
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, sleepOverlayInk(), 0);
#else
    lv_obj_set_style_bg_color(card, glanceColor(themed, 0x0E285B), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_60, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, glanceColor(themed, 0x335D9D), 0);
#endif
    lv_obj_set_style_radius(card, 4, 0);
    lv_obj_set_style_pad_all(card, 3, 0);
    lv_obj_set_style_pad_row(card, 2, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
}

// One carousel page: the whole band, transparent, laid out across or down. The
// chart page and the node pages are the same frame with different cards in it,
// so they take identical padding and gaps and line up exactly when one slides
// in over the other.
static lv_obj_t *buildHomeDashPage(lv_obj_t *host, bool row) {
    lv_obj_t *page = lv_obj_create(host);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
#if HAS_HOME_CAROUSEL_TAP
    // Every lv_obj is CLICKABLE by default, which would make this page — not
    // the host below it — the object a tap lands on. Cleared so the whole band
    // reads as one tap target rather than a patchwork of them.
    lv_obj_clear_flag(page, LV_OBJ_FLAG_CLICKABLE);
#endif
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, kTdeckProBandInset / 2, 0);
    lv_obj_set_style_pad_column(page, 4, 0);
    lv_obj_set_style_pad_row(page, 4, 0);
    lv_obj_set_flex_flow(page, row ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    return page;
}

// One of the two chart cards. Returns the card; the chart and its value label
// come back through the out-parameters so the caller keeps the pointers it
// needs and this keeps the styling in one place.
static lv_obj_t *buildGlanceChartCard(GlanceCarousel &c, lv_obj_t *row, const char *title,
                                      lv_obj_t **chartOut, lv_obj_t **valueOut) {
    lv_obj_t *card = lv_obj_create(row);
    // Grows along whichever axis the row runs, so one card definition serves
    // both the side-by-side and the stacked arrangement.
    lv_obj_set_flex_grow(card, 1);
    if (homeDashSideBySide()) lv_obj_set_height(card, lv_pct(100));
    else                      lv_obj_set_width(card, lv_pct(100));
    homeDashStyleCard(card, c.themed);

    // Side by side, the card is tall and narrow: heading, chart, value, three
    // rows. Stacked, it is short and wide and has no third row to spare -- on a
    // 240x320 panel the header eats 144 px and two cards split what is left, so
    // a row costs the chart a third of its height. There the heading and the
    // reading share one line instead, which is what the extra width is for.
    lv_obj_t *labelRow = card;
    if (!homeDashSideBySide()) {
        labelRow = lv_obj_create(card);
        lv_obj_set_width(labelRow, lv_pct(100));
        lv_obj_set_height(labelRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(labelRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(labelRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(labelRow, 0, 0);
        lv_obj_set_style_pad_all(labelRow, 0, 0);
        lv_obj_set_style_pad_column(labelRow, 6, 0);
        lv_obj_set_flex_flow(labelRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(labelRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
    }

    lv_obj_t *heading = lv_label_create(labelRow);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(heading, homeDashMutedInk(c.themed), 0);
    lv_label_set_text(heading, title);

    lv_obj_t *chart = lv_chart_create(card);
#if HAS_HOME_CAROUSEL_TAP
    // lv_chart does not clear CLICKABLE the way lv_label does, so without this
    // the two charts would be holes in the band's tap target.
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
#endif
    lv_obj_set_width(chart, lv_pct(100));
    // Sits below the labels in both arrangements, because LVGL appends children
    // and they were made first. The only one that grows, too: the label row is
    // one text line whichever way it is built, so whatever height the card turns
    // out to have lands here.
    lv_obj_set_flex_grow(chart, 1);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, ChartHist::CAP);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(chart, 2, 0);
    lv_obj_set_style_bg_color(chart, glanceColor(c.themed, 0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chart, glanceColor(c.themed, 0x335D9D), 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_line_color(chart, glanceColor(c.themed, 0x335D9D), LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, LV_OPA_40, LV_PART_MAIN);
#if defined(DEVICE_TDECK_PRO)
    // Overrides the block above wholesale — white ground, black rules, and the
    // dashed/solid series distinction the 1-bit panel needs in place of colour.
    // The same helper the Tools charts use, so the two read identically.
    applyTdeckProChartStyle(chart);
#endif
    // Fewer division lines than the Tools modals draw. Those have a y-axis
    // scale beside them to label the lines; at this size there is no room for
    // one, so extra lines would be decoration over a chart that is already
    // small.
    lv_chart_set_div_line_count(chart, 3, 4);

    lv_obj_t *value = lv_label_create(labelRow);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(value, glanceInk(c.themed), 0);
    lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
    lv_label_set_text(value, "--");
    if (labelRow == card) {
        lv_obj_set_width(value, lv_pct(100));
    } else {
        // Takes the rest of the row, so the reading ends at the card's right
        // edge rather than wherever the heading happens to stop.
        lv_obj_set_flex_grow(value, 1);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    }

    *chartOut = chart;
    *valueOut = value;
    return card;
}

static void refreshGlanceCharts(GlanceCarousel &c, bool force) {
    if (!lvObjValid(c.chUtilChart) || !lvObjValid(c.snrChart)) return;

    // Right-aligns the newest sample against the right edge, so a ring that is
    // not yet full grows in from the right rather than sitting in the left
    // corner and jumping once it wraps.
    auto fill = [](lv_obj_t *chart, lv_chart_series_t *series, const ChartHist &h,
                   int16_t lo, int16_t hi) {
        if (!series) return;
        const int offset = ChartHist::CAP - h.count;
        for (int i = 0; i < ChartHist::CAP; i++) {
            const int sampleIdx = i - offset;
            if (sampleIdx >= 0 && sampleIdx < h.count) {
                lv_chart_set_value_by_id(chart, series, i,
                                         chartClampInt(chartSampleAt(h, sampleIdx), lo, hi));
            } else {
                lv_chart_set_value_by_id(chart, series, i, LV_CHART_POINT_NONE);
            }
        }
    };

    if (force || c.chUtilSeq != s_chUtilHist.seq
        || c.airUtilSeq != s_airUtilHist.seq) {
        fill(c.chUtilChart, c.chUtilSeries, s_chUtilHist, 0, 100);
        fill(c.chUtilChart, c.airUtilSeries, s_airUtilHist, 0, 100);
        lv_chart_refresh(c.chUtilChart);
        c.chUtilSeq = s_chUtilHist.seq;
        c.airUtilSeq = s_airUtilHist.seq;

        if (lvObjValid(c.chUtilValue)) {
            char ch[12] = "--", air[12] = "--";
            if (s_chUtilHist.hasLast) snprintf(ch, sizeof(ch), "%.0f%%", (double)s_chUtilHist.lastVal);
            if (s_airUtilHist.hasLast) snprintf(air, sizeof(air), "%.0f%%", (double)s_airUtilHist.lastVal);
            char text[40];
            snprintf(text, sizeof(text), TR("ch %s   air %s"), ch, air);
            lv_label_set_text(c.chUtilValue, text);
        }
    }

    if (force || c.snrSeq != s_snrHist.seq
        || c.rssiSeq != s_rssiHist.seq) {
        fill(c.snrChart, c.snrSeries, s_snrHist, -25, 15);
        fill(c.snrChart, c.rssiSeries, s_rssiHist, -130, -30);
        lv_chart_refresh(c.snrChart);
        c.snrSeq = s_snrHist.seq;
        c.rssiSeq = s_rssiHist.seq;

        if (lvObjValid(c.snrValue)) {
            char snr[12] = "--", rssi[12] = "--";
            if (s_snrHist.hasLast) snprintf(snr, sizeof(snr), "%.1f", (double)s_snrHist.lastVal);
            if (s_rssiHist.hasLast) snprintf(rssi, sizeof(rssi), "%.0f", (double)s_rssiHist.lastVal);
            char text[40];
            snprintf(text, sizeof(text), TR("snr %s   rssi %s"), snr, rssi);
            lv_label_set_text(c.snrValue, text);
        }
    }
}

// ---- Node pages -----------------------------------------------------------

// Compact relative age. The two node pages are lists of "how long ago", so the
// absolute local timestamp deviceInfoFormatHeard() switches to once NTP has
// landed is both wider than the column and the wrong question on this surface.
static void homeDashFormatAge(uint32_t lastHeardMs, char *out, size_t cap) {
    // Signed delta: millis() wraps at 49 days, and a stamp taken before the
    // wrap has to read as old rather than as a preposterous age.
    const int32_t delta = (int32_t)(millis() - lastHeardMs);
    const uint32_t ageS = (delta <= 0) ? 0u : ((uint32_t)delta / 1000UL);
    if (ageS < 60UL)         snprintf(out, cap, "%lus", (unsigned long)ageS);
    else if (ageS < 3600UL)  snprintf(out, cap, "%lum", (unsigned long)(ageS / 60UL));
    else if (ageS < 86400UL) snprintf(out, cap, "%luh", (unsigned long)(ageS / 3600UL));
    else                     snprintf(out, cap, "%lud", (unsigned long)(ageS / 86400UL));
}

// The name to show for a node: its advertised long name when it has sent one,
// otherwise the short-name/hex label every other screen falls back to. Same
// order discoveryNodeLabel() and beaconsSenderLabel() use, and the same
// utf8util::copyTruncate — a name can carry multi-byte characters, and cutting
// one in half renders as a replacement glyph.
static const char *homeDashNodeLabel(const NodeEntry *e, char *buf, size_t cap) {
    if (e && e->hasName && e->longName[0]) {
        utf8util::copyTruncate(buf, cap, e->longName);
        return buf;
    }
    return deviceInfoNodeLabel(e, buf, cap);
}

// Faces for the node cards' rows and heading. P4 portrait gives each card the
// full width of a tall panel, so it reads them larger; everywhere else the
// band is short and these stay at the size its charts use.
static const lv_font_t *glanceNodeRowFont() {
#if defined(DEVICE_TDISPLAY_P4) && !UI_LARGE_PANEL_PROFILE
    if (uiPortrait()) return &lv_font_montserrat_14;
#endif
    return &lv_font_montserrat_10;
}

static const lv_font_t *glanceNodeHeadingFont() {
#if defined(DEVICE_TDISPLAY_P4) && !UI_LARGE_PANEL_PROFILE
    if (uiPortrait()) return &lv_font_montserrat_12;
#endif
    return &lv_font_montserrat_10;
}

// How many rows the band holds, from the height it actually got rather than a
// per-board constant: the same code runs against a 222 px Pager band and a
// portrait Pro one, and the answer differs by more than a row.
static int glanceNodeRowCapacity(int pageH) {
    const int lineH = (int)lv_font_get_line_height(glanceNodeRowFont());
    const int headH = (int)lv_font_get_line_height(glanceNodeHeadingFont());
    if (lineH <= 0) return 0;
    // Card padding top and bottom, the heading line, and the flex gap under it.
    const int avail = pageH - (2 * 3) - headH - 2;
    if (avail < lineH) return 0;
    int rows = avail / (lineH + 2);
    if (rows > kHomeDashNodeRowsMax) rows = kHomeDashNodeRowsMax;
    return (rows < 0) ? 0 : rows;
}

// The two faces read one field from opposite ends: lastHeardMs, which is set
// when a packet actually arrived. Entries restored from NVS carry 0 -- "not
// heard since boot" -- and are skipped rather than sorted to the front of
// "longest silent", where nodes we have simply not met yet would crowd out the
// real answer. Same rule buildDeviceInfoLines() applies to its Newest/Oldest
// pair, so the two surfaces cannot disagree about what "heard" means.
//
// Selection scan rather than a sort: `want` is a dozen at the very most against
// a 250-slot table, and this needs no scratch array to sort in.
static int homeDashCollectNodes(bool oldest, const NodeEntry **out, int want) {
    int n = 0;
    const int total = Nodes.count();
    for (int pick = 0; pick < want; pick++) {
        const NodeEntry *best = nullptr;
        for (int i = 0; i < total; i++) {
            // at(), not getByRank(): rank sorts favourites to the top, which
            // would quietly bias both pages towards pinned nodes.
            const NodeEntry *e = Nodes.at(i);
            if (!e || e->nodeId == 0 || e->lastHeardMs == 0) continue;
            bool taken = false;
            for (int j = 0; j < n; j++) {
                if (out[j] == e) { taken = true; break; }
            }
            if (taken) continue;
            if (!best) { best = e; continue; }
            const int32_t d = (int32_t)(e->lastHeardMs - best->lastHeardMs);
            if (oldest ? (d < 0) : (d > 0)) best = e;
        }
        if (!best) break;
        out[n++] = best;
    }
    return n;
}

// One node list as a card. `listIdx` is 0 for the recent list and 1 for the
// oldest, and indexes the row arrays regardless of which page it ends up on --
// on a wide panel both live on one page, on a narrow one they have a page each.
// `row` says which way the page runs, and so which axis the card grows along;
// `cardH` is the height it will actually get, which is what decides its rows.
static void buildGlanceNodeCard(GlanceCarousel &c, lv_obj_t *page, int listIdx,
                                const char *title, bool row, int cardH) {
    lv_obj_t *card = lv_obj_create(page);
    // Same growth rule the chart cards use, so one card definition serves the
    // side-by-side and the stacked arrangement alike.
    lv_obj_set_flex_grow(card, 1);
    if (row) lv_obj_set_height(card, lv_pct(100));
    else     lv_obj_set_width(card, lv_pct(100));
    homeDashStyleCard(card, c.themed);
    c.nodeCard[listIdx] = card;

    lv_obj_t *heading = lv_label_create(card);
    lv_obj_set_width(heading, lv_pct(100));
    lv_obj_set_style_text_font(heading, glanceNodeHeadingFont(), 0);
    lv_obj_set_style_text_color(heading, homeDashMutedInk(c.themed), 0);
    lv_label_set_text(heading, title);

    const lv_font_t *rowFont = glanceNodeRowFont();
    const int rows = glanceNodeRowCapacity(cardH);
    c.nodeRows[listIdx] = rows;
    for (int i = 0; i < rows; i++) {
        lv_obj_t *rowObj = lv_obj_create(card);
        lv_obj_set_width(rowObj, lv_pct(100));
        lv_obj_set_height(rowObj, LV_SIZE_CONTENT);
        lv_obj_clear_flag(rowObj, LV_OBJ_FLAG_SCROLLABLE);
#if HAS_HOME_CAROUSEL_TAP
        lv_obj_clear_flag(rowObj, LV_OBJ_FLAG_CLICKABLE);   // see buildHomeDashPage()
#endif
        lv_obj_set_style_bg_opa(rowObj, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(rowObj, 0, 0);
        lv_obj_set_style_pad_all(rowObj, 0, 0);
        lv_obj_set_style_pad_column(rowObj, 4, 0);
        lv_obj_set_flex_flow(rowObj, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(rowObj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        // Takes whatever the age leaves, and ellipsizes rather than clipping:
        // a name cut mid-word with no mark reads as data loss, "Kitchen Sen..."
        // reads as a long name.
        lv_obj_t *nameLbl = lv_label_create(rowObj);
        // grow alone, no explicit width: that is how buildGlanceChartCard() sizes
        // its reading label, and the flex pass is what gives LONG_DOT the
        // bounded width it needs to know where to ellipsize.
        lv_obj_set_flex_grow(nameLbl, 1);
        lv_obj_set_style_text_font(nameLbl, rowFont, 0);
        lv_obj_set_style_text_color(nameLbl, glanceInk(c.themed), 0);
        lv_label_set_long_mode(nameLbl, LV_LABEL_LONG_DOT);
        lv_label_set_text(nameLbl, "");

        // Sized to its own text, so it is never the thing that gets squeezed.
        lv_obj_t *ageLbl = lv_label_create(rowObj);
        lv_obj_set_style_text_font(ageLbl, rowFont, 0);
        lv_obj_set_style_text_color(ageLbl, homeDashMutedInk(c.themed), 0);
        lv_label_set_long_mode(ageLbl, LV_LABEL_LONG_CLIP);
        lv_label_set_text(ageLbl, "");

        c.nodeRow[listIdx][i] = nameLbl;
        c.nodeAge[listIdx][i] = ageLbl;
    }
}

// Always refills: every caller has already decided this is worth doing, and the
// page in front is the only one that is ever asked.
static void refreshGlanceNodeList(GlanceCarousel &c, int listIdx) {
    if (listIdx < 0 || listIdx > 1) return;
    const int rows = c.nodeRows[listIdx];
    if (rows <= 0) return;

    const NodeEntry *picked[kHomeDashNodeRowsMax] = {};
    const int n = homeDashCollectNodes(/*oldest=*/listIdx == 1, picked, rows);
    for (int i = 0; i < rows; i++) {
        lv_obj_t *row = c.nodeRow[listIdx][i];
        lv_obj_t *ageObj = c.nodeAge[listIdx][i];
        if (!lvObjValid(row)) continue;
        if (i >= n) {
            // Said once, on the first empty row, rather than left blank: an
            // empty page reads as something that failed to load.
            lv_label_set_text(row, (i == 0) ? TR("Nothing heard yet") : "");
            if (lvObjValid(ageObj)) lv_label_set_text(ageObj, "");
            continue;
        }
        // Sized for a full long name (NodeEntry::longName is 40 bytes), not the
        // 12 a hex fallback needs.
        char nameBuf[48], age[12];
        lv_label_set_text(row, homeDashNodeLabel(picked[i], nameBuf, sizeof(nameBuf)));
        homeDashFormatAge(picked[i]->lastHeardMs, age, sizeof(age));
        if (lvObjValid(ageObj)) lv_label_set_text(ageObj, age);
    }
}

// The chart face: both cards and all four series in one call. Two carousels
// build it identically -- only the ground under it differs -- and a second copy
// of the ranges and the series colours would drift the first time either moved.
// Opens what a dashboard target stands for. The cursor is dropped on the way
// out, so coming back finds the dashboard as it is normally left: nothing
// under a cursor.
static void homeDashOpen(HomeDashCursor target) {
    homeDashSetCursor(HOME_CURSOR_NONE);
    switch (target) {
#if HAS_WEATHER
        case HOME_CURSOR_WEATHER: openWeatherModal();      break;
#endif
        case HOME_CURSOR_CHUTIL:  openChUtilChartModal();  break;
        case HOME_CURSOR_SNR:     openSnrRssiChartModal(); break;
        case HOME_CURSOR_NODES_RECENT:
        case HOME_CURSOR_NODES_OLDEST:
                                  openNodesModal();        break;
        default: break;
    }
}

static void homeDashCardClickCb(lv_event_t *e) {
    // Same guard as the weather's: a swipe across the band is the carousel's.
    if (lv_indev_get_gesture_dir(lv_indev_active()) != LV_DIR_NONE) return;
    homeDashOpen((HomeDashCursor)(uintptr_t)lv_event_get_user_data(e));
}

// Makes a band card (a chart, or a node list) one tap target. Everything inside it is made unclickable
// so the tap lands on the card whichever part of it was touched -- lv_chart
// and the stacked layout's label row are both CLICKABLE by default and would
// otherwise take it. The card itself is made clickable again because the Pro's
// band-wide tap clears it (homeDashStyleCard); on that board a tap on a card
// now opens what it stands for, and the gaps around the cards still turn the
// band.
static void homeDashMakeCardTarget(lv_obj_t *card, lv_obj_t *chart,
                                   HomeDashCursor which) {
    if (!lvObjValid(card)) return;
    const uint32_t n = lv_obj_get_child_count(card);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_clear_flag(lv_obj_get_child(card, (int32_t)i), LV_OBJ_FLAG_CLICKABLE);
    }
    if (lvObjValid(chart)) lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_outline_color(card, homeDashCursorInk(), 0);
    lv_obj_set_style_outline_width(card, 0, 0);
    lv_obj_add_event_cb(card, homeDashCardClickCb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)which);
}

static void glanceBuildChartPage(GlanceCarousel &c, lv_obj_t *host) {
    lv_obj_t *page = glanceAddPage(c, buildHomeDashPage(host, homeDashSideBySide()),
                                   GLANCE_PAGE_CHARTS);
    if (!page) return;

    c.chUtilCard = buildGlanceChartCard(c, page, TR("CHANNEL UTIL"),
                                        &c.chUtilChart, &c.chUtilValue);
    // Same colours the Tools modal uses for the same two series, so the legend
    // someone learned there still reads here.
    c.chUtilSeries = lv_chart_add_series(c.chUtilChart, homeDashSeriesColor(0x4FD1C5),
                                         LV_CHART_AXIS_PRIMARY_Y);
    c.airUtilSeries = lv_chart_add_series(c.chUtilChart, homeDashSeriesColor(0xF6AD55),
                                          LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_range(c.chUtilChart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    if (c.chUtilSeries) {
        lv_chart_set_all_value(c.chUtilChart, c.chUtilSeries, LV_CHART_POINT_NONE);
    }
    if (c.airUtilSeries) {
        lv_chart_set_all_value(c.chUtilChart, c.airUtilSeries, LV_CHART_POINT_NONE);
    }

    c.snrCard = buildGlanceChartCard(c, page, "SNR / RSSI", &c.snrChart, &c.snrValue);
    c.snrSeries = lv_chart_add_series(c.snrChart, homeDashSeriesColor(0x68D391),
                                      LV_CHART_AXIS_PRIMARY_Y);
    c.rssiSeries = lv_chart_add_series(c.snrChart, homeDashSeriesColor(0xF687B3),
                                       LV_CHART_AXIS_SECONDARY_Y);
    lv_chart_set_range(c.snrChart, LV_CHART_AXIS_PRIMARY_Y, -25, 15);
    lv_chart_set_range(c.snrChart, LV_CHART_AXIS_SECONDARY_Y, -130, -30);
    if (c.snrSeries) {
        lv_chart_set_all_value(c.snrChart, c.snrSeries, LV_CHART_POINT_NONE);
    }
    if (c.rssiSeries) {
        lv_chart_set_all_value(c.snrChart, c.rssiSeries, LV_CHART_POINT_NONE);
    }

    // The dashboard's cards open their Tools charts; the lock screen's take no
    // input (every press there dismisses it), so they stay as they are.
    if (&c == &s_homeCarousel) {
        homeDashMakeCardTarget(c.chUtilCard, c.chUtilChart, HOME_CURSOR_CHUTIL);
        homeDashMakeCardTarget(c.snrCard, c.snrChart, HOME_CURSOR_SNR);
    }
}

// The node faces. One page carrying both lists where the panel is wide enough
// to halve -- same height, half the width each, so twice as many rows are on
// screen at once instead of costing a turn to reach the second half -- and two
// pages of one list each where it is not. The same call homeDashSideBySide()
// makes for the charts, and for the same reason.
static void glanceBuildNodePages(GlanceCarousel &c, lv_obj_t *host, int cardH) {
#if defined(DEVICE_TDISPLAY_P4)
    // The third shape: portrait on this panel is too narrow to halve across and
    // tall enough to halve down, so both lists share one page as two rows. The
    // page's 4 px row gap comes out of the two halves.
    if (uiPortrait()) {
        lv_obj_t *page = buildHomeDashPage(host, false);
        const int halfH = (cardH - 4) / 2;
        buildGlanceNodeCard(c, page, 0, TR("RECENTLY HEARD"), false, halfH);
        buildGlanceNodeCard(c, page, 1, TR("LONGEST SILENT"), false, halfH);
        glanceAddPage(c, page, GLANCE_PAGE_BOTH_LISTS);
        return;
    }
#endif
    // P4 landscape Home included: the header spans the screen there now, so the
    // band gets the full width and the two lists share one page again.
    if (homeDashSideBySide()) {
        lv_obj_t *page = buildHomeDashPage(host, true);
        buildGlanceNodeCard(c, page, 0, TR("RECENTLY HEARD"), true, cardH);
        buildGlanceNodeCard(c, page, 1, TR("LONGEST SILENT"), true, cardH);
        glanceAddPage(c, page, GLANCE_PAGE_BOTH_LISTS);
    } else {
        lv_obj_t *recent = buildHomeDashPage(host, false);
        buildGlanceNodeCard(c, recent, 0, TR("RECENTLY HEARD"), false, cardH);
        glanceAddPage(c, recent, GLANCE_PAGE_RECENT);

        lv_obj_t *oldest = buildHomeDashPage(host, false);
        buildGlanceNodeCard(c, oldest, 1, TR("LONGEST SILENT"), false, cardH);
        glanceAddPage(c, oldest, GLANCE_PAGE_OLDEST);
    }
}

// Repaint whatever the page at `idx` is carrying, whichever face that turned
// out to be on this panel.
static void refreshGlancePage(GlanceCarousel &c, int idx) {
    if (idx < 0 || idx >= (int)c.pages) return;
    switch ((GlancePageKind)c.kind[idx]) {
        case GLANCE_PAGE_CHARTS:
            refreshGlanceCharts(c, true);
            break;
        case GLANCE_PAGE_RECENT:
            refreshGlanceNodeList(c, 0);
            break;
        case GLANCE_PAGE_OLDEST:
            refreshGlanceNodeList(c, 1);
            break;
        case GLANCE_PAGE_BOTH_LISTS:
            refreshGlanceNodeList(c, 0);
            refreshGlanceNodeList(c, 1);
            break;
        case GLANCE_PAGE_MESSAGES:
#if FEATURE_LOCK_SCREEN
            // The overlay's own rows, reparented onto this page rather than
            // copied: one message ring, one set of labels, one place that knows
            // how to lay a message out across two lines.
            tdeckProRefreshSleepMsgRows();
#endif
            break;
    }
}

// A page turn is the one moment this surface has anything worth animating: a
// slide says which way the carousel went, and a hard cut cannot. Every board
// gets it except the Pro, where each animated frame is a full e-paper refresh —
// the same reason HAS_HOME_TICKER leaves that board out of the scrolling ticker.
// There the swap stays instant, which on a panel that repaints in ~700 ms is
// not a downgrade but the only sane behaviour.
#if defined(DEVICE_TDECK_PRO)
#define HAS_HOME_CAROUSEL_ANIM 0
#else
#define HAS_HOME_CAROUSEL_ANIM 1
#endif

#if HAS_HOME_CAROUSEL_ANIM
// Long enough to read the direction off, short enough that it is a response
// rather than a transition to sit through.
static constexpr uint32_t kHomeDashSlideMs = 180;

static void homeDashSlideXCb(void *obj, int32_t v) {
    lv_obj_t *o = (lv_obj_t *)obj;
    if (lvObjValid(o)) lv_obj_set_x(o, v);
}

// The page that just left, once it is off screen: hidden, and put back at x=0
// so the next turn finds every page where it expects to.
static void homeDashSlideDone(lv_anim_t *a) {
    lv_obj_t *o = (lv_obj_t *)a->var;
    if (!lvObjValid(o)) return;
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(o, 0);
}
#endif

// The instant setter, and the thing that settles a slide still in flight: every
// page ends at x=0 with exactly one of them visible. Called before starting a
// turn, so a fast double swipe lands somewhere sane rather than leaving a page
// parked half off screen.
//
// No anim teardown needed when either surface goes away: lv_obj's destructor
// runs lv_anim_delete(obj, NULL) on every object it frees, children included.
static void glanceShowPage(GlanceCarousel &c, uint8_t page) {
    if (c.pages == 0) return;
    if (page >= c.pages) page = 0;
    c.at = page;
    for (int i = 0; i < (int)c.pages; i++) {
        lv_obj_t *o = c.page[i];
        if (!lvObjValid(o)) continue;
#if HAS_HOME_CAROUSEL_ANIM
        // Deleting an animation does not fire its completed_cb, so this cannot
        // re-enter through homeDashSlideDone().
        lv_anim_delete(o, homeDashSlideXCb);
        lv_obj_set_x(o, 0);
#endif
        if (i == (int)page) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
        else                lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

// One step round the carousel: +1 is the way a rightward swipe goes, -1 the
// other way. Wrapping is the point -- a ring with no ends means neither
// direction ever dead-stops.
static void glanceCarouselGo(GlanceCarousel &c, int delta) {
    if (!lvObjValid(c.host) || c.pages == 0) return;
    const int pages = (int)c.pages;

    // Steps until it finds a face with something on it, rather than stepping
    // once. The messages page is empty whenever nothing is unread, and turning
    // onto a blank band wastes the turn -- on the lock screen, where the turn
    // is the whole interaction, it wastes thirty seconds of it. Bounded by the
    // page count, so if nothing anywhere has anything to show this lands back
    // where it started instead of spinning.
    int next = (int)c.at;
    for (int step = 0; step < pages; step++) {
        next = (next + delta) % pages;
        if (next < 0) next += pages;
        if (glancePageHasContent(c, next)) break;
    }
    if (next == (int)c.at) return;

    // Filled before it is shown, so the page arrives already carrying its
    // content instead of sliding in blank and populating a frame later. Painted
    // now rather than at the minute tick the header runs on: this is a direct
    // answer to a gesture, and on the Pro a page turn is the one thing an
    // e-paper refresh here is actually worth.
    refreshGlancePage(c, next);

#if HAS_HOME_CAROUSEL_ANIM
    lv_obj_t *from = c.page[c.at];
    lv_obj_t *to   = c.page[next];
    // The host's width is the slide distance, so it has to be a real number
    // before the animation is built — right after a rebuild it is not yet.
    lv_obj_update_layout(c.host);
    const int32_t w = lv_obj_get_width(c.host);
    if (w > 0 && lvObjValid(from) && lvObjValid(to)) {
        glanceShowPage(c, c.at);   // settle anything mid-flight

        // The motion follows the finger: a rightward swipe carries the page you
        // were on out to the right and brings the next one in from the left.
        // Children are clipped to the host, so neither is ever drawn outside
        // the band.
        const int32_t outTo  = (delta > 0) ? w : -w;
        const int32_t inFrom = -outTo;

        lv_obj_set_x(to, inFrom);
        lv_obj_clear_flag(to, LV_OBJ_FLAG_HIDDEN);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_duration(&a, kHomeDashSlideMs);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, homeDashSlideXCb);

        lv_anim_set_var(&a, from);
        lv_anim_set_values(&a, 0, outTo);
        lv_anim_set_completed_cb(&a, homeDashSlideDone);
        lv_anim_start(&a);

        lv_anim_set_var(&a, to);
        lv_anim_set_values(&a, inFrom, 0);
        lv_anim_set_completed_cb(&a, nullptr);
        lv_anim_start(&a);

        // Set directly rather than through glanceShowPage(): that would hide
        // the outgoing page this instant and there would be nothing to watch.
        c.at = (uint8_t)next;
        return;
    }
#endif
    glanceShowPage(c, (uint8_t)next);
}

// What the dashboard's input paths drive. The lock screen's copy is turned by
// serviceLockCarousel() and by nothing else.
static void homeDashCarouselGo(int delta) {
    if (!homeDashboardVisible()) return;
    if (homeDashShowAll()) return;   // every face is already showing
    // The cards go with the page. A swipe gets here without passing through
    // homeDashCursorKey(), so the outline would otherwise ride off on a face
    // that is no longer in front.
    if (s_homeCursor != HOME_CURSOR_NONE && s_homeCursor != HOME_CURSOR_WEATHER) {
        homeDashSetCursor(HOME_CURSOR_NONE);
    }
    glanceCarouselGo(s_homeCarousel, delta);
}

#if HAS_HOME_CAROUSEL_TAP
static void homeDashTapCb(lv_event_t *e) {
    LV_UNUSED(e);
    homeDashCarouselGo(+1);
}
#endif

static void homeDashGestureCb(lv_event_t *e) {
    LV_UNUSED(e);
    const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    // Vertical gestures are deliberately left alone. Nothing on this band
    // scrolls, but claiming them would swallow whatever is put here next.
    if (dir == LV_DIR_RIGHT)     homeDashCarouselGo(+1);
    else if (dir == LV_DIR_LEFT) homeDashCarouselGo(-1);
}

#if HAS_WEATHER
// ---- Weather shortcut -------------------------------------------------------
// The conditions in the header open the Weather screen: a tap on the touch
// boards, and on the rest Up to put the cursor over them and Enter to go in.
// Only while there is a reading to point at -- with the reading hidden there
// is nothing on screen to tap or to put a cursor over, so Up keeps its
// carousel meaning.
//
// The target is a box of its own rather than the labels made clickable: the
// reading is two labels in the side column and one in the P4's column below,
// and the cursor should be one outline around all of it, not one per label.
// Labels are not hit-tested unless clickable, so taps on the text land here.

static bool homeDashWxShown() {
    const GlanceHeader &w = s_homeGlance;
    if (!lvObjValid(w.wxHit)) return false;
    if (w.wxBelow) {
        return lvObjValid(w.wxBelowText)
            && !lv_obj_has_flag(w.wxBelowText, LV_OBJ_FLAG_HIDDEN);
    }
    return lvObjValid(w.wxTemp) && !lv_obj_has_flag(w.wxTemp, LV_OBJ_FLAG_HIDDEN);
}

// Fits the box to the reading, or hides it when there is none. Run after every
// header repaint, since that is what shows, hides and rewrites the labels.
static void homeDashPlaceWxHit() {
    GlanceHeader &w = s_homeGlance;
    if (!lvObjValid(w.wxHit)) return;
    if (!homeDashWxShown()) {
        lv_obj_add_flag(w.wxHit, LV_OBJ_FLAG_HIDDEN);
        if (s_homeCursor == HOME_CURSOR_WEATHER) homeDashSetCursor(HOME_CURSOR_NONE);
        return;
    }

    // The labels were just rewritten, so their sizes are stale until a layout
    // pass. Once a minute, on the header's own cadence.
    lv_obj_update_layout(w.wxHit);
    lv_area_t box;
    if (w.wxBelow) {
        lv_obj_get_coords(w.wxBelowText, &box);
    } else {
        lv_area_t temp;
        lv_obj_get_coords(w.wxDesc, &box);
        lv_obj_get_coords(w.wxTemp, &temp);
        box.x1 = min(box.x1, temp.x1);
        box.y1 = min(box.y1, temp.y1);
        box.x2 = max(box.x2, temp.x2);
        box.y2 = max(box.y2, temp.y2);
        // The wide hero's detail block is part of the same reading.
        if (lvObjValid(w.wxDetail) && !lv_obj_has_flag(w.wxDetail, LV_OBJ_FLAG_HIDDEN)) {
            lv_area_t det;
            lv_obj_get_coords(w.wxDetail, &det);
            box.x1 = min(box.x1, det.x1);
            box.y1 = min(box.y1, det.y1);
            box.x2 = max(box.x2, det.x2);
            box.y2 = max(box.y2, det.y2);
        }
    }
    // Relative to the header's parent, which has no padding of its own (both
    // candidates are lv_obj_remove_style_all()'d), so coords map straight across.
    lv_area_t parent;
    lv_obj_get_coords(lv_obj_get_parent(w.wxHit), &parent);
    constexpr int kPad = 4;   // clears the 2 px cursor off the glyphs
    const int x = box.x1 - parent.x1 - kPad;
    const int y = box.y1 - parent.y1 - kPad;
    const int bw = lv_area_get_width(&box) + (2 * kPad);
    const int bh = lv_area_get_height(&box) + (2 * kPad);
    // Only on a change: a set invalidates, and on the Pro's e-paper that is a
    // refresh spent on a box that did not move.
    if (lv_obj_get_x(w.wxHit) != x || lv_obj_get_y(w.wxHit) != y) {
        lv_obj_set_pos(w.wxHit, x, y);
    }
    if (lv_obj_get_width(w.wxHit) != bw || lv_obj_get_height(w.wxHit) != bh) {
        lv_obj_set_size(w.wxHit, bw, bh);
    }
    lv_obj_clear_flag(w.wxHit, LV_OBJ_FLAG_HIDDEN);
}

static void homeDashWxClickCb(lv_event_t *e) {
    LV_UNUSED(e);
    // A swipe that happened to start on the reading is the carousel's, which
    // the gesture bubbled up to already. Not also a tap.
    if (lv_indev_get_gesture_dir(lv_indev_active()) != LV_DIR_NONE) return;
    homeDashOpen(HOME_CURSOR_WEATHER);
}

static void buildHomeDashWxHit(lv_obj_t *parent) {
    lv_obj_t *hit = lv_obj_create(parent);
    lv_obj_remove_style_all(hit);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(hit, 4, 0);
    lv_obj_set_style_border_width(hit, 0, 0);
    lv_obj_set_style_border_color(hit, homeDashCursorInk(), 0);
    lv_obj_add_event_cb(hit, homeDashWxClickCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_HIDDEN);   // until a reading places it
    s_homeGlance.wxHit = hit;
}

#endif  // HAS_WEATHER

// The weather, when there is a reading on screen to put the cursor on.
static bool homeDashCanFocusWeather() {
#if HAS_WEATHER
    return homeDashWxShown();
#else
    return false;
#endif
}

// Where Down lands: the first card on the band's face in front -- Channel Util
// on the charts, the list on a one-list page, the recent list where both share
// a page. NONE if that card was never built (a panel too short for the band).
static HomeDashCursor homeDashBandFirst() {
    const GlanceCarousel &c = s_homeCarousel;
    switch (glanceFacing(c)) {
        case GLANCE_PAGE_CHARTS:
            return lvObjValid(c.chUtilCard) ? HOME_CURSOR_CHUTIL : HOME_CURSOR_NONE;
        case GLANCE_PAGE_RECENT:
        case GLANCE_PAGE_BOTH_LISTS:
            return lvObjValid(c.nodeCard[0]) ? HOME_CURSOR_NODES_RECENT : HOME_CURSOR_NONE;
        case GLANCE_PAGE_OLDEST:
            return lvObjValid(c.nodeCard[1]) ? HOME_CURSOR_NODES_OLDEST : HOME_CURSOR_NONE;
        default:
            return HOME_CURSOR_NONE;
    }
}

// The card beside the one under the cursor, in the direction pressed, or NONE
// at the end of the pair. The node lists only have a neighbour where both are
// on one page; apart, each is its own face and Left/Right turn the band.
static HomeDashCursor homeDashBandStep(HomeDashCursor from, bool right) {
    const bool bothLists = glanceFacing(s_homeCarousel) == GLANCE_PAGE_BOTH_LISTS;
    if (right) {
        if (from == HOME_CURSOR_CHUTIL) return HOME_CURSOR_SNR;
        if (from == HOME_CURSOR_NODES_RECENT && bothLists) return HOME_CURSOR_NODES_OLDEST;
    } else {
        if (from == HOME_CURSOR_SNR) return HOME_CURSOR_CHUTIL;
        if (from == HOME_CURSOR_NODES_OLDEST && bothLists) return HOME_CURSOR_NODES_RECENT;
    }
    return HOME_CURSOR_NONE;
}

// The dashboard's keys, ahead of the carousel's. Returns true when the key was
// the cursor's to take; false leaves it to the carousel below.
//
// Two rows: the weather on top, the band's cards below. Up and Down move
// between the rows, and moving off either end -- Up from the weather, Down
// from the cards -- puts the cursor away and hands the arrows back to the
// carousel. Left and Right move across the cards; stepping off the pair with
// them lets the cursor go and turns the band, the way those keys always did.
static bool homeDashCursorKey(char k) {
    const bool up    = (k == KEY_SCROLL_UP || k == 'j' || k == 'J');
    const bool down  = (k == KEY_SCROLL_DN || k == 'k' || k == 'K');
    const bool left  = (k == KEY_PREV_CHAN);
    const bool right = (k == KEY_NEXT_CHAN);
    const bool enter = (k == KEY_ENTER || k == KEY_ROLLER);
    const bool back  = (k == KEY_ESCAPE || k == KEY_BACKSPACE);

    switch (s_homeCursor) {
    case HOME_CURSOR_NONE:
        if (up && homeDashCanFocusWeather()) {
            homeDashSetCursor(HOME_CURSOR_WEATHER);
            return true;
        }
        if (down && homeDashBandFirst() != HOME_CURSOR_NONE) {
            homeDashSetCursor(homeDashBandFirst());
            return true;
        }
        return false;
    case HOME_CURSOR_WEATHER:
        if (enter) { homeDashOpen(HOME_CURSOR_WEATHER); return true; }
        if (up) { homeDashSetCursor(HOME_CURSOR_NONE); return true; }
        if (down) { homeDashSetCursor(homeDashBandFirst()); return true; }
        break;
    case HOME_CURSOR_CHUTIL:
    case HOME_CURSOR_SNR:
    case HOME_CURSOR_NODES_RECENT:
    case HOME_CURSOR_NODES_OLDEST: {
        if (enter) { homeDashOpen(s_homeCursor); return true; }
        if (up) {
            homeDashSetCursor(homeDashCanFocusWeather() ? HOME_CURSOR_WEATHER
                                                        : HOME_CURSOR_NONE);
            return true;
        }
        if (down) { homeDashSetCursor(HOME_CURSOR_NONE); return true; }
        if (left || right) {
            const HomeDashCursor next = homeDashBandStep(s_homeCursor, right);
            if (next != HOME_CURSOR_NONE) { homeDashSetCursor(next); return true; }
        }
        break;
    }
    }
    // Back clears the cursor and stops there. Anything else -- stepping off
    // the cards, or a letter -- clears it and goes on to do its own job.
    homeDashSetCursor(HOME_CURSOR_NONE);
    return back;
}

static void refreshHomeDashboard(bool force) {
    if (!homeDashboardVisible()) return;

#if HAS_HOME_TICKER
    // Cheap: one 64-bit compare unless something actually arrived. Ahead of the
    // minute gate below, because a message is worth showing when it lands and
    // not up to a minute later.
    homeTickerService();
#endif

    // The header is minute-resolution by construction — clock, date, battery and
    // a weather reading with its own TTL — so repainting it on every UI tick
    // would be six label writes a second to say the same thing.
    const time_t now = time(nullptr);
    const uint32_t minuteKey = (now >= kClockSetEpoch)
                                   ? (uint32_t)(now / 60)
                                   : (0x80000000u | (millis() / 60000UL));
    const bool minuteRolled = (minuteKey != s_homeGlanceMinuteKey);
    if (force || minuteRolled) {
        updateGlanceHeader(s_homeGlance);
#if HAS_WEATHER
        homeDashPlaceWxHit();
#endif
        s_homeGlanceMinuteKey = minuteKey;
    }
#if !defined(DEVICE_TDECK_PRO)
    // Between minutes, the status icons still follow their state — the pair is
    // on this screen precisely so a glance answers "is it on the network yet",
    // and an answer a minute old does not. Two labels, so it costs nothing on a
    // backlit panel; on the Pro every repaint is an e-paper refresh, so there
    // they wait for the minute above.
    else if (glanceStatusIconsKey() != s_homeGlance.statusKey) {
        paintGlanceStatusIcons(s_homeGlance);
    }
#endif

    // Only the face in front is repainted. The other two are refilled from the
    // same rings and the same node table the moment they come round, so keeping
    // them current while they are hidden is work nobody can see.
#if defined(DEVICE_TDECK_PRO)
    // E-paper, and this is the boot screen. A chart redraw here is a panel
    // refresh — ~700 ms of parked loop() and real battery — and the chart
    // sequences move on every packet received, so following them would have the
    // Pro repainting itself all day while nobody is looking at it. It gets the
    // charts on the header's cadence instead: once a minute, in the refresh the
    // clock was going to cost anyway. The node pages ride the same minute for
    // the same reason.
    //
    // The Tools chart modals keep their per-packet updates. You open those to
    // watch the radio; this one opens itself.
    if (force || minuteRolled) refreshGlancePage(s_homeCarousel, (int)s_homeCarousel.at);
#else
    if (homeDashShowAll()) {
        // Every face is in front, so every face is kept current: the charts on
        // their own sequences, the node lists on the same arrived-or-minute gate.
        refreshGlanceCharts(s_homeCarousel, force);
        const bool arrived = (s_snrHist.seq != s_homeCarousel.nodeSeq);
        if (force || minuteRolled || arrived) {
            s_homeCarousel.nodeSeq = s_snrHist.seq;
            for (int i = 0; i < (int)s_homeCarousel.pages; i++) {
                if (s_homeCarousel.kind[i] != GLANCE_PAGE_CHARTS) {
                    refreshGlancePage(s_homeCarousel, i);
                }
            }
        }
    } else if (glanceFacing(s_homeCarousel) == GLANCE_PAGE_CHARTS) {
        refreshGlanceCharts(s_homeCarousel, force);
    } else {
        // Two reasons to refill, and nothing walks the node table without one:
        // a packet arrived (the SNR ring's seq moved), or the minute rolled and
        // the ages on screen are now wrong.
        const bool arrived = (s_snrHist.seq != s_homeCarousel.nodeSeq);
        if (force || minuteRolled || arrived) {
            s_homeCarousel.nodeSeq = s_snrHist.seq;
            refreshGlancePage(s_homeCarousel, (int)s_homeCarousel.at);
        }
    }
#endif
}

// The bar is the one thing meant to stay visible while the dashboard is up, and
// the dashboard now covers the whole panel, so the bar has to sit above it.
//
// Raising it is safe from these call sites: every path that opens the dashboard
// tears down whatever modal was up first (prepareGlobalNavigation, or the nav
// bar's own branch), and modals build their own copy of this bar anyway rather
// than relying on the root screen's.
static void homeDashRaiseBar() {
    if (lvObjValid(s_chatShortcutBar)) lv_obj_move_foreground(s_chatShortcutBar);
}

static void openHomeDashboard() {
    if (!s_rootScreen) return;
    if (homeDashboardVisible()) {
        // Already here. Bring it up and repaint rather than rebuild: Home
        // pressed twice should not cost a teardown.
        homeDashSetFooterMode(true);
        homeDashMarkNavBar(true);
        lv_obj_move_foreground(s_homeDash);
        homeDashRaiseBar();
        refreshHomeDashboard(true);
        return;
    }
    closeHomeDashboard();   // clears stale child pointers if the screen was rebuilt
    // Set before anything can return early: this carousel is always the themed
    // one, and the bail-out below leaves the band unbuilt without leaving the
    // instance claiming to be the lock screen's.
    s_homeCarousel.themed = true;

    // Where the bar below starts. The charts stop there — but the dashboard
    // itself covers the whole panel, because stopping the *object* there left
    // whatever the bar does not cover still showing the chat screen. On the
    // T-Lora Pager that is the bottom of the channel list beside the bar, which
    // is only as wide as the chat column: a strip of the screen underneath,
    // visible along the bottom edge. Full height plus homeDashRaiseBar() costs
    // nothing on the boards whose bar already spans the display.
    const int contentH = homeDashboardHeight();

    // Everything but the nav column, where the bar is one (Left/Right): the
    // column covers the rest, so nothing of the chat shows past either edge.
    const int dashW = (int)lv_disp_get_hor_res(NULL) - navSideInsetLeft() - navSideInsetRight();
    s_homeDash = lv_obj_create(s_rootScreen);
    lv_obj_remove_style_all(s_homeDash);
    lv_obj_set_size(s_homeDash, dashW, lv_disp_get_ver_res(NULL));
    lv_obj_align(s_homeDash, LV_ALIGN_TOP_LEFT, navSideInsetLeft(), 0);
    lv_obj_clear_flag(s_homeDash, LV_OBJ_FLAG_SCROLLABLE);
    // The theme's own background, and the header above draws in inks chosen
    // against it — see glancePalette(). This used to be the lock screen's fixed
    // black for the good reason that the header's type was fixed white; now
    // that both come out of one palette, a themed ground is simply what a
    // screen you use should have.
    lv_obj_set_style_bg_color(s_homeDash, glancePalette(/*themed=*/true).bg, 0);
    lv_obj_set_style_bg_opa(s_homeDash, LV_OPA_COVER, 0);
    // Swallow taps for the same reason the lock screen does: the chat screen is
    // still built underneath, and on a board with a touch panel a press would
    // otherwise land on a button nobody can see.
    lv_obj_add_flag(s_homeDash, LV_OBJ_FLAG_CLICKABLE);
    // One handler for the whole carousel, and the flag clear that makes it
    // reachable. LVGL's indev_gesture() walks *up* from the pressed object for
    // as long as each one HAS LV_OBJ_FLAG_GESTURE_BUBBLE, and delivers to the
    // first that does not. LVGL sets that flag on every object with a parent,
    // so leaving it on here sent every swipe past the dashboard to the root
    // screen — the first parentless object above it — and this callback never
    // ran. Clearing it on the dashboard alone makes it the place the walk
    // stops: children (charts, cards, node rows) still bubble up to it, and it
    // ends here rather than at the screen.
    lv_obj_clear_flag(s_homeDash, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_homeDash, homeDashGestureCb, LV_EVENT_GESTURE, nullptr);

    // The header across the top, the widgets under it, on every board. P4
    // landscape used to put the header in a left column and the widgets beside
    // it; the header now spans the screen there too, and the widgets take the
    // height left under it -- less than the column gave them, which is the
    // trade asked for. With the full width, the weather goes beside the clock
    // (the hero's right half) rather than under it.
    lv_obj_t *headerParent = s_homeDash;
    const int widgetsX = 0;
    const int widgetsW = dashW;
#if HAS_WEATHER
    // P4 landscape: 616 px across is room for each hero cell to carry its
    // detail beside it (alignGlanceWide()).
    glanceSetHomeShape(s_homeGlance);
#endif
    buildGlanceHeader(headerParent, s_homeGlance, !homeDashFooterShowsStatus(),
                      /*themed=*/true);
#if HAS_WEATHER
    // Before the short-panel return below: the header is all that path shows,
    // and the reading in it opens Weather there too.
    buildHomeDashWxHit(headerParent);
#endif

    // Under the header, where the lock screen puts its message previews. Its
    // widgets are positioned absolutely from the top, so the first free row is
    // the bottom of the clock — computed from the font rather than hardcoded,
    // because the six layouts do not share one.
    //
    const int chartsTop = glanceBandTop();

    // Defensive floor. Every panel this builds on clears 90 px here, but the
    // header's height comes from a font and the dashboard's from a widget it
    // measured, so neither is a constant this file controls — and a negative
    // size handed to LVGL is a much worse failure than a header on its own.
    const int chartsH = contentH - chartsTop;
    if (chartsH < 48) {
        s_homeGlanceMinuteKey = UINT32_MAX;
        refreshHomeDashboard(true);
        homeDashSetFooterMode(true);
        homeDashMarkNavBar(true);
        lv_obj_move_foreground(s_homeDash);
        homeDashRaiseBar();
        return;
    }

    // The band under the header is a carousel host now: every page occupies the
    // same rectangle and one of them is visible. The host owns the geometry
    // that used to be the chart row's, and the chart row becomes page 0 at full
    // size inside it — so nothing about how the two cards lay themselves out
    // moves. Which faces follow it is glanceBuildNodePages()' call.
    // Gutters for the carousel arrows. The host is inset by them so no card
    // ever runs under a chevron, and the chevrons sit outside the host — they
    // stay put while the pages slide beneath them, and are never clipped or
    // carried off screen with one.
    const int arrowGutter = homeDashShowAll() ? 0 : 11;   // no arrows: nothing turns
    lv_obj_t *pageHost = lv_obj_create(s_homeDash);
    lv_obj_remove_style_all(pageHost);
    lv_obj_set_size(pageHost, widgetsW - (2 * arrowGutter), chartsH);
    lv_obj_align(pageHost, LV_ALIGN_TOP_LEFT, widgetsX + arrowGutter, chartsTop);
    lv_obj_clear_flag(pageHost, LV_OBJ_FLAG_SCROLLABLE);
    s_homeCarousel.host = pageHost;
#if HAS_HOME_CAROUSEL_TAP
    // The host stays clickable while its pages and cards do not, so one handler
    // here catches a tap anywhere in the band. The arrows sit outside it and are
    // labels, so they are not tap targets — the whole band is.
    lv_obj_add_event_cb(pageHost, homeDashTapCb, LV_EVENT_CLICKED, nullptr);
#endif

    // The affordance. Without them page 0 is indistinguishable from the fixed
    // pair of charts this band used to be, and nothing on screen suggests it
    // turns. Labels rather than buttons: they mark the gesture instead of
    // offering a second way to perform it, and a tap target here would sit over
    // the pages and complicate what the swipe handler sees.
    if (!homeDashShowAll()) {
        const int arrowY = chartsTop
            + (chartsH - (int)lv_font_get_line_height(&lv_font_montserrat_12)) / 2;
        for (int side = 0; side < 2; side++) {
            lv_obj_t *arrow = lv_label_create(s_homeDash);
            lv_obj_set_style_text_font(arrow, &lv_font_montserrat_12, 0);
            // The dashboard's own affordance, so always the themed ink: this
            // block is openHomeDashboard()'s, and the lock screen draws no
            // arrows (see buildLockCarousel).
            lv_obj_set_style_text_color(arrow, homeDashMutedInk(true), 0);
#if !defined(DEVICE_TDECK_PRO)
            // A step back from the headings, so they read as a hint rather than
            // competing with the cards. Not on the Pro: partial opacity there
            // dithers into a chequer of the two tones it has.
            lv_obj_set_style_text_opa(arrow, LV_OPA_60, 0);
#endif
            lv_label_set_text(arrow, side == 0 ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT);
            lv_obj_align(arrow, side == 0 ? LV_ALIGN_TOP_LEFT : LV_ALIGN_TOP_RIGHT,
                         side == 0 ? widgetsX + 1 : -1, arrowY);
        }
    }

    if (homeDashShowAll()) {
        // The band shared between the faces rather than each having all of it:
        // the charts, then the node lists -- one face when they fit side by
        // side, two when they do not. Worked out before the node faces are
        // built, because a list card sizes its row count from the height it is
        // given.
        constexpr int kShowAllGap = 4;
        const int showAllFaces = homeDashSideBySide() ? 2 : 3;
        const int faceH = (chartsH - kShowAllGap * (showAllFaces - 1)) / showAllFaces;
        glanceBuildChartPage(s_homeCarousel, pageHost);
        glanceBuildNodePages(s_homeCarousel, pageHost, faceH - kTdeckProBandInset);
        for (int i = 0; i < (int)s_homeCarousel.pages; i++) {
            lv_obj_t *page = s_homeCarousel.page[i];
            if (!lvObjValid(page)) continue;
            lv_obj_set_height(page, faceH);
            lv_obj_set_y(page, i * (faceH + kShowAllGap));
            lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        glanceBuildChartPage(s_homeCarousel, pageHost);
        // A card gets the band less its page's own padding, and that height is
        // what decides how many rows it fits.
        glanceBuildNodePages(s_homeCarousel, pageHost, chartsH - kTdeckProBandInset);
    }
    // Either list opens the Nodes screen. Here rather than in the page builder,
    // which has three layouts to return from and serves the lock screen too.
    homeDashMakeCardTarget(s_homeCarousel.nodeCard[0], nullptr, HOME_CURSOR_NODES_RECENT);
    homeDashMakeCardTarget(s_homeCarousel.nodeCard[1], nullptr, HOME_CURSOR_NODES_OLDEST);

    // The remembered page can outlive the layout that had it: the Heltec and
    // Wio boards rotate on a setting, and landscape has one fewer face than
    // portrait. Left alone, coming back in landscape on page 2 would show a
    // page this build never made.
    if (!homeDashShowAll()) glanceShowPage(s_homeCarousel, s_homeCarousel.at);

    s_homeGlanceMinuteKey = UINT32_MAX;   // force the first header paint
    refreshHomeDashboard(true);
    homeDashSetFooterMode(true);
    homeDashMarkNavBar(true);
    lv_obj_move_foreground(s_homeDash);
    homeDashRaiseBar();
}

#if FEATURE_LOCK_SCREEN
// ── Lock-screen carousel ─────────────────────────────────────────────────────
// The same band the dashboard turns, under the lock screen's header, carrying
// the recent-message rows that used to own that space as its first face.
//
// Built from here rather than from showTdeckProSleepClock(), which sits twenty
// thousand lines above everything this needs -- the page builders, the node
// collector, the card styling. The overlay reaches in through the forward
// declarations at the top of the file instead, and hands back the one thing it
// has to own itself: where to put the message labels.
//
// There is no navigation input. Every press on a locked screen is a dismissal --
// see tryExitLockScreenFromInput() -- apart from a hold on a message preview,
// which opens Message Actions (lockMsgHoldOnTouch()); a swipe handler here would
// fight both. The band turns on a timer or not at all, which is also
// why it draws no arrows: those mark a gesture, and there is no gesture to mark.

// Returns the page the message rows should be built into, or nullptr if the
// band was too short to carry a carousel -- in which case the caller lays the
// rows out exactly as it always did, straight onto the overlay.
static lv_obj_t *buildLockCarousel(lv_obj_t *overlay, int top, int height) {
    glanceCarouselForget(s_lockCarousel);
    s_lockCarousel.themed = false;   // the fixed palette, like the header above
    s_lockCarousel.at = 0;           // always opens on the messages face
    s_lockCarouselTurnedMs = millis();

    // The same defensive floor openHomeDashboard() applies, for the same
    // reason: this height comes from a per-board constant and a panel
    // resolution, neither of which this file controls, and a carousel with no
    // room for a chart is worse than the plain message list it replaced.
    if (!overlay || height < 48) return nullptr;

    // Full width, unlike the dashboard's inset host: with no arrows there are
    // no gutters to leave for them, and the message rows keep the exact
    // geometry they had before -- which is what lets sleepOverlayMsgRowWidth()
    // go on being right about how wide a message may be.
    lv_obj_t *host = lv_obj_create(overlay);
    lv_obj_remove_style_all(host);
    lv_obj_set_size(host, lv_disp_get_hor_res(NULL), height);
    lv_obj_align(host, LV_ALIGN_TOP_LEFT, 0, top);
    lv_obj_clear_flag(host, LV_OBJ_FLAG_SCROLLABLE);
    s_lockCarousel.host = host;

    // Messages first, because that is what this band was, and what someone
    // glancing at a locked device is most likely to be looking for. A plain
    // container rather than buildHomeDashPage(): the rows position themselves
    // absolutely from the top of their parent, and a flex page with padding of
    // its own would fight them for the layout.
    lv_obj_t *msgPage = lv_obj_create(host);
    lv_obj_remove_style_all(msgPage);
    lv_obj_set_size(msgPage, lv_pct(100), lv_pct(100));
    lv_obj_align(msgPage, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(msgPage, LV_OBJ_FLAG_SCROLLABLE);
    glanceAddPage(s_lockCarousel, msgPage, GLANCE_PAGE_MESSAGES);

#if HOME_DASH_CAN_SHOW_ALL
    // Everything at once, as on Home: the messages, then Home's widgets under
    // them, each face a share of the band -- and no timer turning it
    // (serviceLockCarousel()). The message budget is cut to the messages'
    // share, worst case all one-line rows with a gap after each, so the last
    // row cannot reach into the charts below.
    if (homeDashShowAll()) {
        constexpr int kShowAllGap = 4;
        const int faces = 1 + (homeDashSideBySide() ? 2 : 3);
        const int faceH = (height - kShowAllGap * (faces - 1)) / faces;
        glanceBuildChartPage(s_lockCarousel, host);
        glanceBuildNodePages(s_lockCarousel, host, faceH - kTdeckProBandInset);
        for (int i = 0; i < (int)s_lockCarousel.pages; i++) {
            lv_obj_t *page = s_lockCarousel.page[i];
            if (!lvObjValid(page)) continue;
            lv_obj_set_height(page, faceH);
            lv_obj_set_y(page, i * (faceH + kShowAllGap));
            lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
        }
        kTdeckProMsgTotalLines = max(1, (faceH + kTdeckProMsgGapY)
                                        / (kTdeckProMsgLineH + kTdeckProMsgGapY));
        return msgPage;
    }
#endif

    glanceBuildChartPage(s_lockCarousel, host);
    glanceBuildNodePages(s_lockCarousel, host, height - kTdeckProBandInset);

    // Opens on messages when there are any and on the charts when there are
    // not, rather than showing an empty band for the first thirty seconds.
    // Stepped here rather than through glanceCarouselGo() so the first face
    // simply appears: that would slide, and there is nothing to slide off.
    uint8_t first = 0;
    while (first < s_lockCarousel.pages
           && !glancePageHasContent(s_lockCarousel, first)) {
        first++;
    }
    if (first >= s_lockCarousel.pages) first = 0;
    glanceShowPage(s_lockCarousel, first);
    return msgPage;
}

// Bookkeeping only: every object above was a child of the overlay and LVGL
// freed them with it. Back to the messages face for the next lock, rather than
// wherever the timer had got to when the screen was dismissed -- this surface
// is arrived at, not returned to.
static void resetLockCarousel() {
    glanceCarouselForget(s_lockCarousel);
    s_lockCarousel.at = 0;
}

// Repaints the face in front. Called on the overlay's minute tick, so a node
// page's ages creep with the clock above them instead of freezing at whatever
// they read when the page came round.
static void refreshLockCarouselPage() {
    if (!lvObjValid(s_lockCarousel.host)) return;
    if (homeDashShowAll()) {
        // Every face is showing (buildLockCarousel()), so every face is repainted.
        for (int i = 0; i < (int)s_lockCarousel.pages; i++) {
            refreshGlancePage(s_lockCarousel, i);
        }
    } else {
        refreshGlancePage(s_lockCarousel, (int)s_lockCarousel.at);
    }
}

// The only thing that turns this carousel, driven from serviceLockScreen()
// because that already runs every loop with the clock this wants.
static void serviceLockCarousel(uint32_t nowMs) {
    if (homeDashShowAll()) return;   // every face is already showing
    if (!lvObjValid(s_lockCarousel.host) || s_lockCarousel.pages < 2) return;
    if ((uint32_t)(nowMs - s_lockCarouselTurnedMs) < kLockCarouselDwellMs) return;
    s_lockCarouselTurnedMs = nowMs;
    glanceCarouselGo(s_lockCarousel, +1);
}
#endif  // FEATURE_LOCK_SCREEN
#endif  // HAS_HOME_DASHBOARD
