#pragma once
/*
 * chart.hpp — ASCII/Unicode chart renderer for TermiTrade
 * Renders line charts and candlesticks inside FTXUI canvas elements
 */

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/canvas.hpp>
#include <vector>
#include <algorithm>
#include <string>

using namespace ftxui;

class Chart {
public:
    // Renders a line chart from price history data
    // Returns an FTXUI Element (canvas-based)
    Element render(const std::vector<double>& data, bool is_positive) {
        if (data.empty()) return text("No data") | center;

        const int W = 120, H = 30; // canvas cells
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

        // Draw grid lines (horizontal)
        for (int g = 0; g <= 4; g++) {
            int y = 4 + g * (H * 4 - 8) / 4;
            for (int x = 2; x < W * 2 - 2; x += 4)
                c.DrawText(x, y, "·", [](Pixel& p){ p.foreground_color = Color::GrayDark; });
        }

        // Draw price line with fill
        for (int i = 1; i < (int)data.size(); i++) {
            int x0 = toX(i-1), y0 = toY(data[i-1]);
            int x1 = toX(i),   y1 = toY(data[i]);
            c.DrawPointLine(x0, y0, x1, y1, line_color);

            // Fill under the curve
            int fill_bot = H * 4 - 4;
            for (int fy = std::min(y0,y1); fy <= fill_bot; fy += 4)
                c.DrawPoint(x1, fy,
                    is_positive ? Color::GreenLight : Color::RedLight);
        }

        // Current price dot (bright)
        int last_x = toX(data.size()-1);
        int last_y = toY(data.back());
        c.DrawPointCircleFilled(last_x, last_y, 2, line_color);

        // Price labels (right axis)
        // These are drawn as text overlays separately
        return canvas(std::move(c));
    }

    // Render mini sparkline (for watchlist row)
    Element sparkline(const std::vector<double>& data, bool up) {
        if (data.size() < 2) return text("────");
        std::string line;
        const char* blocks = "▁▂▃▄▅▆▇█";
        double mn = *std::min_element(data.begin(), data.end());
        double mx = *std::max_element(data.begin(), data.end());
        double range = mx - mn;
        if (range == 0) return text("────");
        for (auto v : data) {
            int idx = (int)((v - mn) / range * 7.0);
            idx = std::clamp(idx, 0, 7);
            line += blocks[idx];
        }
        return text(line) | color(up ? Color::Green : Color::Red);
    }
};
