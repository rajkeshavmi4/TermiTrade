#pragma once
/*
 * chart.hpp — ASCII/Unicode chart renderer for TermiTrade
 *
 * Provides two chart modes:
 *   1. LINE  — area-filled line chart (original behaviour)
 *   2. CANDLE — Unicode candlestick chart using FTXUI Canvas braille pixels
 *
 * Toggle with 'c' key in main.cpp.
 */

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/canvas.hpp>
#include <vector>
#include <algorithm>
#include <string>
#include <cmath>

using namespace ftxui;

// ──────────────────────────────────────────────
//  OHLCV candle — fetched by ApiClient::fetch_ohlc()
// ──────────────────────────────────────────────
struct CandleData {
    double      open   = 0;
    double      high   = 0;
    double      low    = 0;
    double      close  = 0;
    long long   volume = 0;
    std::string date;          // "YYYY-MM-DD" or "HH:MM:SS"
};

// ──────────────────────────────────────────────
//  Chart mode enum (toggled in main)
// ──────────────────────────────────────────────
enum class ChartMode { LINE, CANDLE };

// ──────────────────────────────────────────────
//  Chart renderer
// ──────────────────────────────────────────────
class Chart {
public:
    ChartMode mode = ChartMode::LINE;

    // ── Line chart (original, unchanged logic) ──────────────────────────────
    Element render(const std::vector<double>& data, bool is_positive) {
        if (data.empty()) return text("No data") | center;

        const int W = 120, H = 30;
        auto c = Canvas(W * 2, H * 4);

        double mn = *std::min_element(data.begin(), data.end()) * 0.999;
        double mx = *std::max_element(data.begin(), data.end()) * 1.001;
        double range = mx - mn;
        if (range == 0) range = 1;

        Color line_color = is_positive ? Color::Green : Color::Red;

        auto toX = [&](int i) -> int {
            return (int)((double)i / (data.size() - 1) * (W * 2 - 4)) + 2;
        };
        auto toY = [&](double v) -> int {
            return (int)((1.0 - (v - mn) / range) * (H * 4 - 8)) + 4;
        };

        // Horizontal grid lines
        for (int g = 0; g <= 4; g++) {
            int y = 4 + g * (H * 4 - 8) / 4;
            for (int x = 2; x < W * 2 - 2; x += 4)
                c.DrawText(x, y, "·",
                    [](Pixel& p){ p.foreground_color = Color::GrayDark; });
        }

        // Price line + area fill
        for (int i = 1; i < (int)data.size(); i++) {
            int x0 = toX(i-1), y0 = toY(data[i-1]);
            int x1 = toX(i),   y1 = toY(data[i]);
            c.DrawPointLine(x0, y0, x1, y1, line_color);

            int fill_bot = H * 4 - 4;
            for (int fy = std::min(y0, y1); fy <= fill_bot; fy += 4)
                c.DrawPoint(x1, fy,
                    is_positive ? Color::GreenLight : Color::RedLight);
        }

        // Last-price dot
        c.DrawPointCircleFilled(toX(data.size()-1), toY(data.back()), 2, line_color);

        return canvas(std::move(c));
    }

