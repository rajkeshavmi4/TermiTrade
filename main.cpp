/*
 * TermiTrade v1.1 - Bloomberg-style Terminal Stock Tracker
 * Author: Keshav Raj | DTU
 * Built with: C++ | FTXUI | libcurl | nlohmann/json
 *
 * New in v1.1:
 *   [c] Candlestick chart mode   (toggle with 'c')
 *   [i] Indian market (NSE/BSE)  (auto-detected from config)
 *   [₿] Crypto portfolio tab     (Tab key to switch)
 *   [e] CSV export               ('e' key)
 *   [~] Live config reload       (watches config.json, no restart)
 *   [n] ncurses fallback         (--ncurses flag or TERM=dumb)
 */

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "api_client.hpp"       // Stock, Watchlist, Portfolio, AlertSystem, utils
#include "chart.hpp"            // Chart, CandleData, ChartMode
#include "crypto.hpp"           // CryptoTick, CryptoHolding, CryptoPortfolio, AssetType
#include "config.hpp"           // ConfigLoader, AppConfig
#include "export.hpp"           // PortfolioExporter
#include "ncurses_fallback.hpp" // NcursesApp (stub if ncurses not compiled in)

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <iostream>
#include <cstdlib>
#include <algorithm>

using namespace ftxui;
using json = nlohmann::json;

// ─────────────────────────────────────────────
//  Panel enum — Tab key cycles between these
// ─────────────────────────────────────────────
enum class Panel { WATCHLIST, CRYPTO, PORTFOLIO };

// ─────────────────────────────────────────────
//  Global state (mutex-protected)
// ─────────────────────────────────────────────
std::mutex         g_mtx;
std::atomic<bool>  g_running{true};

Watchlist          g_watchlist;
Portfolio          g_portfolio;
CryptoPortfolio    g_crypto_portfolio;
std::vector<CryptoTick> g_crypto_ticks;
AlertSystem        g_alerts;
Chart              g_chart;
ConfigLoader*      g_config_loader = nullptr;

int         g_selected_idx    = 0;
int         g_crypto_sel_idx  = 0;
std::string g_selected_sym    = "AAPL";
std::string g_timeframe       = "1D";
Panel       g_panel           = Panel::WATCHLIST;
ChartMode   g_chart_mode      = ChartMode::LINE;
std::string g_status_msg;           // ephemeral status line (e.g. "CSV saved to …")
std::chrono::steady_clock::time_point g_status_until;

// ─────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────
static void set_status(const std::string& msg, int seconds = 4) {
    g_status_msg   = msg;
    g_status_until = std::chrono::steady_clock::now()
                   + std::chrono::seconds(seconds);
}

static std::string current_status() {
    if (std::chrono::steady_clock::now() < g_status_until)
        return g_status_msg;
    return "";
}

// ─────────────────────────────────────────────
//  Apply AppConfig → global state
//  Called on first load and on every live-reload
// ─────────────────────────────────────────────
static void apply_config(const AppConfig& cfg, ApiClient& api) {
    // Rebuild watchlist preserving live price data where symbol already exists
    std::map<std::string,Stock> old_prices;
    for (auto& s : g_watchlist.stocks) old_prices[s.symbol] = s;

    g_watchlist.stocks.clear();
    for (auto& e : cfg.watchlist) {
        if (e.market == "CRYPTO") continue;  // crypto lives in g_crypto_ticks
        std::string name = e.symbol;         // we don't store names in config yet
        g_watchlist.add(e.symbol, name, e.market);
        if (old_prices.count(e.symbol))
            g_watchlist.stocks.back() = old_prices[e.symbol];
    }

    // Rebuild stock portfolio
    g_portfolio.holdings.clear();
    for (auto& e : cfg.portfolio) {
        if (e.asset_type == "crypto") continue;
        g_portfolio.add(e.symbol, e.shares, e.avg_cost);
    }

    // Rebuild crypto portfolio
    g_crypto_portfolio.holdings.clear();
    for (auto& e : cfg.portfolio) {
        if (e.asset_type != "crypto") continue;
        g_crypto_portfolio.add(e.symbol, e.shares, e.avg_cost);
    }

    // Rebuild crypto ticks watchlist
    std::map<std::string,CryptoTick> old_crypto;
    for (auto& ct : g_crypto_ticks) old_crypto[ct.symbol] = ct;
    g_crypto_ticks.clear();
    for (auto& e : cfg.watchlist) {
        if (e.market != "CRYPTO") continue;
        CryptoTick ct;
        ct.symbol = e.symbol;
        ct.name   = e.symbol;
        if (old_crypto.count(e.symbol)) ct = old_crypto[e.symbol];
        g_crypto_ticks.push_back(ct);
    }

    // Rebuild price alerts
    for (auto& ae : cfg.alerts) {
        auto dir = (ae.direction == "above") ? AlertDirection::ABOVE : AlertDirection::BELOW;
        g_alerts.add_price_alert(ae.symbol, ae.price, dir);
    }

    g_selected_idx = 0;
    if (!g_watchlist.stocks.empty())
        g_selected_sym = g_watchlist.stocks[0].symbol;
}

