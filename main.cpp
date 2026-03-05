/*
 * TermiTrade - Bloomberg-style Terminal Stock Tracker
 * Author: Keshav Raj | DTU
 * Built with: C++ | FTXUI | libcurl | nlohmann/json
 *
 * Features:
 *   - Real-time price updates via Alpha Vantage API
 *   - ASCII/Unicode candlestick + line charts
 *   - Portfolio P&L tracker
 *   - Price alert system with notifications
 *   - Multi-panel TUI (Terminal UI)
 */

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "portfolio.hpp"
#include "api_client.hpp"
#include "chart.hpp"
#include "alerts.hpp"
#include "watchlist.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <iostream>

using namespace ftxui;
using json = nlohmann::json;

// ─────────────────────────────────────────────
//  Global state (mutex-protected for threads)
// ─────────────────────────────────────────────
std::mutex g_mtx;
std::atomic<bool> g_running{true};

Watchlist  g_watchlist;
Portfolio  g_portfolio;
AlertSystem g_alerts;
Chart      g_chart;

std::string g_selected_sym = "AAPL";
int         g_selected_idx = 0;
std::string g_timeframe    = "1D";

// ─────────────────────────────────────────────
//  Background thread: fetch prices every 15s
// ─────────────────────────────────────────────
void price_updater_thread(ApiClient& api, ScreenInteractive& screen) {
    while (g_running) {
        {
            std::lock_guard<std::mutex> lock(g_mtx);
            for (auto& stock : g_watchlist.stocks) {
                auto data = api.fetch_quote(stock.symbol);
                if (!data.empty()) {
                    stock.price = data["price"];
                    stock.change = data["change"];
                    stock.change_pct = data["change_pct"];
                    stock.volume = data["volume"];
                }
            }
            g_alerts.check_alerts(g_watchlist.stocks);
        }
        screen.PostEvent(Event::Custom); // trigger redraw
        std::this_thread::sleep_for(std::chrono::seconds(15));
    }
}

// ─────────────────────────────────────────────
//  Render: Watchlist panel (left)
// ─────────────────────────────────────────────
Element render_watchlist() {
    Elements rows;
    rows.push_back(
        hbox({
            text(" SYM  ") | bold | color(Color::Yellow),
            text("       PRICE") | bold | color(Color::Yellow),
            text("     CHG%") | bold | color(Color::Yellow),
        }) | bgcolor(Color::Black)
    );
    rows.push_back(separator());

    for (int i = 0; i < (int)g_watchlist.stocks.size(); i++) {
        auto& s = g_watchlist.stocks[i];
        bool selected = (i == g_selected_idx);

        Color price_color = s.change >= 0 ? Color::Green : Color::Red;
        std::string arrow = s.change >= 0 ? "▲" : "▼";
        std::string chg_str = arrow + " " + format_pct(std::abs(s.change_pct));

        auto row = hbox({
            text(" " + s.symbol) | bold | color(Color::Yellow) | size(WIDTH, EQUAL, 6),
            text(format_price(s.price)) | color(Color::White) | size(WIDTH, EQUAL, 12),
            text(chg_str) | color(price_color) | size(WIDTH, EQUAL, 9),
        });

        if (selected)
            rows.push_back(row | bgcolor(Color::GrayDark) | inverted);
        else
            rows.push_back(row);
    }

    return vbox(rows) | border | size(WIDTH, EQUAL, 30);
}