    // ── Candlestick chart ────────────────────────────────────────────────────
    //
    //  Each candle is rendered as:
    //    • A thin vertical line (wick) from low to high  (DrawPointLine)
    //    • A filled rectangle (body) from open to close
    //      — Green (bullish) when close >= open
    //      — Red   (bearish) when close <  open
    //
    //  If candles are empty, falls back to a demo sine-wave line chart.
    //
    Element render_candlestick(const std::vector<CandleData>& candles) {
        if (candles.empty())
            return text(" No OHLC data — try pressing 'r' to refresh ") | center | color(Color::GrayDark);

        const int W = 120, H = 30;
        auto c = Canvas(W * 2, H * 4);

        // Price range across all candles
        double mn = 1e18, mx = -1e18;
        for (auto& cd : candles) {
            mn = std::min(mn, cd.low);
            mx = std::max(mx, cd.high);
        }
        mn -= (mx - mn) * 0.02;
        mx += (mx - mn) * 0.02;
        double range = mx - mn;
        if (range <= 0) range = 1;

        int n       = (int)candles.size();
        int avail_w = W * 2 - 8;
        int cw      = std::max(3, avail_w / n);  // pixels per candle (min 3)

        auto toY = [&](double v) -> int {
            return (int)((1.0 - (v - mn) / range) * (H * 4 - 10)) + 5;
        };

        // Horizontal grid
        for (int g = 0; g <= 4; g++) {
            int y = 5 + g * (H * 4 - 10) / 4;
            for (int x = 2; x < W * 2 - 2; x += 6)
                c.DrawText(x, y, "·",
                    [](Pixel& p){ p.foreground_color = Color::GrayDark; });
        }

        for (int i = 0; i < n; i++) {
            auto& cd   = candles[i];
            bool bull  = cd.close >= cd.open;
            Color body = bull ? Color::Green : Color::Red;
            Color wick = bull ? Color::GreenLight : Color::RedLight;

            int cx     = 4 + i * cw + cw / 2;
            int y_high = toY(cd.high);
            int y_low  = toY(cd.low);
            int y_open = toY(cd.open);
            int y_clos = toY(cd.close);

            // Wick: full low-high span
            c.DrawPointLine(cx, y_high, cx, y_low, wick);

            // Body: filled block between open and close
            int y_top = std::min(y_open, y_clos);
            int y_bot = std::max(y_open, y_clos);
            if (y_top == y_bot) y_bot = y_top + 2;   // doji: thin line

            int half = std::max(1, cw / 2 - 1);
            for (int py = y_top; py <= y_bot; py++) {
                for (int px = cx - half; px <= cx + half; px++) {
                    c.DrawPoint(px, py, body);
                }
            }
        }

        // Latest close dot
        if (!candles.empty()) {
            auto& last = candles.back();
            bool bull  = last.close >= last.open;
            int cx     = 4 + (n-1) * cw + cw / 2;
            c.DrawPointCircleFilled(cx, toY(last.close), 3,
                bull ? Color::Green : Color::Red);
        }

        return canvas(std::move(c));
    }

    // ── Axis price labels (overlay, used by render_chart() in main.cpp) ──────
    //  Returns a vbox of right-aligned price labels for 5 gridlines.
    Element price_axis(double mn, double mx) {
        Elements labels;
        for (int g = 4; g >= 0; g--) {
            double v = mn + (mx - mn) * g / 4.0;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << v;
            labels.push_back(text(ss.str()) | color(Color::GrayLight) | align_right);
        }
        return vbox(labels) | size(WIDTH, EQUAL, 8);
    }

    // ── Mini sparkline (watchlist row) ────────────────────────────────────────
    Element sparkline(const std::vector<double>& data, bool up) {
        if (data.size() < 2) return text("────");
        const char* blocks = "▁▂▃▄▅▆▇█";
        double mn = *std::min_element(data.begin(), data.end());
        double mx = *std::max_element(data.begin(), data.end());
        double range = mx - mn;
        if (range == 0) return text("────");
        std::string line;
        // Cap sparkline width to last 20 points so it fits in the column
        int start = std::max(0, (int)data.size() - 20);
        for (int i = start; i < (int)data.size(); i++) {
            int idx = (int)((data[i] - mn) / range * 7.0);
            line += blocks[std::clamp(idx, 0, 7)];
        }
        return text(line) | color(up ? Color::Green : Color::Red);
    }

    // ── Crypto sparkline (wider, uses ▏▎▍▌▋▊▉█) ────────────────────────────
    Element crypto_sparkline(const std::vector<double>& data, bool up) {
        return sparkline(data, up);   // same logic, different call site styling
    }
};