// ─────────────────────────────────────────────
//  Background: price updater
// ─────────────────────────────────────────────
void price_updater_thread(ApiClient& api, ScreenInteractive& screen) {
    while (g_running) {
        // ── Stock quotes ──
        {
            std::lock_guard<std::mutex> lock(g_mtx);
            for (auto& stock : g_watchlist.stocks) {
                std::string api_sym = api_symbol(stock.symbol,
                                        market_to_type(stock.market));
                auto data = api.fetch_quote(api_sym);
                if (!data.empty()) {
                    stock.price       = data["price"];
                    stock.change      = data["change"];
                    stock.change_pct  = data["change_pct"];
                    stock.volume_str  = data["volume"].get<std::string>();
                    stock.open        = data["open"];
                    stock.high        = data["high"];
                    stock.low         = data["low"];
                }
            }
            // ── Crypto quotes ──
            for (auto& ct : g_crypto_ticks) {
                auto data = api.fetch_crypto_quote(ct.symbol);
                if (!data.empty()) {
                    ct.price = data["price"];
                    // change_pct not available on free tier — leave as 0
                }
            }
            g_alerts.check_alerts(g_watchlist.stocks);
        }

        // ── Config live-reload ──
        if (g_config_loader) {
            bool changed = false;
            {
                std::lock_guard<std::mutex> lock(g_mtx);
                changed = g_config_loader->reload_if_changed();
            }
            if (changed) {
                std::lock_guard<std::mutex> lock(g_mtx);
                apply_config(g_config_loader->get(), api);
                // Re-fetch histories for any new symbols
                for (auto& s : g_watchlist.stocks)
                    if (s.history.empty())
                        s.history = api.fetch_history(
                            api_symbol(s.symbol, market_to_type(s.market)),
                            g_timeframe);
                set_status(" ⟳  Config reloaded from config.json");
            }
        }

        screen.PostEvent(Event::Custom);

        // Sleep in 1-second ticks so we can react to g_running quickly
        for (int i = 0; i < 15 && g_running; i++)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ─────────────────────────────────────────────
//  Render helpers — Watchlist panel
// ─────────────────────────────────────────────
Element render_watchlist() {
    Elements rows;
    rows.push_back(
        hbox({
            text(" SYM  ") | bold | color(Color::Yellow),
            text("  MKT") | bold | color(Color::Yellow),
            text("       PRICE") | bold | color(Color::Yellow),
            text("    CHG%") | bold | color(Color::Yellow),
            text("  SPARK") | bold | color(Color::Yellow),
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
            text(type_badge(market_to_type(s.market))) | color(Color::GrayDark) | size(WIDTH, EQUAL, 4),
            text(format_price(s.price)) | color(Color::White) | size(WIDTH, EQUAL, 12),
            text(chg_str) | color(price_color) | size(WIDTH, EQUAL, 9),
            g_chart.sparkline(s.history, s.change >= 0) | size(WIDTH, EQUAL, 10),
        });

        rows.push_back(selected ? (row | bgcolor(Color::GrayDark) | inverted) : row);
    }

    return vbox(rows) | border | size(WIDTH, EQUAL, 44);
}

// ─────────────────────────────────────────────
//  Render helpers — Crypto watchlist panel
// ─────────────────────────────────────────────
Element render_crypto_list() {
    Elements rows;
    rows.push_back(
        hbox({
            text(" COIN ") | bold | color(Color::Yellow),
            text("          PRICE") | bold | color(Color::Yellow),
            text("   CHG%") | bold | color(Color::Yellow),
            text("  SPARK") | bold | color(Color::Yellow),
        }) | bgcolor(Color::Black)
    );
    rows.push_back(separator());

    if (g_crypto_ticks.empty()) {
        rows.push_back(text(" No crypto in watchlist") | color(Color::GrayDark));
        rows.push_back(text(" Add via config.json:") | color(Color::GrayDark));
        rows.push_back(text("  {\"symbol\":\"BTC\",\"market\":\"CRYPTO\"}") | color(Color::GrayDark));
    }

    for (int i = 0; i < (int)g_crypto_ticks.size(); i++) {
        auto& ct = g_crypto_ticks[i];
        bool sel = (i == g_crypto_sel_idx);
        Color pc = ct.change_pct >= 0 ? Color::Green : Color::Red;
        std::string arr = ct.change_pct >= 0 ? "▲" : "▼";

        auto row = hbox({
            text(" " + ct.symbol) | bold | color(Color::Yellow) | size(WIDTH, EQUAL, 6),
            text("$" + format_price(ct.price)) | color(Color::White) | size(WIDTH, EQUAL, 15),
            text(arr + " " + format_pct(std::abs(ct.change_pct))) | color(pc) | size(WIDTH, EQUAL, 9),
            g_chart.crypto_sparkline(ct.history, ct.change_pct >= 0) | size(WIDTH, EQUAL, 10),
        });
        rows.push_back(sel ? (row | bgcolor(Color::GrayDark) | inverted) : row);
    }

    return vbox(rows) | border | size(WIDTH, EQUAL, 44);
}

// ─────────────────────────────────────────────
//  Render helpers — Chart panel
// ─────────────────────────────────────────────
Element render_chart() {
    if (g_watchlist.stocks.empty())
        return text(" No stocks in watchlist ") | center | border | flex;

    auto& s = g_watchlist.stocks[g_selected_idx];
    Color c = s.change >= 0 ? Color::Green : Color::Red;
    std::string arrow = s.change >= 0 ? "▲ +" : "▼ ";

    // ── Header row ──
    auto header = hbox({
        text(s.symbol + " ") | bold | color(Color::Yellow),
        text("[" + s.market + "] ") | color(Color::GrayDark),
        text(format_price(s.price)) | bold | color(Color::White),
        text("  " + arrow + format_price(s.change)
             + " (" + format_pct(s.change_pct) + "%)") | color(c),
        filler(),
        text("O:" + format_price(s.open)) | color(Color::GrayLight),
        text("  H:" + format_price(s.high)) | color(Color::Green),
        text("  L:" + format_price(s.low))  | color(Color::Red),
        text("  VOL:" + s.volume_str) | color(Color::GrayLight),
    });

    // ── Mode badge ──
    auto mode_badge = text(g_chart_mode == ChartMode::LINE ? " LINE " : " CANDLE ")
                    | bold
                    | bgcolor(g_chart_mode == ChartMode::LINE ? Color::Blue : Color::Magenta)
                    | color(Color::White);

    // ── Timeframe selector ──
    auto tf = [&](const std::string& label) {
        bool active = (g_timeframe == label);
        return text(active ? "[" + label + "]" : " " + label + " ")
             | (active ? bold | bgcolor(Color::Yellow) | color(Color::Black)
                       : color(Color::GrayDark));
    };
    auto tf_bar = hbox({
        mode_badge,
        text("  "),
        tf("1D"), tf("5D"), tf("1M"), tf("3M"), tf("1Y"),
        filler(),
        text("↑↓:stock  c:chart  e:export  Tab:panel  Q:quit") | color(Color::GrayDark),
    });

    // ── Chart body ──
    Element chart_elem;
    if (g_chart_mode == ChartMode::CANDLE)
        chart_elem = g_chart.render_candlestick(s.candles);
    else
        chart_elem = g_chart.render(s.history, s.change >= 0);

    return vbox({
        header | border,
        tf_bar | bgcolor(Color::Black),
        separator(),
        chart_elem | flex,
    }) | border | flex;
}

// ─────────────────────────────────────────────
//  Render helpers — Crypto chart
// ─────────────────────────────────────────────
Element render_crypto_chart() {
    if (g_crypto_ticks.empty())
        return text(" No crypto data ") | center | border | flex;

    auto& ct = g_crypto_ticks[g_crypto_sel_idx];
    bool up   = ct.change_pct >= 0;
    Color c   = up ? Color::Green : Color::Red;

    auto header = hbox({
        text("₿ " + ct.symbol + " ") | bold | color(Color::Yellow),
        text("$" + format_price(ct.price)) | bold | color(Color::White),
        text("  " + std::string(up ? "▲ +" : "▼ ")
             + format_pct(std::abs(ct.change_pct)) + "%") | color(c),
        filler(),
        text("H:" + format_price(ct.high_24h)) | color(Color::Green),
        text("  L:" + format_price(ct.low_24h)) | color(Color::Red),
    });

    auto chart_elem = g_chart.render(ct.history, up);

    return vbox({
        header | border,
        separator(),
        chart_elem | flex,
    }) | border | flex;
}

// ─────────────────────────────────────────────
//  Render helpers — Portfolio panel
// ─────────────────────────────────────────────
Element render_portfolio() {
    Elements rows;
    rows.push_back(
        hbox({
            text(" SYM ") | bold | color(Color::Cyan),
            text("    VAL") | bold | color(Color::Cyan),
            text("     P&L") | bold | color(Color::Cyan),
            text("  P&L%") | bold | color(Color::Cyan),
        })
    );
    rows.push_back(separator());

    double total = 0.0, total_pnl = 0.0;
    for (auto& h : g_portfolio.holdings) {
        auto it = std::find_if(g_watchlist.stocks.begin(), g_watchlist.stocks.end(),
            [&](auto& s){ return s.symbol == h.symbol; });
        if (it == g_watchlist.stocks.end()) continue;

        double cur_val  = it->price * h.shares;
        double cost     = h.avg_cost * h.shares;
        double pnl      = cur_val - cost;
        double pnl_pct  = cost > 0 ? (pnl / cost) * 100.0 : 0.0;
        total     += cur_val;
        total_pnl += pnl;

        Color pnl_col = pnl >= 0 ? Color::Green : Color::Red;
        std::string sign = pnl >= 0 ? "+" : "";

        rows.push_back(hbox({
            text(" " + h.symbol) | color(Color::Cyan) | size(WIDTH, EQUAL, 5),
            text(format_price(cur_val))   | size(WIDTH, EQUAL, 8),
            text(sign + format_price(pnl)) | color(pnl_col) | size(WIDTH, EQUAL, 9),
            text(sign + format_pct(pnl_pct) + "%") | color(pnl_col) | size(WIDTH, EQUAL, 7),
        }));
    }

    // ── Crypto holdings sub-section ──
    if (!g_crypto_portfolio.holdings.empty()) {
        rows.push_back(separator());
        rows.push_back(text(" ── CRYPTO ──") | color(Color::Yellow));
        for (auto& h : g_crypto_portfolio.holdings) {
            double cur_price = 0;
            for (auto& ct : g_crypto_ticks)
                if (ct.symbol == h.symbol) { cur_price = ct.price; break; }
            double cur_val  = cur_price * h.amount;
            double cost     = h.avg_cost * h.amount;
            double pnl      = cur_val - cost;
            double pnl_pct  = cost > 0 ? (pnl / cost) * 100.0 : 0.0;
            total     += cur_val;
            total_pnl += pnl;

            Color pnl_col = pnl >= 0 ? Color::Green : Color::Red;
            std::string sign = pnl >= 0 ? "+" : "";
            rows.push_back(hbox({
                text(" " + h.symbol) | color(Color::Yellow) | size(WIDTH, EQUAL, 5),
                text(format_price(cur_val))    | size(WIDTH, EQUAL, 8),
                text(sign + format_price(pnl)) | color(pnl_col) | size(WIDTH, EQUAL, 9),
                text(sign + format_pct(pnl_pct) + "%") | color(pnl_col) | size(WIDTH, EQUAL, 7),
            }));
        }
    }

    rows.push_back(separator());
    rows.push_back(hbox({
        text(" TOTAL") | bold,
        text(format_price(total)) | bold | size(WIDTH, EQUAL, 8),
        text((total_pnl>=0?"+":"")+format_price(total_pnl)) | bold
            | color(total_pnl>=0 ? Color::Green : Color::Red),
    }));

    return vbox(rows) | border | size(HEIGHT, LESS_THAN, 20);
}

// ─────────────────────────────────────────────
//  Render helpers — Alerts panel
// ─────────────────────────────────────────────
Element render_alerts() {
    Elements items;
    items.push_back(text(" ALERTS") | bold | color(Color::Yellow));
    items.push_back(separator());
    for (auto& a : g_alerts.get_recent(10)) {
        Color c = a.type == AlertType::UP   ? Color::Green  :
                  a.type == AlertType::DOWN  ? Color::Red    : Color::Yellow;
        items.push_back(
            hbox({
                text(a.timestamp + " ") | color(Color::GrayDark) | size(WIDTH, EQUAL, 9),
                text(a.message) | color(c),
            })
        );
    }
    if (g_alerts.get_recent(1).empty())
        items.push_back(text(" No alerts yet — use 'a' to add") | color(Color::GrayDark));
    return vbox(items) | border | flex;
}

// ─────────────────────────────────────────────
//  Render helpers — Ticker tape
// ─────────────────────────────────────────────
Element render_ticker() {
    Elements ticks;
    ticks.push_back(text("◆ LIVE │ ") | bold | bgcolor(Color::Yellow) | color(Color::Black));
    for (auto& s : g_watchlist.stocks) {
        Color c = s.change >= 0 ? Color::Green : Color::Red;
        ticks.push_back(text(s.symbol + " ") | bold | color(Color::Yellow));
        ticks.push_back(text(format_price(s.price) + " "));
        ticks.push_back(
            text((s.change>=0?"▲":"▼") + format_pct(std::abs(s.change_pct)) + "% │ ")
            | color(c));
    }
    // Crypto in ticker too
    for (auto& ct : g_crypto_ticks) {
        ticks.push_back(text("₿" + ct.symbol + " ") | bold | color(Color::Yellow));
        ticks.push_back(text("$" + format_price(ct.price) + " │ ") | color(Color::Cyan));
    }
    return hbox(ticks) | bgcolor(Color::Black);
}

// ─────────────────────────────────────────────
//  Render helpers — Status bar
// ─────────────────────────────────────────────
Element render_statusbar() {
    std::string ephem = current_status();
    auto now = get_time_str();

    // Active panel indicator
    std::string panel_ind = g_panel == Panel::WATCHLIST ? "STOCKS"
                          : g_panel == Panel::CRYPTO    ? "CRYPTO"
                          :                               "PORTFOLIO";

    return hbox({
        text(" ● ") | color(Color::Green),
        text("FEED ACTIVE") | color(Color::GrayLight),
        ephem.empty() ? filler()
                      : hbox({text("  │  "), text(ephem) | color(Color::Cyan)}),
        filler(),
        text("Panel: [" + panel_ind + "]  ") | color(Color::Yellow),
        text("TermiTrade v1.1  │  " + now + " IST ") | color(Color::GrayDark),
    }) | bgcolor(Color::Black);
}

// ─────────────────────────────────────────────
//  Panel tab bar (shown between ticker and main layout)
// ─────────────────────────────────────────────
Element render_tab_bar() {
    auto tab = [&](const std::string& label, Panel p) {
        bool active = (g_panel == p);
        return text(active ? "[ " + label + " ]" : "  " + label + "  ")
             | (active ? bold | bgcolor(Color::Blue) | color(Color::White)
                       : color(Color::GrayDark));
    };
    return hbox({
        tab("STOCKS",    Panel::WATCHLIST),
        text(" "),
        tab("CRYPTO",    Panel::CRYPTO),
        text(" "),
        tab("PORTFOLIO", Panel::PORTFOLIO),
        filler(),
        text(" Tab: switch panel ") | color(Color::GrayDark),
    }) | bgcolor(Color::Black);
}

// ─────────────────────────────────────────────
//  Main layout renderer
// ─────────────────────────────────────────────
Element render_main() {
    Element left_panel, center_panel;

    if (g_panel == Panel::CRYPTO) {
        left_panel   = render_crypto_list();
        center_panel = render_crypto_chart() | flex;
    } else {
        left_panel   = render_watchlist();
        center_panel = render_chart() | flex;
    }

    Element right_panel = vbox({
        render_portfolio(),
        separator(),
        render_alerts() | flex,
    }) | size(WIDTH, EQUAL, 40);

    if (g_panel == Panel::PORTFOLIO) {
        // Portfolio panel takes center stage
        return vbox({
            render_ticker(),
            render_tab_bar(),
            separator(),
            hbox({
                render_portfolio() | flex,
                separator(),
                render_alerts() | flex,
            }) | flex,
            separator(),
            render_statusbar(),
        });
    }

    return vbox({
        render_ticker(),
        render_tab_bar(),
        separator(),
        hbox({
            left_panel,
            separator(),
            center_panel,
            separator(),
            right_panel,
        }) | flex,
        separator(),
        render_statusbar(),
    });
}

// ─────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────
int main(int argc, char* argv[]) {
    // ── Parse flags ──────────────────────────
    bool use_ncurses = false;
    std::string api_key = "demo";
    std::string config_path = "config.json";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--ncurses")         { use_ncurses = true; continue; }
        if (arg == "--config" && i+1 < argc) { config_path = argv[++i]; continue; }
        api_key = arg;  // positional: API key
    }

    // Check env var API key
    if (api_key == "demo" && getenv("AV_API_KEY"))
        api_key = getenv("AV_API_KEY");

    // Detect dumb terminal → ncurses fallback
    const char* term = getenv("TERM");
    if (term && (std::string(term) == "dumb" || std::string(term) == "unknown"))
        use_ncurses = true;

    // ── Config loader ────────────────────────
    ConfigLoader cfg_loader(config_path);
    g_config_loader = &cfg_loader;

    // Write example config if none exists
    {
        std::ifstream test(config_path);
        if (!test.is_open()) {
            ConfigLoader::write_example(config_path);
            std::cout << "[config] Created " << config_path << " — edit it and restart, or it will hot-reload.\n";
        }
    }
    cfg_loader.load();
    const auto& cfg = cfg_loader.get();
    if (cfg.api_key != "demo" && cfg.api_key != "YOUR_KEY_HERE")
        api_key = cfg.api_key;

    // ── API client ───────────────────────────
    ApiClient api(api_key);

    // ── Seed global state from config ────────
    {
        // Fallback hardcoded defaults if config is empty
        if (cfg.watchlist.empty()) {
            g_watchlist.add("AAPL",  "Apple Inc.");
            g_watchlist.add("GOOGL", "Alphabet Inc.");
            g_watchlist.add("MSFT",  "Microsoft Corp.");
            g_watchlist.add("TSLA",  "Tesla Inc.");
            g_watchlist.add("NVDA",  "NVIDIA Corp.");
            g_watchlist.add("META",  "Meta Platforms");
            g_portfolio.add("AAPL", 10, 160.00);
            g_portfolio.add("NVDA",  5, 580.00);
            g_portfolio.add("MSFT",  8, 350.00);
            g_portfolio.add("TSLA", 15, 220.00);
            g_alerts.add_price_alert("AAPL", 190.0, AlertDirection::ABOVE);
            g_alerts.add_price_alert("TSLA", 240.0, AlertDirection::BELOW);
        } else {
            apply_config(cfg, api);
        }
    }
    g_timeframe = cfg.default_timeframe;

    // ── Initial data fetch ───────────────────
    for (auto& s : g_watchlist.stocks) {
        std::string sym = api_symbol(s.symbol, market_to_type(s.market));
        s.history = api.fetch_history(sym, g_timeframe);
        s.candles = api.fetch_ohlc(sym, g_timeframe);
    }
    for (auto& ct : g_crypto_ticks) {
        ct.history = api.fetch_crypto_history(ct.symbol);
        auto q = api.fetch_crypto_quote(ct.symbol);
        if (!q.empty()) ct.price = q["price"];
    }

    // ── ncurses fallback path ────────────────
    if (use_ncurses) {
        auto alerts = g_alerts.get_recent(20);
        NcursesApp nc_app(g_watchlist.stocks, g_portfolio.holdings,
                          alerts, g_mtx, g_running);
        // Start a background updater that doesn't need a ScreenInteractive
        std::thread upd([&](){
            while (g_running) {
                {
                    std::lock_guard<std::mutex> lock(g_mtx);
                    for (auto& s : g_watchlist.stocks) {
                        auto d = api.fetch_quote(api_symbol(s.symbol, market_to_type(s.market)));
                        if (!d.empty()) { s.price = d["price"]; s.change = d["change"]; s.change_pct = d["change_pct"]; }
                    }
                }
                for (int i = 0; i < 15 && g_running; i++)
                    std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
        nc_app.run();
        g_running = false;
        upd.join();
        return 0;
    }

    // ── FTXUI interactive app ────────────────
    auto screen = ScreenInteractive::Fullscreen();

    std::thread updater([&](){ price_updater_thread(api, screen); });

    auto renderer = Renderer([&]() {
        std::lock_guard<std::mutex> lock(g_mtx);
        return render_main();
    });

    auto component = CatchEvent(renderer, [&](Event e) {
        std::lock_guard<std::mutex> lock(g_mtx);

        // ── Quit ──
        if (e == Event::Character('q') || e == Event::Character('Q')) {
            g_running = false;
            screen.ExitLoopClosure()();
            return true;
        }

        // ── Tab: cycle panels ──
        if (e == Event::Tab) {
            g_panel = g_panel == Panel::WATCHLIST ? Panel::CRYPTO
                    : g_panel == Panel::CRYPTO    ? Panel::PORTFOLIO
                    :                               Panel::WATCHLIST;
            return true;
        }

        // ── Navigation ──
        if (g_panel == Panel::CRYPTO) {
            if (e == Event::ArrowDown || e == Event::Character('j'))
                g_crypto_sel_idx = std::min((int)g_crypto_ticks.size()-1, g_crypto_sel_idx+1);
            if (e == Event::ArrowUp || e == Event::Character('k'))
                g_crypto_sel_idx = std::max(0, g_crypto_sel_idx-1);
        } else {
            if (e == Event::ArrowDown || e == Event::Character('j')) {
                g_selected_idx = std::min((int)g_watchlist.stocks.size()-1, g_selected_idx+1);
                if (!g_watchlist.stocks.empty())
                    g_selected_sym = g_watchlist.stocks[g_selected_idx].symbol;
            }
            if (e == Event::ArrowUp || e == Event::Character('k')) {
                g_selected_idx = std::max(0, g_selected_idx-1);
                if (!g_watchlist.stocks.empty())
                    g_selected_sym = g_watchlist.stocks[g_selected_idx].symbol;
            }
        }

        // ── Timeframes ──
        if (e == Event::Character('1')) { g_timeframe = "1D"; return true; }
        if (e == Event::Character('5')) { g_timeframe = "5D"; return true; }
        if (e == Event::Character('m')) { g_timeframe = "1M"; return true; }
        if (e == Event::Character('3')) { g_timeframe = "3M"; return true; }
        if (e == Event::Character('y')) { g_timeframe = "1Y"; return true; }

        // ── Toggle chart mode (Line / Candlestick) ──
        if (e == Event::Character('c')) {
            g_chart_mode = (g_chart_mode == ChartMode::LINE)
                         ? ChartMode::CANDLE : ChartMode::LINE;
            g_chart.mode = g_chart_mode;
            set_status(" Chart: " + std::string(g_chart_mode == ChartMode::LINE
                                                ? "Line" : "Candlestick"));
            return true;
        }

        // ── Force refresh ──
        if (e == Event::Character('r') || e == Event::Character('R')) {
            // Re-fetch history + candles for selected symbol (non-blocking here — trigger thread)
            set_status(" ⟳  Refreshing prices…");
            return true;
        }

        // ── Export portfolio CSV ──
        if (e == Event::Character('e') || e == Event::Character('E')) {
            try {
                std::string path = PortfolioExporter::export_csv(
                    g_watchlist.stocks,
                    g_portfolio.holdings,
                    g_crypto_ticks,
                    g_crypto_portfolio.holdings);
                set_status(" ✔  Saved: " + path, 6);
            } catch (const std::exception& ex) {
                set_status(" ✘  Export failed: " + std::string(ex.what()), 6);
            }
            return true;
        }

        return false;
    });

    screen.Loop(component);
    updater.join();
    return 0;
}