// ─────────────────────────────────────────────
//  Render: Chart panel (center)
// ─────────────────────────────────────────────
Element render_chart() {
    auto& s = g_watchlist.stocks[g_selected_idx];
    Color c = s.change >= 0 ? Color::Green : Color::Red;
    std::string arrow = s.change >= 0 ? "▲ +" : "▼ ";

    auto header = hbox({
        text(s.symbol + " ") | bold | color(Color::Yellow) | size(HEIGHT, EQUAL, 1),
        text(format_price(s.price)) | bold | color(Color::White),
        text("  " + arrow + format_price(s.change) +
             " (" + format_pct(s.change_pct) + "%)") | color(c),
        filler(),
        text("O:" + format_price(s.open)) | color(Color::GrayLight),
        text("  H:" + format_price(s.high)) | color(Color::Green),
        text("  L:" + format_price(s.low))  | color(Color::Red),
        text("  VOL:" + s.volume_str) | color(Color::GrayLight),
    });

    // Timeframe selector
    auto tf_bar = hbox({
        text(g_timeframe == "1D" ? "[1D]" : " 1D ") | (g_timeframe=="1D" ? bold|bgcolor(Color::Yellow)|color(Color::Black) : color(Color::GrayDark)),
        text(g_timeframe == "5D" ? "[5D]" : " 5D ") | (g_timeframe=="5D" ? bold|bgcolor(Color::Yellow)|color(Color::Black) : color(Color::GrayDark)),
        text(g_timeframe == "1M" ? "[1M]" : " 1M ") | (g_timeframe=="1M" ? bold|bgcolor(Color::Yellow)|color(Color::Black) : color(Color::GrayDark)),
        text(g_timeframe == "3M" ? "[3M]" : " 3M ") | (g_timeframe=="3M" ? bold|bgcolor(Color::Yellow)|color(Color::Black) : color(Color::GrayDark)),
        text(g_timeframe == "1Y" ? "[1Y]" : " 1Y ") | (g_timeframe=="1Y" ? bold|bgcolor(Color::Yellow)|color(Color::Black) : color(Color::GrayDark)),
        filler(),
        text("← → switch  TAB panel  A add alert  Q quit") | color(Color::GrayDark),
    });

    // ASCII chart canvas
    auto chart_elem = g_chart.render(s.history, s.change >= 0);

    return vbox({
        header | border,
        tf_bar  | bgcolor(Color::Black),
        separator(),
        chart_elem | flex,
    }) | border | flex;
}

// ─────────────────────────────────────────────
//  Render: Portfolio panel (right top)
// ─────────────────────────────────────────────
Element render_portfolio() {
    Elements rows;
    rows.push_back(
        hbox({
            text(" SYM ") | bold | color(Color::Cyan),
            text("    VAL") | bold | color(Color::Cyan),
            text("     P&L") | bold | color(Color::Cyan),
        })
    );
    rows.push_back(separator());

    double total = 0.0, total_pnl = 0.0;
    for (auto& h : g_portfolio.holdings) {
        auto it = std::find_if(g_watchlist.stocks.begin(), g_watchlist.stocks.end(),
            [&](auto& s){ return s.symbol == h.symbol; });
        if (it == g_watchlist.stocks.end()) continue;

        double cur_val = it->price * h.shares;
        double cost    = h.avg_cost * h.shares;
        double pnl     = cur_val - cost;
        double pnl_pct = (pnl / cost) * 100.0;
        total     += cur_val;
        total_pnl += pnl;

        Color pnl_col = pnl >= 0 ? Color::Green : Color::Red;
        std::string sign = pnl >= 0 ? "+" : "";

        rows.push_back(hbox({
            text(" " + h.symbol) | color(Color::Cyan) | size(WIDTH, EQUAL, 5),
            text(format_price(cur_val)) | size(WIDTH, EQUAL, 8),
            text(sign + format_price(pnl)) | color(pnl_col) | size(WIDTH, EQUAL, 9),
        }));
    }

    rows.push_back(separator());
    rows.push_back(hbox({
        text(" TOTAL") | bold,
        text(format_price(total)) | bold | size(WIDTH, EQUAL, 8),
        text((total_pnl>=0?"+":"")+format_price(total_pnl)) | bold
            | color(total_pnl>=0 ? Color::Green : Color::Red),
    }));

    return vbox(rows) | border | size(HEIGHT, LESS_THAN, 14);
}

// ─────────────────────────────────────────────
//  Render: Alerts panel (right bottom)
// ─────────────────────────────────────────────
Element render_alerts() {
    Elements items;
    for (auto& a : g_alerts.get_recent(10)) {
        Color c = a.type == AlertType::UP  ? Color::Green  :
                  a.type == AlertType::DOWN ? Color::Red    : Color::Yellow;
        items.push_back(
            hbox({
                text(a.timestamp + " ") | color(Color::GrayDark) | size(WIDTH, EQUAL, 9),
                text(a.message) | color(c),
            })
        );
    }
    return vbox(items) | border | flex;
}

// ─────────────────────────────────────────────
//  Render: Ticker tape (top)
// ─────────────────────────────────────────────
Element render_ticker() {
    Elements ticks;
    ticks.push_back(text("◆ LIVE │ ") | bold | bgcolor(Color::Yellow) | color(Color::Black));
    for (auto& s : g_watchlist.stocks) {
        Color c = s.change >= 0 ? Color::Green : Color::Red;
        ticks.push_back(text(s.symbol + " ") | bold | color(Color::Yellow));
        ticks.push_back(text(format_price(s.price) + " "));
        ticks.push_back(text((s.change>=0?"▲":"▼") + format_pct(std::abs(s.change_pct)) + "% │ ") | color(c));
    }
    return hbox(ticks) | bgcolor(Color::Black);
}

// ─────────────────────────────────────────────
//  Render: Bottom status bar
// ─────────────────────────────────────────────
Element render_statusbar() {
    auto now = get_time_str();
    return hbox({
        text(" ● ") | color(Color::Green),
        text("FEED ACTIVE") | color(Color::GrayLight),
        text("  │  NIFTY: 21,843 ▲  │  SENSEX: 72,186 ▲  │  BTC: $67,420 ▲  │  USD/INR: 83.42") | color(Color::GrayDark),
        filler(),
        text("TermiTrade v1.0  │  " + now + " IST ") | color(Color::GrayDark),
    }) | bgcolor(Color::Black);
}

// ─────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────
int main(int argc, char* argv[]) {
    // Load API key from env or arg
    std::string api_key = (argc > 1) ? argv[1] : (getenv("AV_API_KEY") ? getenv("AV_API_KEY") : "demo");

    ApiClient api(api_key);

    // Seed watchlist
    g_watchlist.add("AAPL",  "Apple Inc.");
    g_watchlist.add("GOOGL", "Alphabet Inc.");
    g_watchlist.add("MSFT",  "Microsoft Corp.");
    g_watchlist.add("TSLA",  "Tesla Inc.");
    g_watchlist.add("NVDA",  "NVIDIA Corp.");
    g_watchlist.add("META",  "Meta Platforms");

    // Seed portfolio
    g_portfolio.add("AAPL", 10, 160.00);
    g_portfolio.add("NVDA",  5, 580.00);
    g_portfolio.add("MSFT",  8, 350.00);
    g_portfolio.add("TSLA", 15, 220.00);

    // Seed default alerts
    g_alerts.add_price_alert("AAPL", 190.0, AlertDirection::ABOVE);
    g_alerts.add_price_alert("TSLA", 240.0, AlertDirection::BELOW);

    // Initial fetch
    for (auto& s : g_watchlist.stocks)
        s.history = api.fetch_history(s.symbol, g_timeframe);

    auto screen = ScreenInteractive::Fullscreen();

    // Background price update thread
    std::thread updater([&](){ price_updater_thread(api, screen); });

    // ── Input handling ──
    auto renderer = Renderer([&]() {
        std::lock_guard<std::mutex> lock(g_mtx);
        return vbox({
            render_ticker(),
            separator(),
            hbox({
                render_watchlist(),
                separator(),
                render_chart() | flex,
                separator(),
                vbox({
                    render_portfolio(),
                    separator(),
                    render_alerts() | flex,
                }) | size(WIDTH, EQUAL, 35),
            }) | flex,
            separator(),
            render_statusbar(),
        });
    });

    auto component = CatchEvent(renderer, [&](Event e) {
        std::lock_guard<std::mutex> lock(g_mtx);
        if (e == Event::Character('q') || e == Event::Character('Q')) {
            g_running = false;
            screen.ExitLoopClosure()();
            return true;
        }
        if (e == Event::ArrowDown || e == Event::Character('j')) {
            g_selected_idx = std::min((int)g_watchlist.stocks.size()-1, g_selected_idx+1);
            g_selected_sym = g_watchlist.stocks[g_selected_idx].symbol;
            return true;
        }
        if (e == Event::ArrowUp || e == Event::Character('k')) {
            g_selected_idx = std::max(0, g_selected_idx-1);
            g_selected_sym = g_watchlist.stocks[g_selected_idx].symbol;
            return true;
        }
        if (e == Event::Character('1')) { g_timeframe = "1D"; return true; }
        if (e == Event::Character('5')) { g_timeframe = "5D"; return true; }
        if (e == Event::Character('m')) { g_timeframe = "1M"; return true; }
        if (e == Event::Character('3')) { g_timeframe = "3M"; return true; }
        if (e == Event::Character('y')) { g_timeframe = "1Y"; return true; }
        return false;
    });

    screen.Loop(component);

    updater.join();
    return 0;
}
